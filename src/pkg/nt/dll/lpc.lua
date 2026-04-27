-- nt.dll.lpc — Local Procedure Call. Maps to NTOS/LPC on the kernel
-- side; the same IPC that CSRSS, USERSRV, and LSA run on top of.
--
-- Shape of a session:
--
--   server          client
--   ------          ------
--   NtCreatePort(&conn_port, name, max_conn_info, max_msg, max_pool)
--                         ← NtConnectPort(name, qos, [client_view], ...)
--                             → returns client_port
--   NtReplyWaitReceivePort(conn_port, nil, nil, recv)
--     recv.hdr.u2.s2.Type == LPC_CONNECTION_REQUEST
--   NtAcceptConnectPort(conn_port, ctx, recv, accept=true, ...)
--     → server_port (the per-client handle)
--   NtCompleteConnectPort(server_port)
--
--   Now a request/reply exchange:
--                         ← NtRequestWaitReplyPort(client_port, req, reply)
--   NtReplyWaitReceivePort(conn_port, &ctx, prev_reply, recv)
--     dispatch recv, fill reply
--     next loop iteration sends that reply alongside waiting for the
--     next message.
--
-- Short messages (≤ PORT_MAXIMUM_MESSAGE_LENGTH payload) go through
-- the kernel in one shot. Bigger payloads use PORT_VIEW (a section
-- mapped into both address spaces at connect time) — you send the
-- small LPC as a synchronization signal and read/write the shared
-- region directly. The FB section for the compositor fits naturally
-- here.
--
-- PORT_MESSAGE layout under pack(4) matches NT 3.5's (MSC 8.00 capped
-- natural alignments at 4, so the in-struct `double` that normally
-- forces 8-byte alignment ends up 4-aligned and the struct is 24
-- bytes flat — confirmed against the NT 3.5 header).
--
-- Message buffers:
--   lpc.new_message(payload_size) returns a fused NT_LPC_BUF cdata
--   holding the PORT_MESSAGE header followed by a payload[payload_size]
--   byte array in one allocation. Access header via msg.hdr.*, payload
--   via msg.data[i] / ffi.copy(msg.data, ...). No aliased cast pointers
--   that could outlive the backing storage.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')
local str    = require('nt.dll.str')

