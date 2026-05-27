-- nt.net.afd — IPv4 sockets over \Device\Afd.
--
-- AFD is the kernel-side socket emulation layer above TDI. We open
-- \Device\Afd with an EA buffer naming the underlying transport
-- (\Device\Tcp or \Device\Udp); IOCTL_AFD_* + IOCTL_TDI_CONNECT do
-- the bind/listen/accept/connect operations. AFD repurposes
-- IRP_MJ_READ / IRP_MJ_WRITE on the socket handle as recv / send,
-- so once you've got a connected socket, you can just use the
-- existing fs.NtReadFile / fs.NtWriteFile wrappers verbatim — no
-- separate send/recv helpers needed in this module.
--
-- This module is *not* under nt/dll/ because AFD isn't an ntdll
-- export; it's a kernel device the user-mode side talks to via
-- NtCreateFile + NtDeviceIoControlFile. Same pattern as nt.tree
-- (also non-dll: it composes ntdll calls into a higher-level
-- abstraction).
--
-- Coverage (v1):
--   tcp() / udp()                     Construct a fresh socket NT_HANDLE.
--   bind(s, host, port)               Bind to a local address.
--   connect(s, host, port, [timeout]) TCP: real 3-way handshake. UDP:
--                                     stash remote address so subsequent
--                                     send/recv use it.
--   listen(s, backlog)                TCP only. Sets up the connection
--                                     backlog.
--   accept(s, [timeout])              TCP only. Blocks until a connection
--                                     arrives. Returns a NEW socket handle
--                                     bound to the peer.
--   send(s, data, [timeout])          Write bytes to a connected socket.
--   recv(s, max_bytes, [timeout])     Read up to max_bytes; returns string.
--   getsockname(s)                    Returns (host_str, port) of the
--                                     local bound address.
--
-- All blocking operations take an optional timeout in seconds. Omit or
-- pass nil for infinite. On timeout we issue NtCancelIoFile, drain the
-- IRP via an infinite wait so the kernel is no longer touching our
-- buffers, then raise STATUS_CANCELLED via the standard nt.errors path.
--
-- I/O model: sockets are opened *without* FILE_SYNCHRONOUS_IO_NONALERT
-- so every IRP can pend. The internal io_wait helper threads an Event
-- handle through each NtRead/NtWrite/NtDeviceIoControlFile call; on
-- STATUS_PENDING it waits on the Event, on success it returns iosb
-- straight away. Going async unlocks timeouts without forking the API
-- surface, and keeps us source-compatible with a future IOCP layer.
--
-- Unconnected UDP (sendto / recvfrom) is wrapped via udp_sendto /
-- udp_recvfrom — IOCTL_TDI_SEND_DATAGRAM / IOCTL_TDI_RECEIVE_DATAGRAM
-- with AFD's "fast" buffer shapes (AfdSendDatagram in SEND.C and
-- AfdReceiveDatagram in RECVDG.C).  Used by DHCP, which needs to
-- broadcast DISCOVER before it has a peer to connect() to.
--
-- Lifetime: every socket is an NT_HANDLE wrapper. :close() goes
-- through the standard NT_HANDLE __gc / NtClose path.

local ffi    = require('ffi')
local bit    = require('bit')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local fs     = require('nt.dll.fs')
local handle = require('nt.dll.handle')
local ke     = require('nt.dll.ke')
local oa     = require('nt.dll.oa')
local str    = require('nt.dll.str')

-- ------------------------------------------------------------------
-- Type definitions (matching NT 3.5 PRIVATE/INC/AFD.H + PRIVATE/INC/TDI.H).
-- All structures are pack(1) — they're network-protocol shapes that
-- ride on the wire / between user and kernel as exact byte layouts.
-- ------------------------------------------------------------------