ffi.cdef[[
#pragma pack(push, 4)

typedef struct _PORT_MESSAGE {
    union {
        struct { short DataLength; short TotalLength; } s1;
        ULONG Length;
    } u1;
    union {
        struct { short Type; short DataInfoOffset; } s2;
        ULONG ZeroInit;
    } u2;
    union {
        CLIENT_ID ClientId;
        double    ForceAlignment;
    } u3;
    ULONG MessageId;
    ULONG ClientViewSize;
} PORT_MESSAGE;

/* Fused allocation: header + variable-length payload in one cdata.
 * Single GC anchor - no cast-and-hope-caller-keeps-anchor pattern. */
typedef struct _NT_LPC_BUF {
    PORT_MESSAGE  hdr;
    unsigned char data[?];
} NT_LPC_BUF;

typedef struct _PORT_VIEW {
    ULONG  Length;
    HANDLE SectionHandle;
    ULONG  SectionOffset;
    ULONG  ViewSize;
    PVOID  ViewBase;
    PVOID  ViewRemoteBase;
} PORT_VIEW;

typedef struct _REMOTE_PORT_VIEW {
    ULONG Length;
    ULONG ViewSize;
    PVOID ViewBase;
} REMOTE_PORT_VIEW;

typedef struct _PORT_DATA_ENTRY {
    PVOID Base;
    ULONG Size;
} PORT_DATA_ENTRY;

typedef struct _PORT_DATA_INFORMATION {
    ULONG           CountDataEntries;
    PORT_DATA_ENTRY DataEntries[1];
} PORT_DATA_INFORMATION;

typedef struct _SECURITY_QUALITY_OF_SERVICE {
    ULONG         Length;
    int           ImpersonationLevel;
    unsigned char ContextTrackingMode;
    unsigned char EffectiveOnly;
} SECURITY_QUALITY_OF_SERVICE;

#pragma pack(pop)

NTSTATUS __stdcall NtCreatePort(HANDLE *PortHandle,
                                OBJECT_ATTRIBUTES *ObjectAttributes,
                                ULONG MaxConnectionInfoLength,
                                ULONG MaxMessageLength,
                                ULONG MaxPoolUsage);

NTSTATUS __stdcall NtConnectPort(HANDLE *PortHandle,
                                 UNICODE_STRING *PortName,
                                 SECURITY_QUALITY_OF_SERVICE *SecurityQos,
                                 PORT_VIEW *ClientView,
                                 REMOTE_PORT_VIEW *ServerView,
                                 ULONG *MaxMessageLength,
                                 void *ConnectionInformation,
                                 ULONG *ConnectionInformationLength);

NTSTATUS __stdcall NtListenPort(HANDLE PortHandle,
                                PORT_MESSAGE *ConnectionRequest);

NTSTATUS __stdcall NtAcceptConnectPort(HANDLE *PortHandle,
                                       void *PortContext,
                                       PORT_MESSAGE *ConnectionRequest,
                                       unsigned char AcceptConnection,
                                       PORT_VIEW *ServerView,
                                       REMOTE_PORT_VIEW *ClientView);

NTSTATUS __stdcall NtCompleteConnectPort(HANDLE PortHandle);

NTSTATUS __stdcall NtRequestPort(HANDLE PortHandle,
                                 PORT_MESSAGE *RequestMessage);

NTSTATUS __stdcall NtRequestWaitReplyPort(HANDLE PortHandle,
                                          PORT_MESSAGE *RequestMessage,
                                          PORT_MESSAGE *ReplyMessage);

NTSTATUS __stdcall NtReplyPort(HANDLE PortHandle,
                               PORT_MESSAGE *ReplyMessage);

NTSTATUS __stdcall NtReplyWaitReplyPort(HANDLE PortHandle,
                                        PORT_MESSAGE *ReplyMessage);

NTSTATUS __stdcall NtReplyWaitReceivePort(HANDLE PortHandle,
                                          void **PortContext,
                                          PORT_MESSAGE *ReplyMessage,
                                          PORT_MESSAGE *ReceiveMessage);

NTSTATUS __stdcall NtImpersonateClientOfPort(HANDLE PortHandle,
                                             PORT_MESSAGE *Message);

NTSTATUS __stdcall NtReadRequestData(HANDLE PortHandle,
                                     PORT_MESSAGE *Message,
                                     ULONG DataEntryIndex,
                                     void *Buffer,
                                     ULONG BufferSize,
                                     ULONG *NumberOfBytesRead);

NTSTATUS __stdcall NtWriteRequestData(HANDLE PortHandle,
                                      PORT_MESSAGE *Message,
                                      ULONG DataEntryIndex,
                                      void *Buffer,
                                      ULONG BufferSize,
                                      ULONG *NumberOfBytesWritten);

NTSTATUS __stdcall NtQueryInformationPort(HANDLE PortHandle,
                                          int PortInformationClass,
                                          void *PortInformation,
                                          ULONG Length,
                                          ULONG *ReturnLength);
]]

-- PORT_MESSAGE.Type values. LPC_CONNECTION_REQUEST arrives via
-- NtReplyWaitReceivePort when a client is trying to connect.
local M = {
    LPC_REQUEST            = 1,
    LPC_REPLY              = 2,
    LPC_DATAGRAM           = 3,
    LPC_LOST_REPLY         = 4,
    LPC_PORT_CLOSED        = 5,
    LPC_CLIENT_DIED        = 6,
    LPC_EXCEPTION          = 7,
    LPC_DEBUG_EVENT        = 8,
    LPC_ERROR_EVENT        = 9,
    LPC_CONNECTION_REQUEST = 10,

    PORT_MAXIMUM_MESSAGE_LENGTH = 256,
    PORT_HEADER_SIZE            = ffi.sizeof('PORT_MESSAGE'),
}

-- SecurityImpersonationLevel values for SECURITY_QUALITY_OF_SERVICE.
M.SecurityAnonymous      = 0
M.SecurityIdentification = 1
M.SecurityImpersonation  = 2
M.SecurityDelegation     = 3

-- Cast helper used inside syscall wrappers. msg is the caller's
-- NT_LPC_BUF cdata (or a PORT_MESSAGE * — both work). The cast is
-- scoped to the call expression; the source cdata remains anchored
-- as the wrapper's argument.
local function hdr_ptr(msg)
    return msg and ffi.cast('PORT_MESSAGE *', msg) or nil
end

-- ------------------------------------------------------------------
-- Message-buffer helpers
-- ------------------------------------------------------------------

-- Allocate a fused NT_LPC_BUF cdata large enough to hold a
-- PORT_MESSAGE header plus payload_size bytes of variable-length data.
-- Caller fills in the payload at msg.data[...], and stamps the header
-- via init_message or by setting msg.hdr.* directly.
--
-- Max usable payload for a short LPC is PORT_MAXIMUM_MESSAGE_LENGTH
-- (256 on NT 3.5). Larger payloads must go through a PORT_VIEW
-- (shared section) attached at connect time.
function M.new_message(payload_size)
    payload_size = payload_size or M.PORT_MAXIMUM_MESSAGE_LENGTH
    return ffi.new('NT_LPC_BUF', payload_size)
end

-- Stamp a request/reply header with its content-size fields. Pass the
-- payload length (bytes beyond the fixed header); this module fills in
-- DataLength and TotalLength and clears the padding / Type fields.
function M.init_message(msg, payload_size, type_code)
    msg.hdr.u1.s1.DataLength  = payload_size
    msg.hdr.u1.s1.TotalLength = M.PORT_HEADER_SIZE + payload_size
    msg.hdr.u2.ZeroInit       = 0
    msg.hdr.u2.s2.Type        = type_code or M.LPC_REQUEST
    msg.hdr.MessageId         = 0
    msg.hdr.ClientViewSize    = 0
end

-- Default SECURITY_QUALITY_OF_SERVICE for a typical LPC client connect.
-- SecurityImpersonation lets the server NtImpersonateClientOfPort to
-- check access as the client; ContextTrackingMode=0 means static (the
-- token snapshotted at connect time, not dynamic). EffectiveOnly=0
-- lets the server use the full token. Caller owns the cdata and must
-- keep it alive across the NtConnectPort call.
function M.default_qos()
    local qos = ffi.new('SECURITY_QUALITY_OF_SERVICE')
    qos.Length              = ffi.sizeof('SECURITY_QUALITY_OF_SERVICE')
    qos.ImpersonationLevel  = M.SecurityImpersonation
    qos.ContextTrackingMode = 0
    qos.EffectiveOnly       = 0
    return qos
end

-- ------------------------------------------------------------------
-- Syscall wrappers
-- ------------------------------------------------------------------

-- Server: create a named connection port clients can NtConnectPort to.
-- max_conn_info bounds the size of connection-info blobs clients pass
-- during connect; max_msg bounds ordinary messages; max_pool caps the
-- kernel pool this port may hold for queued messages.
function M.NtCreatePort(oa, max_conn_info, max_msg, max_pool)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreatePort(h, oa, max_conn_info or 0,
                                  max_msg or M.PORT_MAXIMUM_MESSAGE_LENGTH,
                                  max_pool or 0)
    if err.is_error(st) then err.raise('NtCreatePort', st) end
    return handle.wrap(h[0])
end

-- Client: connect to a server's named port. `name` is a UTF-8 Lua
-- string (e.g. "\\MicroGFX"); the NT_STRING is built internally and
-- held alive across the call so the caller can't accidentally drop
-- the UNICODE_STRING.Buffer mid-flight.
--
-- `qos` may be nil — in that case the wrapper fabricates a default
-- SECURITY_QUALITY_OF_SERVICE (Impersonation level, static tracking)
-- as a local and holds it alive for the syscall. Pass a cdata you
-- built via lpc.default_qos() / ffi.new if you want to customize.
-- `client_view` is a PORT_VIEW cdata to share a section with the
-- server (FB compositor section, etc.), or nil.
--
-- Returns (port_handle, max_message_length_the_server_agreed_to).
function M.NtConnectPort(name, qos, client_view, server_view,
                         connection_info, connection_info_length)
    local ns      = str.to_utf16(name)
    local local_qos = qos or M.default_qos()
    local h       = ffi.new('HANDLE[1]')
    local max_msg = ffi.new('ULONG[1]')
    local ci_len  = ffi.new('ULONG[1]')
    if connection_info_length then ci_len[0] = connection_info_length end
    local st = ntdll.NtConnectPort(h, ns.us, local_qos,
                                   client_view, server_view,
                                   max_msg,
                                   connection_info,
                                   connection_info and ci_len or nil)
    if err.is_error(st) then err.raise('NtConnectPort', st) end
    return handle.wrap(h[0]), max_msg[0]
end