ffi.cdef[[
#pragma pack(push, 1)

typedef struct _AFD_OPEN_PACKET {
    long  EndpointType;            /* AFD_ENDPOINT_TYPE enum */
    unsigned long  TransportDeviceNameLength;  /* bytes, not wchars */
    wchar_t TransportDeviceName[?];
} AFD_OPEN_PACKET;

typedef struct _TDI_ADDRESS_IP {
    unsigned short sin_port;       /* network byte order */
    unsigned long  in_addr;        /* network byte order */
    unsigned char  sin_zero[8];
} TDI_ADDRESS_IP;

/* TA_IP_ADDRESS as one self-contained struct; matches NT layout
 * exactly under pack(1). 22 bytes total. */
typedef struct _TA_IP_ADDRESS {
    long           TAAddressCount;       /* always 1 for our use */
    unsigned short AddressLength;        /* always sizeof(TDI_ADDRESS_IP) = 14 */
    unsigned short AddressType;          /* TDI_ADDRESS_TYPE_IP = 2 */
    TDI_ADDRESS_IP Address;
} TA_IP_ADDRESS;

/* TDI_ADDRESS_INFO is what TDI_QUERY_ADDRESS_INFO writes back into
 * the IOCTL_AFD_GET_ADDRESS output buffer — a 4-byte ActivityCount
 * then a TRANSPORT_ADDRESS. We mirror it as TDI_ADDRESS_INFO_IP so
 * we can size the buffer (26 bytes) and reach the address fields
 * without offset arithmetic. */
typedef struct _TDI_ADDRESS_INFO_IP {
    unsigned long ActivityCount;
    TA_IP_ADDRESS Address;
} TDI_ADDRESS_INFO_IP;

typedef struct _AFD_LISTEN_INFO {
    unsigned long MaximumConnectionQueue;
} AFD_LISTEN_INFO;

typedef struct _AFD_LISTEN_RESPONSE_INFO {
    unsigned long Sequence;
    TA_IP_ADDRESS RemoteAddress;
} AFD_LISTEN_RESPONSE_INFO;

typedef struct _AFD_ACCEPT_INFO {
    unsigned long Sequence;
    void *        AcceptHandle;
} AFD_ACCEPT_INFO;

/* TDI_REQUEST + TDI_REQUEST_CONNECT laid out flat with the inline
 * address buffer following — gives us a single contiguous user-mode
 * buffer the kernel can pointer-fix-up via the (PVOID - UserBuffer)
 * trick in CONNECT.C. */
typedef struct _TDI_REQUEST {
    void *Handle;                 /* AddressHandle / ConnectionContext */
    void *RequestNotifyObject;
    void *RequestContext;
    long  TdiStatus;
} TDI_REQUEST;

typedef struct _TDI_CONNECTION_INFORMATION {
    long  UserDataLength;
    void *UserData;
    long  OptionsLength;
    void *Options;
    long  RemoteAddressLength;
    void *RemoteAddress;
} TDI_CONNECTION_INFORMATION;

/* Send-datagram contiguous buffer.  Mirrors the connect-buffer
 * pointer-fixup pattern: SendDatagramInformation points at the
 * embedded TDI_CONNECTION_INFORMATION, which in turn points at the
 * embedded TA_IP_ADDRESS.  Kernel does the user-mode pointer
 * dereferences (SEND.C:1005-1011) — buffer must live across the
 * IOCTL.  Total 66 bytes under pack(1). */
typedef struct _AFD_SEND_DG_BUF {
    TDI_REQUEST                Request;
    TDI_CONNECTION_INFORMATION *SendDatagramInformation;
    TDI_CONNECTION_INFORMATION ConnInfo;
    TA_IP_ADDRESS              Addr;
} AFD_SEND_DG_BUF;

/* Receive-datagram control buffer.  Single allocation that serves
 * as both InputBuffer (kernel reads ReceiveFlags at offset 0 and
 * OutputBuffer pointer at offset 4) AND output destination (kernel
 * writes ReceiveLength/AddressLength/source-address starting at
 * offset 0 — overwriting the input fields).  Aliasing-back to
 * itself is the Winsock convention: WINSOCK/RECV.C:402-403 declares
 * `receiveInput = receiveOutput = requestBuffer` and sets
 * `receiveInput->OutputBuffer = receiveOutput`.  Necessary because
 * AFD's fast path (FASTIO.C:1245) writes directly to InputBuffer
 * offsets, ignoring the OutputBuffer field — pointing OutputBuffer
 * at a separate allocation breaks the fast path silently.
 *
 * Total size must be >= AFD_FAST_RECVDG_BUFFER_LENGTH (58) to pass
 * AfdReceiveDatagram's length check (RECVDG.C:107).  We size to 64
 * with a trailing pad to absorb any /Zp8 alignment differences. */
typedef struct _AFD_RECV_DG_CTRL {
    /* INPUT view (before IOCTL): */
    /*   offset 0..3  ReceiveFlags                                    */
    /*   offset 4..7  OutputBuffer pointer (set to &self)             */
    /* OUTPUT view (after IOCTL):                                     */
    /*   offset 0..3  ReceiveLength                                   */
    /*   offset 4..7  AddressLength                                   */
    /*   offset 8..n  TRANSPORT_ADDRESS (TAAddressCount + TA_ADDRESS) */
    unsigned long  ReceiveLength_or_Flags;
    unsigned long  AddressLength_or_OutputBuffer;
    long           TAAddressCount;
    unsigned short InnerAddrLength;
    unsigned short InnerAddrType;
    TDI_ADDRESS_IP InnerAddr;
    unsigned char  pad[34];
} AFD_RECV_DG_CTRL;

/* AFD_INFORMATION — payload for IOCTL_AFD_GET/SET_INFORMATION.
 * The kernel struct is { ULONG; union { BOOLEAN; ULONG; LARGE_INTEGER } }.
 * Under /Zp8 the union (containing LARGE_INTEGER) is 8-byte aligned, so
 * the kernel sees sizeof(AFD_INFORMATION) == 16 with InformationType at
 * offset 0 and the union at offset 8.  The kernel's input/output buffer
 * checks compare against sizeof(*afdInfo), so we must match that exact
 * layout.  We flatten the union into two ULONGs because every documented
 * info class reads/writes the low 32 bits (BOOLEAN aliases the low byte,
 * LARGE_INTEGER.HighPart is unused by the classes AFD exposes). */
typedef struct _AFD_INFORMATION {
    unsigned long InformationType;       /* offset 0 */
    unsigned long Pad_AlignUnionToEight; /* offset 4 — kernel ignores  */
    unsigned long Information_Ulong;     /* offset 8 — union low dword */
    unsigned long Information_HighPart;  /* offset 12 — union high dword */
} AFD_INFORMATION;

typedef struct _AFD_CONNECT_BUFFER {
    /* TDI_REQUEST_CONNECT header. */
    TDI_REQUEST                Request;
    TDI_CONNECTION_INFORMATION *RequestConnectionInformation;
    TDI_CONNECTION_INFORMATION *ReturnConnectionInformation;
    long long                  Timeout;
    /* Inline structures pointed at by the headers above. The kernel
     * resolves user-mode pointers via offset-from-user-buffer math
     * (CONNECT.C:185 stock; bounds-checked through
     * AfdMapUserBufferPointer post-patch), so RequestConnectionInformation
     * and ReturnConnectionInformation must point WITHIN this same
     * allocation. ReturnInfo is required for TCP — AfdConnect's
     * stock code dereferences a wild pointer if Return is NULL, and
     * even with the patch the validator rejects NULL. UDP doesn't
     * use Return, but we always include it for layout simplicity. */
    TDI_CONNECTION_INFORMATION RequestInfo;
    TDI_CONNECTION_INFORMATION ReturnInfo;
    TA_IP_ADDRESS              RequestAddr;
} AFD_CONNECT_BUFFER;

#pragma pack(pop)

NTSTATUS __stdcall NtCancelIoFile(HANDLE FileHandle,
                                  IO_STATUS_BLOCK *IoStatusBlock);
]]