-- Server: accept an incoming connection request (previously received
-- via NtReplyWaitReceivePort). `accept` is a boolean. Returns the
-- per-client server-side port handle (nil if we rejected).
--
-- PortContext is an opaque value the kernel stores against the port
-- object and hands back later via NtReplyWaitReceivePort's
-- ctx_out. The kernel never dereferences it — it's just bits — but
-- if you pass a Lua-allocated cdata pointer, keep that cdata alive
-- on the Lua side for the port's whole lifetime. Otherwise later
-- retrievals will read through a dangling Lua-GC'd pointer.
--
-- After accept, call NtCompleteConnectPort on the returned handle to
-- wake the blocked client.
function M.NtAcceptConnectPort(conn_port, context, req_msg, accept,
                               server_view, client_view)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtAcceptConnectPort(h, context, hdr_ptr(req_msg),
                                         accept and 1 or 0,
                                         server_view, client_view)
    if err.is_error(st) then err.raise('NtAcceptConnectPort', st) end
    if not accept then return nil end
    return handle.wrap(h[0])
end

function M.NtCompleteConnectPort(port)
    local st = ntdll.NtCompleteConnectPort(handle.raw(port))
    if err.is_error(st) then err.raise('NtCompleteConnectPort', st) end
end

-- Server: wait for a connection request on a connection port. Less
-- common than the NtReplyWaitReceivePort loop; included for symmetry.
function M.NtListenPort(port, conn_req_msg)
    local st = ntdll.NtListenPort(handle.raw(port), hdr_ptr(conn_req_msg))
    if err.is_error(st) then err.raise('NtListenPort', st) end
end

-- Client: fire-and-forget one-way message (Type = LPC_DATAGRAM).
function M.NtRequestPort(port, req)
    local st = ntdll.NtRequestPort(handle.raw(port), hdr_ptr(req))
    if err.is_error(st) then err.raise('NtRequestPort', st) end
end

-- Client: synchronous request + wait for reply. `reply` is caller-
-- allocated; kernel fills it in. Most LPC calls clients make go
-- through here — one syscall, one round-trip.
function M.NtRequestWaitReplyPort(port, req, reply)
    local st = ntdll.NtRequestWaitReplyPort(handle.raw(port),
                                            hdr_ptr(req), hdr_ptr(reply))
    if err.is_error(st) then err.raise('NtRequestWaitReplyPort', st) end
end

-- Server: unsolicited reply / datagram back to a specific client.
function M.NtReplyPort(port, reply)
    local st = ntdll.NtReplyPort(handle.raw(port), hdr_ptr(reply))
    if err.is_error(st) then err.raise('NtReplyPort', st) end
end

function M.NtReplyWaitReplyPort(port, reply)
    local st = ntdll.NtReplyWaitReplyPort(handle.raw(port), hdr_ptr(reply))
    if err.is_error(st) then err.raise('NtReplyWaitReplyPort', st) end
end

-- Server main-loop call: send optional reply to previous request, then
-- wait for the next message (connection request OR normal message) on
-- this port. ctx_out is an out pointer that receives the per-client
-- PortContext we set via NtAcceptConnectPort; pass nil if not tracked.
function M.NtReplyWaitReceivePort(port, ctx_out, reply, recv)
    local st = ntdll.NtReplyWaitReceivePort(handle.raw(port),
                                            ctx_out,
                                            hdr_ptr(reply),
                                            hdr_ptr(recv))
    if err.is_error(st) then err.raise('NtReplyWaitReceivePort', st) end
end

function M.NtImpersonateClientOfPort(port, msg)
    local st = ntdll.NtImpersonateClientOfPort(handle.raw(port), hdr_ptr(msg))
    if err.is_error(st) then err.raise('NtImpersonateClientOfPort', st) end
end

-- Scatter/gather data fetch/stash (for PORT_DATA_INFORMATION-carrying
-- messages, where the client attached separate data buffers pointed to
-- by the message's DataInfoOffset).
function M.NtReadRequestData(port, msg, index, buf, buf_size)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtReadRequestData(handle.raw(port), hdr_ptr(msg),
                                       index, buf, buf_size, ret)
    if err.is_error(st) then err.raise('NtReadRequestData', st) end
    return ret[0]
end

function M.NtWriteRequestData(port, msg, index, buf, buf_size)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtWriteRequestData(handle.raw(port), hdr_ptr(msg),
                                        index, buf, buf_size, ret)
    if err.is_error(st) then err.raise('NtWriteRequestData', st) end
    return ret[0]
end

-- Caller provides buffer + length, receives (bytes_written, status).
-- Info class 0 = PortBasicInformation; NT 3.5 also has
-- PortDumpInformation (=1) when the kernel was built with DEVL.
function M.NtQueryInformationPort(port, info_class, buf, length)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryInformationPort(handle.raw(port), info_class,
                                            buf, length, ret)
    if err.is_error(st) then err.raise('NtQueryInformationPort', st) end
    return ret[0], err.normalize(st)
end

return M