-- ------------------------------------------------------------------
-- IOCTL codes (AFD.H + NTDDTDI.H, computed inline rather than cdef'd
-- so they're plain Lua ints).
-- ------------------------------------------------------------------
--
-- AFD: ((FILE_DEVICE_NETWORK=0x12)<<12) | (request<<2) | method
--      bind(0)/start_listen(1)/wait_for_listen(2)/accept(3)/poll(4) all
--      METHOD_BUFFERED(0).
-- TDI: CTL_CODE(FILE_DEVICE_TRANSPORT=0x21, function, method, FILE_ANY_ACCESS=0)
--      = (0x21 << 16) | (function << 2) | method.

local IOCTL_AFD_BIND               = 0x12000
local IOCTL_AFD_START_LISTEN       = 0x12004
local IOCTL_AFD_WAIT_FOR_LISTEN    = 0x12008
local IOCTL_AFD_ACCEPT             = 0x1200C
local IOCTL_AFD_GET_ADDRESS        = 0x1201A   -- METHOD_OUT_DIRECT (= 2 in low bits)
local IOCTL_AFD_SET_INFORMATION    = 0x12024   -- _AFD_CONTROL_CODE(9,  METHOD_BUFFERED)
local IOCTL_AFD_GET_INFORMATION    = 0x12064   -- _AFD_CONTROL_CODE(25, METHOD_BUFFERED)

-- AFD_INFORMATION.InformationType values.  Boolean classes read/write
-- the low byte of Ulong; range classes use the whole Ulong.
local AFD_INLINE_MODE          = 0x01   -- BOOLEAN; SET routes via AfdSetInLineMode on connected TCP
local AFD_NONBLOCKING_MODE     = 0x02   -- BOOLEAN
local AFD_MAX_SEND_SIZE        = 0x03   -- ULONG   (read-only)
local AFD_SENDS_PENDING        = 0x04   -- ULONG   (read-only)
local AFD_MAX_PATH_SEND_SIZE   = 0x05   -- ULONG   (read-only; takes optional remote addr)
local AFD_RECEIVE_WINDOW_SIZE  = 0x06   -- ULONG
local AFD_SEND_WINDOW_SIZE     = 0x07   -- ULONG

local IOCTL_TDI_CONNECT            = 0x210004

-- (0x21<<16) | (6<<2) | METHOD_OUT_DIRECT(2) = 0x21001A
local IOCTL_TDI_RECEIVE_DATAGRAM   = 0x21001A
-- (0x21<<16) | (8<<2) | METHOD_IN_DIRECT(1) = 0x210021
local IOCTL_TDI_SEND_DATAGRAM      = 0x210021

local TDI_RECEIVE_NORMAL    = 0x20

-- Length floor for IOCTL_TDI_RECEIVE_DATAGRAM input buffer
-- (AfdReceiveDatagram in RECVDG.C:107-112 rejects under this).
-- Evaluates to sizeof(AFD_RECEIVE_DATAGRAM_INPUT) +
-- sizeof(AFD_RECEIVE_DATAGRAM_OUTPUT) + AFD_MAX_TDI_FAST_ADDRESS
-- = 8 + 18 + 32 = 58 on /Zp8 builds; we round to 64 to absorb any
-- alignment gunk and not have to track the kernel's exact sizeof.
local AFD_RECVDG_INPUT_BYTES = 64

-- AFD_ENDPOINT_TYPE enum.
local AfdEndpointTypeStream     = 0
local AfdEndpointTypeDatagram   = 1

-- TDI address family.
local TDI_ADDRESS_TYPE_IP       = 2
local TDI_ADDRESS_LENGTH_IP     = 14   -- sizeof(TDI_ADDRESS_IP)

-- The EA name AFD's CREATE.C looks for. The "XX" suffix is alignment
-- padding so the AFD_OPEN_PACKET that follows is 4-byte aligned;
-- AFD itself parses the EA at `EaName + EaNameLength + 1`, so the
-- name must be exactly this string. 15 chars, no NUL counted.
local AfdOpenPacketName = "AfdOpenPacketXX"

-- ------------------------------------------------------------------
-- IPv4 helpers
-- ------------------------------------------------------------------

-- Parse a dotted-quad string into (b0, b1, b2, b3) — the four octets
-- in network order. Raises on malformed input. We don't accept names
-- (no DNS resolver yet); callers pass numeric IPs.
local function parse_ipv4(s)
    if type(s) ~= "string" then
        error("nt.net.afd: expected dotted-quad string, got " .. type(s), 3)
    end
    local b0, b1, b2, b3 = s:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
    if not b0 then
        error("nt.net.afd: bad IPv4 address: " .. s, 3)
    end
    b0, b1, b2, b3 = tonumber(b0), tonumber(b1), tonumber(b2), tonumber(b3)
    if b0 > 255 or b1 > 255 or b2 > 255 or b3 > 255 then
        error("nt.net.afd: octet out of range in: " .. s, 3)
    end
    return b0, b1, b2, b3
end

-- Format four octets back into a dotted-quad string.
local function format_ipv4(b0, b1, b2, b3)
    return string.format("%d.%d.%d.%d", b0, b1, b2, b3)
end

-- Fill in a TDI_ADDRESS_IP from (host, port). Both fields go in
-- network byte order; we write the bytes directly via uint8_t cast
-- so we don't have to think about the host's endianness.
local function fill_addr(addr, host, port)
    local b0, b1, b2, b3 = parse_ipv4(host)
    -- sin_port: 2 bytes big-endian.
    local pb = ffi.cast('uint8_t *', addr) + 0
    pb[0] = bit.band(bit.rshift(port, 8), 0xFF)   -- high byte first
    pb[1] = bit.band(port, 0xFF)
    -- in_addr: 4 bytes (already in net order from the dotted-quad).
    pb[2] = b0
    pb[3] = b1
    pb[4] = b2
    pb[5] = b3
    -- sin_zero: zeroed by ffi.new (caller's responsibility if reused).
end

-- Inverse: read a TDI_ADDRESS_IP cdata and return (host_str, port).
local function read_addr(addr)
    local pb = ffi.cast('uint8_t *', addr)
    local port = (pb[0] * 256) + pb[1]               -- big-endian → host
    return format_ipv4(pb[2], pb[3], pb[4], pb[5]), port
end

-- Build a fully-formed TA_IP_ADDRESS cdata from (host, port). Single
-- 22-byte allocation; caller keeps the cdata reachable across the
-- syscall.  When `dhcp_marker` is true, stuff the magic 0x12345678
-- into sin_zero[0..3] — NTDISP.C's IsDHCPZeroAddress (line 3300)
-- looks for that exact value to flag the AddrObj as DHCP-mode, which
-- makes UDPSend skip the route lookup that would otherwise fail on
-- an interface without a NTE_VALID address (UDP.C:769-780).
local function make_ta_ip_address(host, port, dhcp_marker)
    local ta = ffi.new('TA_IP_ADDRESS')
    ta.TAAddressCount = 1
    ta.AddressLength  = TDI_ADDRESS_LENGTH_IP
    ta.AddressType    = TDI_ADDRESS_TYPE_IP
    fill_addr(ta.Address, host, port)
    if dhcp_marker then
        -- sin_zero is at offset 6 of TDI_ADDRESS_IP (2-byte sin_port +
        -- 4-byte in_addr).  Write ULONG 0x12345678 in native byte order
        -- — the kernel reads it as `*(ULONG *)sin_zero`.
        ffi.cast('uint32_t *', ta.Address.sin_zero)[0] = 0x12345678
    end
    return ta
end

-- ------------------------------------------------------------------
-- Socket creation: NtCreateFile on \Device\Afd with the EA buffer
-- the kernel's AfdCreate (CREATE.C) parses.
-- ------------------------------------------------------------------
--
-- The EA buffer layout the kernel walks:
--   FILE_FULL_EA_INFORMATION {
--     ULONG  NextEntryOffset = 0
--     UCHAR  Flags           = 0
--     UCHAR  EaNameLength    = 15  (length of "AfdOpenPacketXX")
--     USHORT EaValueLength   = sizeof header + transport name bytes
--     CHAR   EaName[15]      = "AfdOpenPacketXX"
--     CHAR   NUL             = 0   (always present after EaName)
--     -- AFD_OPEN_PACKET starts at offset 8+15+1 = 24 (4-byte aligned).
--     ULONG  EndpointType
--     ULONG  TransportDeviceNameLength    (in bytes)
--     WCHAR  TransportDeviceName[]
--   }
local FILE_FULL_EA_HEADER_BYTES = 8     -- offsetof(FILE_FULL_EA_INFORMATION, EaName)
local AFD_EA_NAME_BYTES         = #AfdOpenPacketName

ffi.cdef[[
#pragma pack(push, 1)
typedef struct _FILE_FULL_EA_INFORMATION_HDR {
    unsigned long  NextEntryOffset;
    unsigned char  Flags;
    unsigned char  EaNameLength;
    unsigned short EaValueLength;
} FILE_FULL_EA_INFORMATION_HDR;
#pragma pack(pop)
]]

-- Open access mask. AFD's CREATE.C doesn't really care about the
-- access bits the user requested — it's a synthetic device — but
-- we ask for the typical bundle so NtReadFile / NtWriteFile on the
-- resulting handle work without further fuss.
local SOCK_ACCESS = bit.bor(fs.FILE_GENERIC_READ,
                            fs.FILE_GENERIC_WRITE,
                            fs.SYNCHRONIZE)
-- Open the socket *without* FILE_SYNCHRONOUS_IO_NONALERT. Every IRP
-- routes through an Event so we can support timeouts (and, later,
-- IOCP/poll multiplexing). The io_wait helper below threads the
-- Event through each NtRead/NtWrite/NtDeviceIoControlFile call.
local SOCK_OPTIONS = 0

-- ------------------------------------------------------------------
-- Async I/O bridge — every IRP gets an Event handle. STATUS_PENDING
-- triggers a wait with optional timeout; on timeout we cancel and
-- drain so the kernel is no longer touching our buffers when the
-- error propagates.
-- ------------------------------------------------------------------

local STATUS_PENDING    = 0x00000103
local STATUS_TIMEOUT    = 0x00000102
local STATUS_CANCELLED  = 0xC0000120
local STATUS_END_OF_FILE = 0xC0000011

local EVENT_ALL_ACCESS  = 0x1F0003

-- The "fresh" zero offset used for socket NtReadFile/NtWriteFile.
-- The I/O manager's NtReadFile probe rejects async handles with a
-- NULL ByteOffset (NT 3.5 IO/RW.C). AFD itself ignores the value.
local function zero_offset() return ffi.new('LARGE_INTEGER') end

-- Wait on `event` for the IRP to complete, honouring an optional
-- timeout. On entry `st` is whatever the issuing syscall returned
-- (success / informational / STATUS_PENDING / error). Returns
-- (iosb.Information, normalized status). Raises on hard errors and
-- on timeout (after cancelling and draining).
local function io_wait(fn_name, sock, event, iosb, st, timeout_secs)
    local stu = err.normalize(st)
    if stu == STATUS_PENDING then
        local li = ke.timeout(timeout_secs)
        local wst  = ntdll.NtWaitForSingleObject(handle.raw(event), 0, li)
        local wstu = err.normalize(wst)
        if wstu == STATUS_TIMEOUT then
            -- Cancel the IRP and wait for completion before unwinding;
            -- the kernel may still write to iosb / our buffers between
            -- the cancel call and IRP teardown, so the infinite drain
            -- is mandatory regardless of cancel's return status.
            local cancel_iosb = ffi.new('IO_STATUS_BLOCK')
            ntdll.NtCancelIoFile(handle.raw(sock), cancel_iosb)
            ntdll.NtWaitForSingleObject(handle.raw(event), 0, nil)
            err.raise(fn_name .. ' (timeout)', STATUS_CANCELLED)
        end
        if wstu ~= 0 then
            err.raise(fn_name .. ' (wait)', wst)
        end
        st  = iosb.Status
        stu = err.normalize(st)
    end
    if stu == STATUS_END_OF_FILE then
        return iosb.Information, stu
    end
    if err.is_error(st) then
        err.raise(fn_name, st)
    end
    return iosb.Information, stu
end

local function ioctl(sock, code, in_buf, in_len, out_buf, out_len, timeout_secs)
    local event = ke.NtCreateEvent(EVENT_ALL_ACCESS, nil, 0, false)
    local iosb  = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtDeviceIoControlFile(handle.raw(sock),
                                           handle.raw(event), nil, nil,
                                           iosb, code,
                                           in_buf,  in_len  or 0,
                                           out_buf, out_len or 0)
    return io_wait('NtDeviceIoControlFile', sock, event, iosb, st, timeout_secs)
end

local function read_io(sock, buf, len, timeout_secs)
    local event  = ke.NtCreateEvent(EVENT_ALL_ACCESS, nil, 0, false)
    local iosb   = ffi.new('IO_STATUS_BLOCK')
    local offset = zero_offset()
    local st = ntdll.NtReadFile(handle.raw(sock),
                                handle.raw(event), nil, nil,
                                iosb, buf, len, offset, nil)
    return io_wait('NtReadFile', sock, event, iosb, st, timeout_secs)
end

local function write_io(sock, buf, len, timeout_secs)
    local event  = ke.NtCreateEvent(EVENT_ALL_ACCESS, nil, 0, false)
    local iosb   = ffi.new('IO_STATUS_BLOCK')
    local offset = zero_offset()
    local st = ntdll.NtWriteFile(handle.raw(sock),
                                 handle.raw(event), nil, nil,
                                 iosb, buf, len, offset, nil)
    return io_wait('NtWriteFile', sock, event, iosb, st, timeout_secs)
end

-- Make a socket. `kind` is 'tcp' or 'udp'. Returns an NT_HANDLE.
local function socket(kind)
    local endpoint_type, transport_name
    if kind == 'tcp' or kind == 'stream' then
        endpoint_type   = AfdEndpointTypeStream
        transport_name  = "\\Device\\Tcp"
    elseif kind == 'udp' or kind == 'datagram' then
        endpoint_type   = AfdEndpointTypeDatagram
        transport_name  = "\\Device\\Udp"
    else
        error("nt.net.afd.socket: kind must be 'tcp' or 'udp', got " ..
              tostring(kind), 2)
    end

    -- Encode transport name as UTF-16LE inline.
    local wchars = str.decode_utf8(transport_name)
    local nwch   = #wchars
    local name_bytes = nwch * 2

    -- AFD's CREATE.C validates `EaValueLength >= sizeof(AFD_OPEN_PACKET)
    -- + TransportDeviceNameLength`. The kernel's struct has a WCHAR[1]
    -- placeholder inline and is built with /Zp8, so its sizeof is 12
    -- (4 EndpointType + 4 NameLength + 2 WCHAR[1] + 2 tail-pad). Our
    -- actual data only fills 8 + name_bytes, but we have to satisfy
    -- the kernel's check — so the EA value gets 4 bytes of trailing
    -- zero pad. The kernel never reads those bytes; they exist only
    -- to make the length check pass.
    local KERNEL_OPEN_PACKET_SIZEOF = 12
    local ea_value_bytes            = KERNEL_OPEN_PACKET_SIZEOF + name_bytes
    -- Total EA buffer: header(8) + EaName(15) + NUL(1) + value.
    local ea_total = FILE_FULL_EA_HEADER_BYTES + AFD_EA_NAME_BYTES + 1
                   + ea_value_bytes

    local ea_buf = ffi.new('uint8_t[?]', ea_total)
    -- Header.
    local hdr = ffi.cast('FILE_FULL_EA_INFORMATION_HDR *', ea_buf)
    hdr.NextEntryOffset = 0
    hdr.Flags           = 0
    hdr.EaNameLength    = AFD_EA_NAME_BYTES
    hdr.EaValueLength   = ea_value_bytes
    -- EaName: ASCII bytes of "AfdOpenPacketXX".
    ffi.copy(ea_buf + FILE_FULL_EA_HEADER_BYTES,
             AfdOpenPacketName, AFD_EA_NAME_BYTES)
    -- NUL after EaName.
    ea_buf[FILE_FULL_EA_HEADER_BYTES + AFD_EA_NAME_BYTES] = 0
    -- AFD_OPEN_PACKET starts at offset 8 + 15 + 1 = 24.
    local pkt_off = FILE_FULL_EA_HEADER_BYTES + AFD_EA_NAME_BYTES + 1
    -- EndpointType (ULONG, little-endian).
    ffi.cast('uint32_t *', ea_buf + pkt_off)[0] = endpoint_type
    -- TransportDeviceNameLength (ULONG, in bytes).
    ffi.cast('uint32_t *', ea_buf + pkt_off + 4)[0] = name_bytes
    -- TransportDeviceName (UTF-16LE wchars).
    local wbuf = ffi.cast('uint16_t *', ea_buf + pkt_off + 8)
    for i = 1, nwch do
        wbuf[i-1] = wchars[i]
    end

    -- NtCreateFile takes an OBJECT_ATTRIBUTES naming \Device\Afd and
    -- our EA buffer.
    local noa = oa.path("\\Device\\Afd")
    local h, _disp = fs.NtCreateFile(SOCK_ACCESS, noa.oa,
                                     nil,                      -- AllocationSize
                                     fs.FILE_ATTRIBUTE_NORMAL,
                                     bit.bor(fs.FILE_SHARE_READ,
                                             fs.FILE_SHARE_WRITE),
                                     fs.FILE_OPEN_IF,          -- AFD opens fresh endpoint
                                     SOCK_OPTIONS,
                                     ea_buf,                   -- EaBuffer
                                     ea_total)                 -- EaLength
    return h
end

-- ------------------------------------------------------------------
-- bind — IOCTL_AFD_BIND with a TA_IP_ADDRESS as input.
--
-- Output buffer is intentionally NULL/0. AFD's BIND.C gates a global
-- "is this address already bound?" duplicate-check on whether an
-- output buffer was provided (BIND.C:90); if we pass one, AFD walks
-- every endpoint and rejects with STATUS_SHARING_VIOLATION when the
-- user-supplied addresses match — including the port=0 wildcard,
-- because the comparison happens BEFORE TDI picks an ephemeral port.
-- That breaks "two sockets bound to 127.0.0.1:0" patterns. Skipping
-- the check lets TDI assign distinct ephemeral ports per socket;
-- we use getsockname() if the caller wants the assigned port.
-- ------------------------------------------------------------------
local function bind(sock, host, port, timeout_secs, opts)
    local ta = make_ta_ip_address(host, port, opts and opts.dhcp)
    ioctl(sock, IOCTL_AFD_BIND,
          ta,  ffi.sizeof('TA_IP_ADDRESS'),
          nil, 0, timeout_secs)
end

-- ------------------------------------------------------------------
-- connect — IOCTL_TDI_CONNECT with a flat TDI_REQUEST_CONNECT-shaped
-- buffer where RequestConnectionInformation and RemoteAddress point
-- into the same buffer (kernel does the user→system pointer fixup).
-- ------------------------------------------------------------------
local function connect(sock, host, port, timeout_secs)
    local buf      = ffi.new('AFD_CONNECT_BUFFER')
    local buf_base = ffi.cast('uint8_t *', buf)
    -- RequestInfo holds the address pointer + length; the kernel
    -- translates these two pointers via the user-buffer-to-system-
    -- buffer offset trick (now bounds-checked via the patched
    -- AfdMapUserBufferPointer). Setting them to addresses of fields
    -- within `buf` itself satisfies that.
    buf.RequestInfo.UserDataLength      = 0
    buf.RequestInfo.UserData            = nil
    buf.RequestInfo.OptionsLength       = 0
    buf.RequestInfo.Options             = nil
    buf.RequestInfo.RemoteAddressLength = ffi.sizeof('TA_IP_ADDRESS')
    buf.RequestInfo.RemoteAddress       = ffi.cast('void *',
        buf_base + ffi.offsetof('AFD_CONNECT_BUFFER', 'RequestAddr'))
    fill_addr(buf.RequestAddr.Address, host, port)
    buf.RequestAddr.TAAddressCount = 1
    buf.RequestAddr.AddressLength  = TDI_ADDRESS_LENGTH_IP
    buf.RequestAddr.AddressType    = TDI_ADDRESS_TYPE_IP

    -- ReturnInfo: TCP uses this slot to hand back negotiated connection
    -- info. We don't care about the contents — AfdSetupConnectDataBuffers
    -- only writes here when Endpoint->ConnectDataBuffers is set
    -- (controlled by IOCTL_AFD_SET_CONNECT_DATA, which we don't issue).
    -- The slot just needs to be a valid TDI_CONNECTION_INFORMATION-sized
    -- region inside the buffer so the kernel's pointer validator accepts
    -- it. Zero-init is enough.
    buf.ReturnInfo.UserDataLength      = 0
    buf.ReturnInfo.UserData             = nil
    buf.ReturnInfo.OptionsLength        = 0
    buf.ReturnInfo.Options              = nil
    buf.ReturnInfo.RemoteAddressLength  = 0
    buf.ReturnInfo.RemoteAddress        = nil

    buf.RequestConnectionInformation = ffi.cast('TDI_CONNECTION_INFORMATION *',
        buf_base + ffi.offsetof('AFD_CONNECT_BUFFER', 'RequestInfo'))
    buf.ReturnConnectionInformation  = ffi.cast('TDI_CONNECTION_INFORMATION *',
        buf_base + ffi.offsetof('AFD_CONNECT_BUFFER', 'ReturnInfo'))
    buf.Timeout                      = 0           -- infinite
    ioctl(sock, IOCTL_TDI_CONNECT,
          buf, ffi.sizeof('AFD_CONNECT_BUFFER'),
          buf, ffi.sizeof('AFD_CONNECT_BUFFER'),
          timeout_secs)
end

-- ------------------------------------------------------------------
-- listen — IOCTL_AFD_START_LISTEN. Sets up the connection backlog;
-- pending connects queue here until accept() consumes them.
-- ------------------------------------------------------------------
local function listen(sock, backlog, timeout_secs)
    local info = ffi.new('AFD_LISTEN_INFO')
    info.MaximumConnectionQueue = backlog or 5
    ioctl(sock, IOCTL_AFD_START_LISTEN,
          info, ffi.sizeof('AFD_LISTEN_INFO'),
          nil,  0, timeout_secs)
end

-- ------------------------------------------------------------------
-- accept — three-step dance:
--   1. Allocate a fresh AFD endpoint to receive the accepted conn.
--   2. IOCTL_AFD_WAIT_FOR_LISTEN: blocks until a peer arrives,
--      returns (sequence, remote_address).
--   3. IOCTL_AFD_ACCEPT: hand the listener the new endpoint's
--      handle + the sequence; AFD attaches the pending connection.
-- ------------------------------------------------------------------
local function accept(sock, timeout_secs)
    -- Step 1: fresh stream endpoint, bound to the same transport.
    local peer = socket('tcp')

    -- Step 2: wait for a peer. Only this step is naturally blocking;
    -- the timeout applies here. Steps 1 and 3 are state-machine pokes
    -- that complete inline.
    local resp = ffi.new('AFD_LISTEN_RESPONSE_INFO')
    ioctl(sock, IOCTL_AFD_WAIT_FOR_LISTEN,
          nil,  0,
          resp, ffi.sizeof('AFD_LISTEN_RESPONSE_INFO'),
          timeout_secs)

    -- Step 3: hand off the peer endpoint with the matching sequence.
    local info = ffi.new('AFD_ACCEPT_INFO')
    info.Sequence     = resp.Sequence
    info.AcceptHandle = ffi.cast('void *', handle.raw(peer))
    ioctl(sock, IOCTL_AFD_ACCEPT,
          info, ffi.sizeof('AFD_ACCEPT_INFO'),
          nil,  0)
    return peer
end

-- ------------------------------------------------------------------
-- getsockname — IOCTL_AFD_GET_ADDRESS, returns (host, port).
-- The output is a TDI_ADDRESS_INFO (4-byte ActivityCount + the
-- TRANSPORT_ADDRESS), NOT a bare TA_IP_ADDRESS — AfdGetAddress
-- forwards the IRP's MDL to the TDI provider's TDI_QUERY_ADDRESS_INFO
-- handler, which always prefixes the activity count.
-- ------------------------------------------------------------------
local function getsockname(sock)
    local out = ffi.new('TDI_ADDRESS_INFO_IP')
    ioctl(sock, IOCTL_AFD_GET_ADDRESS,
          nil, 0,
          out, ffi.sizeof('TDI_ADDRESS_INFO_IP'))
    return read_addr(out.Address.Address)
end

-- ------------------------------------------------------------------
-- send / recv — string-friendly wrappers over the async I/O bridge.
-- The buffers are allocated inside these helpers (so they're alive
-- across the syscall) and the recv path returns a Lua string sized
-- to the actual bytes the kernel transferred (iosb.Information).
-- ------------------------------------------------------------------

local function send(sock, data, timeout_secs)
    local n = #data
    local cbuf = ffi.new('uint8_t[?]', n)
    ffi.copy(cbuf, data, n)
    return write_io(sock, cbuf, n, timeout_secs)
end

local function recv(sock, max_bytes, timeout_secs)
    local cbuf  = ffi.new('uint8_t[?]', max_bytes)
    local n, st = read_io(sock, cbuf, max_bytes, timeout_secs)
    return ffi.string(cbuf, n), st
end

-- ------------------------------------------------------------------
-- udp_sendto — IOCTL_TDI_SEND_DATAGRAM for unconnected UDP.
--
-- One contiguous AFD_SEND_DG_BUF with SendDatagramInformation and
-- the RemoteAddress pointer both fixed up to point at embedded
-- offsets within the buffer.  The kernel (SEND.C:1005-1037)
-- dereferences these user-mode pointers from kernel mode via
-- try/except — they must be valid user addresses, which our
-- LuaJIT cdata satisfies as long as we keep `buf` alive across
-- the IOCTL (the `ioctl(..., buf, ...)` line below).
--
-- Data goes through METHOD_IN_DIRECT's MDL-locked OutputBuffer
-- (sic — for SEND_DATAGRAM, "Output" is the payload).  Endpoint
-- must be bound — DHCP binds to 0.0.0.0:68 before calling this.
-- ------------------------------------------------------------------

local function udp_sendto(sock, host, port, data, timeout_secs)
    local buf      = ffi.new('AFD_SEND_DG_BUF')
    local buf_base = ffi.cast('uint8_t *', buf)

    buf.Request.Handle              = nil
    buf.Request.RequestNotifyObject = nil
    buf.Request.RequestContext      = nil
    buf.Request.TdiStatus           = 0

    buf.SendDatagramInformation = ffi.cast('TDI_CONNECTION_INFORMATION *',
        buf_base + ffi.offsetof('AFD_SEND_DG_BUF', 'ConnInfo'))

    buf.ConnInfo.UserDataLength      = 0
    buf.ConnInfo.UserData            = nil
    buf.ConnInfo.OptionsLength       = 0
    buf.ConnInfo.Options             = nil
    buf.ConnInfo.RemoteAddressLength = ffi.sizeof('TA_IP_ADDRESS')
    buf.ConnInfo.RemoteAddress       = ffi.cast('void *',
        buf_base + ffi.offsetof('AFD_SEND_DG_BUF', 'Addr'))

    buf.Addr.TAAddressCount = 1
    buf.Addr.AddressLength  = TDI_ADDRESS_LENGTH_IP
    buf.Addr.AddressType    = TDI_ADDRESS_TYPE_IP
    fill_addr(buf.Addr.Address, host, port)

    local n     = #data
    local data_cbuf = ffi.new('uint8_t[?]', n)
    ffi.copy(data_cbuf, data, n)

    return ioctl(sock, IOCTL_TDI_SEND_DATAGRAM,
                 buf,      ffi.sizeof('AFD_SEND_DG_BUF'),
                 data_cbuf, n,
                 timeout_secs)
end

-- ------------------------------------------------------------------
-- udp_recvfrom — IOCTL_TDI_RECEIVE_DATAGRAM for unconnected UDP.
-- Returns (data_string, source_host, source_port, status).
--
-- Three user-mode buffers:
--   input (64 bytes) - AFD_RECEIVE_DATAGRAM_INPUT header + dead
--                      space.  The kernel checks InputBufferLength
--                      >= AFD_FAST_RECVDG_BUFFER_LENGTH (RECVDG.C:107)
--                      so we round to 64.
--   meta             - AFD_RECV_DG_META.  Receives ReceiveLength +
--                      AddressLength + source TRANSPORT_ADDRESS at
--                      IRP completion via the IRP_INPUT_OPERATION
--                      copy from SystemBuffer (RECVDG.C:1163-1213).
--   data             - OutputBuffer; MDL-locked, gets the datagram
--                      bytes via TdiCopyBufferToMdl (RECVDG.C:1118).
--
-- input[4..7] holds a pointer to `meta`; the kernel reads it on
-- entry (RECVDG.C:137) and stashes it in Irp->UserBuffer.
-- ------------------------------------------------------------------

local function udp_recvfrom(sock, max_bytes, timeout_secs)
    local ctrl = ffi.new('AFD_RECV_DG_CTRL')
    local data = ffi.new('uint8_t[?]', max_bytes)

    -- INPUT layout in `ctrl`: ReceiveFlags + self-pointer.  After
    -- the IOCTL, the same bytes hold ReceiveLength + AddressLength
    -- and the TRANSPORT_ADDRESS tail.
    ctrl.ReceiveLength_or_Flags = TDI_RECEIVE_NORMAL
    ffi.cast('void **', ffi.cast('uint8_t *', ctrl) + 4)[0] =
        ffi.cast('void *', ctrl)

    ioctl(sock, IOCTL_TDI_RECEIVE_DATAGRAM,
          ctrl, ffi.sizeof('AFD_RECV_DG_CTRL'),
          data, max_bytes,
          timeout_secs)

    local n        = tonumber(ctrl.ReceiveLength_or_Flags)
    local data_str = ffi.string(data, n)
    local host, port = read_addr(ctrl.InnerAddr)
    return data_str, host, port
end

-- ------------------------------------------------------------------
-- AFD information ioctls — get/set per-endpoint flags + window sizes.
--
-- The kernel side reads `Information.Ulong` for both range classes
-- (windows, max send) and Boolean classes (NonBlocking, InLine);
-- the union shares its first 4 bytes with the Boolean's low byte, so
-- writing the integer 0/1 reads back as BOOLEAN cleanly.
-- AFD_INLINE_MODE on a connected TCP endpoint is the path that drops
-- into AfdSetInLineMode (MISC.C:879).
-- ------------------------------------------------------------------

local function get_info(sock, info_class, timeout_secs)
    local info = ffi.new('AFD_INFORMATION')
    info.InformationType = info_class
    ioctl(sock, IOCTL_AFD_GET_INFORMATION,
          info, ffi.sizeof('AFD_INFORMATION'),
          info, ffi.sizeof('AFD_INFORMATION'),
          timeout_secs)
    return tonumber(info.Information_Ulong)
end

local function set_info(sock, info_class, value, timeout_secs)
    local info = ffi.new('AFD_INFORMATION')
    info.InformationType    = info_class
    info.Information_Ulong  = value
    ioctl(sock, IOCTL_AFD_SET_INFORMATION,
          info, ffi.sizeof('AFD_INFORMATION'),
          nil, 0,
          timeout_secs)
end

-- ------------------------------------------------------------------
-- Public surface.
-- ------------------------------------------------------------------
return {
    socket      = socket,
    tcp         = function() return socket('tcp') end,
    udp         = function() return socket('udp') end,
    bind        = bind,
    connect     = connect,
    listen      = listen,
    accept      = accept,
    send        = send,
    recv        = recv,
    udp_sendto   = udp_sendto,
    udp_recvfrom = udp_recvfrom,
    getsockname = getsockname,
    get_info    = get_info,
    set_info    = set_info,
    -- AFD information classes — pass as info_class to get_info / set_info.
    INLINE_MODE         = AFD_INLINE_MODE,
    NONBLOCKING_MODE    = AFD_NONBLOCKING_MODE,
    MAX_SEND_SIZE       = AFD_MAX_SEND_SIZE,
    SENDS_PENDING       = AFD_SENDS_PENDING,
    MAX_PATH_SEND_SIZE  = AFD_MAX_PATH_SEND_SIZE,
    RECEIVE_WINDOW_SIZE = AFD_RECEIVE_WINDOW_SIZE,
    SEND_WINDOW_SIZE    = AFD_SEND_WINDOW_SIZE,
    -- Helpers that occasionally come in handy outside this module.
    parse_ipv4  = parse_ipv4,
    format_ipv4 = format_ipv4,
}
