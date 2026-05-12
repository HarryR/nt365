-- nt.net.info — TDI info-query interface for the TCP/IP stack.
--
-- Direct access to the kernel's MIB-II surface via \Device\Tcp.
-- Wraps IOCTL_TCP_QUERY_INFORMATION_EX (all reads) and
-- IOCTL_TCP_SET_INFORMATION_EX (writable knobs) with typed Lua
-- helpers.  Entity-instance discovery is internal; callers ask for
-- stats by name (ip_stats, icmp_stats) and don't see the TDI
-- entity ID vocabulary.
--
--
-- Surface:
--
--   open()                Open \Device\Tcp.  Returns NT_HANDLE; close
--                         with h:close() or let __gc handle it.
--
--   ip_stats(h)           IPSNMPInfo cdata.  Fields per IPINFO.H —
--                         counters (ipsi_inreceives, ipsi_forwdatagrams,
--                         ...) plus the writable ipsi_forwarding /
--                         ipsi_defaultttl knobs.  Raises on failure.
--
--   icmp_stats(h)         ICMPSNMPInfo cdata.  In+out ICMP stats per
--                         IPINFO.H — icsi_instats / icsi_outstats
--                         each an ICMPStats with icmps_redirects /
--                         icmps_errors etc.  Raises on failure.
--
--   set_ip_stats(h, info) Apply mutable IPSNMPInfo fields.  Only
--                         ipsi_defaultttl and ipsi_forwarding are
--                         honoured by the kernel; the rest are
--                         counter fields and ignored.  Returns raw
--                         NTSTATUS so the caller can distinguish
--                         refusal (TDI_INVALID_PARAMETER, used by
--                         the H-020 strip to reject IP_FORWARDING)
--                         from acceptance (STATUS_SUCCESS).  Does
--                         NOT raise — refusal is observable state.
--
--   entities(h)           Escape hatch.  Returns the full entity
--                         list as { {entity = N, instance = M}, ... }
--                         so the caller can see what MIB consumers
--                         the kernel exposes (CL_NL_ENTITY for IP,
--                         ER_ENTITY for ICMP, AT_ENTITY for ARP,
--                         IF_ENTITY for per-NIC interface, CO_TL_
--                         and CL_TL_ for TCP/UDP).  Useful for
--                         diagnostic dumps and for writers of new
--                         MIB query wrappers.
--
--   addresses(h)          Array of IPAddrEntry — one per configured
--                         IP (most importantly the iae_context
--                         field, which userland DHCP needs to feed
--                         into IOCTL_IP_SET_ADDRESS).
--
--   routes(h)             Array of IPRouteEntry — the IP route
--                         table.  Snapshot for the "no network
--                         input mutates routes" invariant; also
--                         used by DHCP to verify the default route
--                         landed after configuration push.
--
--   arp(h)                Array of IPNetToMediaEntry — the ARP
--                         cache.  Used by DHCP to verify gateway
--                         resolution.  Multi-interface caveat: we
--                         pick the first AT_ENTITY instance, which
--                         on a single-NIC guest is correct.
--
--   interfaces(h)         Array of IFEntry — one per NIC.  Exposes
--                         if_physaddr (MAC), if_mtu, if_type
--                         (ETHERNET / LOOPBACK).  DHCP uses this
--                         to find the chaddr it needs to put in
--                         the DHCP packet.
--
--   add_route(h, e)       Install a route.  `e` is a Lua table:
--   del_route(h, e)         { dest, mask, nexthop, if_index,
--                             metric=1, type=IRE_TYPE_INDIRECT,
--                             proto=IRE_PROTO_NETMGMT }.  All IPv4
--                         numerics are network-order ULONGs (use
--                         the same encoding the routes(h) walker
--                         produces).  del_route forces ire_type =
--                         IRE_TYPE_INVALID — the kernel's wire
--                         convention for delete on this MIB
--                         (IP/INFO.C:478-487).  Returns raw NTSTATUS
--                         so callers can observe TDI_INVALID_PARAMETER
--                         refusal vs STATUS_SUCCESS.
--
--   add_arp(h, e)         Install / drop an ARP entry.  `e` is a
--   del_arp(h, e)           { addr, if_index, mac, type=DYNAMIC }.
--                         `mac` is a 6-byte string (Ethernet).
--                         del_arp forces inme_type = INME_TYPE_INVALID.
--                         Returns raw NTSTATUS.  Not used by DHCP
--                         but exposed for static-ARP / neighbour-
--                         cache test paths.
--
--   open_ip()             Open \Device\Ip.  Returns NT_HANDLE.
--                         Separate from open() because the address-
--                         set IOCTL lives on the IP device, not
--                         TCP.  Used by set_address only.
--
--   set_address(hi, c,    Push an IP address + subnet mask onto an
--               a, m)     NTE.  hi is the \Device\Ip handle from
--                         open_ip(); c is the NTE context from
--                         addresses(h)[i].iae_context; a + m are
--                         network-order IPv4 ULONGs.  Returns raw
--                         NTSTATUS.  DHCP calls this after ACK.
--
--
-- Cursor-walk kernel quirk: the routes / addresses / arp queries
-- use the TDI "cursor" pagination protocol — caller hands a 16-byte
-- Context, kernel returns one batch and updates the cursor for the
-- next call.  This build has a bug in TCPQueryInformationEx
-- (NTDISP.C:2311): the wrapper guards the Context copy-back with
-- NT_SUCCESS(status), which excludes STATUS_BUFFER_OVERFLOW because
-- WARNING-severity codes (top bit set) are negative.  So the cursor
-- updates the kernel's stack-local copy but never propagates to
-- user space, and multi-batch iteration is impossible.  We work
-- around it by oversizing our buffer to fit the whole table in one
-- call; if we ever overflow, we raise rather than loop on a
-- stuck cursor.  Tables we walk are bounded small on this stack
-- (route table single-digits, address table 1-2, ARP cache
-- typically <50 on a single-NIC guest) so the oversize approach
-- is enough.
--
--
-- Error-handling asymmetry: query functions raise because the caller
-- has no useful action on failure other than reporting it.  Mutator
-- returns the status because tests want to assert *which* refusal
-- code the kernel chose, and wrapping that in pcall would lose it.
--
-- TDI_INVALID_PARAMETER aliases to STATUS_INVALID_PARAMETER on the
-- NT build (TDISTAT.H:78) — there is no separate TDI status
-- namespace at runtime.  We expose the value under the TDI_ name
-- because callers reading IP/INFO.C see `return TDI_INVALID_PARAMETER`
-- and that's the symbol they want to assert on.
--
-- Synchronous I/O: \Device\Tcp is opened with FILE_SYNCHRONOUS_IO_NONALERT
-- so NtDeviceIoControlFile blocks until IRP completion before returning.
-- IOCTL_TCP_QUERY_INFORMATION_EX uses METHOD_NEITHER (kernel sees raw
-- user pointers); IOCTL_TCP_SET_INFORMATION_EX uses METHOD_BUFFERED.
-- Either way our ffi.new() buffers are pinned for the whole call,
-- and the kernel does its own probe.

local ffi    = require('ffi')
local bit    = require('bit')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local fs     = require('nt.dll.fs')
local handle = require('nt.dll.handle')
local oa     = require('nt.dll.oa')

local M = {}

-- ------------------------------------------------------------------
-- Type defs (TDIINFO.H + IPINFO.H).  pack(1) — these structs cross
-- the user/kernel boundary and the kernel side uses the wire layout.
-- ------------------------------------------------------------------

ffi.cdef[[
#pragma pack(push, 1)

typedef struct _TDIEntityID {
    unsigned long tei_entity;
    unsigned long tei_instance;
} TDIEntityID;

typedef struct _TDIObjectID {
    TDIEntityID   toi_entity;
    unsigned long toi_class;
    unsigned long toi_type;
    unsigned long toi_id;
} TDIObjectID;

typedef struct _TCP_REQUEST_QUERY_INFORMATION_EX {
    TDIObjectID   ID;
    unsigned char Context[16];
} TCP_REQUEST_QUERY_INFORMATION_EX;

typedef struct _IPSNMPInfo {
    unsigned long ipsi_forwarding;
    unsigned long ipsi_defaultttl;
    unsigned long ipsi_inreceives;
    unsigned long ipsi_inhdrerrors;
    unsigned long ipsi_inaddrerrors;
    unsigned long ipsi_forwdatagrams;
    unsigned long ipsi_inunknownprotos;
    unsigned long ipsi_indiscards;
    unsigned long ipsi_indelivers;
    unsigned long ipsi_outrequests;
    unsigned long ipsi_routingdiscards;
    unsigned long ipsi_outdiscards;
    unsigned long ipsi_outnoroutes;
    unsigned long ipsi_reasmtimeout;
    unsigned long ipsi_reasmreqds;
    unsigned long ipsi_reasmoks;
    unsigned long ipsi_reasmfails;
    unsigned long ipsi_fragoks;
    unsigned long ipsi_fragfails;
    unsigned long ipsi_fragcreates;
    unsigned long ipsi_numif;
    unsigned long ipsi_numaddr;
    unsigned long ipsi_numroutes;
} IPSNMPInfo;

typedef struct _ICMPStats {
    unsigned long icmps_msgs;
    unsigned long icmps_errors;
    unsigned long icmps_destunreachs;
    unsigned long icmps_timeexcds;
    unsigned long icmps_parmprobs;
    unsigned long icmps_srcquenchs;
    unsigned long icmps_redirects;
    unsigned long icmps_echos;
    unsigned long icmps_echoreps;
    unsigned long icmps_timestamps;
    unsigned long icmps_timestampreps;
    unsigned long icmps_addrmasks;
    unsigned long icmps_addrmaskreps;
} ICMPStats;

typedef struct _ICMPSNMPInfo {
    ICMPStats icsi_instats;
    ICMPStats icsi_outstats;
} ICMPSNMPInfo;

/* UDPStats — CL_TL_ENTITY / UDP_MIB_STAT_ID.  TCPINFO.H:52. */
typedef struct _UDPStats {
    unsigned long us_indatagrams;
    unsigned long us_noports;       /* received UDP with no listener */
    unsigned long us_inerrors;      /* delivery errors (AO invalid, etc) */
    unsigned long us_outdatagrams;
    unsigned long us_numaddrs;
} UDPStats;

typedef struct _IPAddrEntry {
    unsigned long  iae_addr;       /* address itself, network order */
    unsigned long  iae_index;      /* interface index (if_index)    */
    unsigned long  iae_mask;       /* subnet mask, network order    */
    unsigned long  iae_bcastaddr;  /* low bit of broadcast addr     */
    unsigned long  iae_reasmsize;  /* 0 on this stack (no reasm)    */
    unsigned short iae_context;    /* NTE context — feed into       */
                                   /* IOCTL_IP_SET_ADDRESS          */
    unsigned short iae_pad;
} IPAddrEntry;

typedef struct _IPRouteEntry {
    unsigned long ire_dest;        /* destination, network order    */
    unsigned long ire_index;       /* outbound interface index      */
    unsigned long ire_metric1;     /* primary metric                */
    unsigned long ire_metric2;
    unsigned long ire_metric3;
    unsigned long ire_metric4;
    unsigned long ire_nexthop;     /* gateway IP                    */
    unsigned long ire_type;        /* IRE_TYPE_DIRECT / _INDIRECT / */
                                   /* _OTHER / _INVALID             */
    unsigned long ire_proto;       /* IRE_PROTO_LOCAL / _NETMGMT /  */
                                   /* _ICMP (redirect-learned —    */
                                   /* should never appear post-     */
                                   /* H-009) / etc                  */
    unsigned long ire_age;
    unsigned long ire_mask;        /* destination mask              */
    unsigned long ire_metric5;
} IPRouteEntry;

/* Per-interface IFEntry — IF_ENTITY / IF_MIB_STATS_ID.  Variable-
 * tail layout: fixed fields up to if_descrlen, then `if_descr[1]`
 * placeholder.  The kernel respects BufferSize and skips the descr
 * tail when there's only room for the fixed part (ARP.C:3504-3559),
 * so sizeof(IFEntry) (= IFE_FIXED_SIZE + 1) is enough.  DHCP only
 * cares about if_physaddrlen + if_physaddr + if_index. */
typedef struct _IFEntry {
    unsigned long  if_index;
    unsigned long  if_type;          /* IF_TYPE_ETHERNET = 6 */
    unsigned long  if_mtu;
    unsigned long  if_speed;
    unsigned long  if_physaddrlen;
    unsigned char  if_physaddr[8];   /* MAX_PHYSADDR_SIZE */
    unsigned long  if_adminstatus;
    unsigned long  if_operstatus;
    unsigned long  if_lastchange;
    unsigned long  if_inoctets;
    unsigned long  if_inucastpkts;
    unsigned long  if_innucastpkts;
    unsigned long  if_indiscards;
    unsigned long  if_inerrors;
    unsigned long  if_inunknownprotos;
    unsigned long  if_outoctets;
    unsigned long  if_outucastpkts;
    unsigned long  if_outnucastpkts;
    unsigned long  if_outdiscards;
    unsigned long  if_outerrors;
    unsigned long  if_outqlen;
    unsigned long  if_descrlen;
    unsigned char  if_descr[1];
} IFEntry;

/* ARP entry — note MAX_PHYSADDR_SIZE = 8 in LLINFO.H to accommodate
 * the wider hardware addresses (e.g. FDDI 6-byte, Token Ring 6-byte
 * with potential extensions).  Ethernet uses only the first 6 bytes. */
typedef struct _IPNetToMediaEntry {
    unsigned long inme_index;
    unsigned long inme_physaddrlen;
    unsigned char inme_physaddr[8];
    unsigned long inme_addr;       /* IP, network order */
    unsigned long inme_type;       /* INME_TYPE_DYNAMIC / _STATIC / */
                                   /* _INVALID / _OTHER */
} IPNetToMediaEntry;

/* SET_INFORMATION_EX with an IPSNMPInfo payload baked in.  The
 * kernel struct uses Buffer[1] with the real payload immediately
 * following; we embed the whole IPSNMPInfo so the cdata is one
 * contiguous allocation. */
typedef struct _TCP_REQUEST_SET_INFORMATION_EX_IPSNMP {
    TDIObjectID   ID;
    unsigned int  BufferSize;
    IPSNMPInfo    Buffer;
} TCP_REQUEST_SET_INFORMATION_EX_IPSNMP;

typedef struct _TCP_REQUEST_SET_INFORMATION_EX_IPROUTE {
    TDIObjectID   ID;
    unsigned int  BufferSize;
    IPRouteEntry  Buffer;
} TCP_REQUEST_SET_INFORMATION_EX_IPROUTE;

typedef struct _TCP_REQUEST_SET_INFORMATION_EX_ARP {
    TDIObjectID         ID;
    unsigned int        BufferSize;
    IPNetToMediaEntry   Buffer;
} TCP_REQUEST_SET_INFORMATION_EX_ARP;

/* IOCTL_IP_SET_ADDRESS payload — see NTDDIP.H:51-55.  Same wire
 * format the stock NT 3.5 DHCP client uses. */
typedef struct _IP_SET_ADDRESS_REQUEST {
    unsigned short    Context;
    unsigned long     Address;
    unsigned long     SubnetMask;
} IP_SET_ADDRESS_REQUEST;

#pragma pack(pop)
]]

-- ------------------------------------------------------------------
-- Internal constants (TDIINFO.H + IPINFO.H).  Hidden from callers
-- because the surface above is keyed on names, not entity IDs.
-- ------------------------------------------------------------------

local GENERIC_ENTITY        = 0
local IF_ENTITY             = 0x200   -- per-NIC interface (NDIS adapter)
local AT_ENTITY             = 0x280   -- ARP / address translation
local CL_NL_ENTITY          = 0x301   -- IP
local ER_ENTITY             = 0x380   -- ICMP
local CL_TL_ENTITY          = 0x401   -- UDP (connectionless transport)

local INFO_CLASS_GENERIC    = 0x100
local INFO_CLASS_PROTOCOL   = 0x200
local INFO_TYPE_PROVIDER    = 0x100

local ENTITY_LIST_ID        = 0
local IF_MIB_STATS_ID       = 1
local IP_MIB_STATS_ID       = 1
local IP_MIB_RTTABLE_ENTRY_ID   = 0x101
local IP_MIB_ADDRTABLE_ENTRY_ID = 0x102
local ICMP_MIB_STATS_ID         = 1
local AT_MIB_ADDRXLAT_ENTRY_ID  = 0x101
local UDP_MIB_STAT_ID           = 1

local STATUS_BUFFER_OVERFLOW = 0x80000005

-- IOCTL codes — CTL_CODE(FILE_DEVICE_NETWORK=0x12, fn, method, access).
-- QUERY = (0x12<<16) | (0<<14) | (0<<2) | METHOD_NEITHER(3)             = 0x00120003
-- SET   = (0x12<<16) | (FILE_WRITE_ACCESS=2 <<14) | (1<<2) | BUFFERED(0) = 0x00128004
local IOCTL_TCP_QUERY_INFORMATION_EX = 0x00120003
local IOCTL_TCP_SET_INFORMATION_EX   = 0x00128004

-- \Device\Ip IOCTL — function 1 on FSCTL_IP_BASE (NTDDIP.H:95-96).
-- (0x12<<16) | (FILE_WRITE_ACCESS=2 <<14) | (1<<2) | METHOD_BUFFERED(0) = 0x00128004
-- Coincidentally equal to IOCTL_TCP_SET_INFORMATION_EX in numeric
-- value — different device, different handler.
local IOCTL_IP_SET_ADDRESS = 0x00128004

-- ------------------------------------------------------------------
-- Public constants — values callers write into IPSNMPInfo, and the
-- two NTSTATUS codes the H-020 refusal test asserts against.
-- ------------------------------------------------------------------

M.IP_FORWARDING         = 1
M.IP_NOT_FORWARDING     = 2

M.STATUS_SUCCESS        = 0x00000000
M.TDI_INVALID_PARAMETER = 0xC000000D   -- == STATUS_INVALID_PARAMETER

-- IPRouteEntry.ire_type — direct = on-link, indirect = via gateway,
-- invalid = administratively-down placeholder.
M.IRE_TYPE_OTHER    = 1
M.IRE_TYPE_INVALID  = 2
M.IRE_TYPE_DIRECT   = 3
M.IRE_TYPE_INDIRECT = 4

-- IPRouteEntry.ire_proto — how this route was learned.  IRE_PROTO_ICMP
-- means a redirect installed it; on a post-H-009 build that
-- bucket should always be empty (redirect reception is gone).
M.IRE_PROTO_OTHER   = 1
M.IRE_PROTO_LOCAL   = 2
M.IRE_PROTO_NETMGMT = 3
M.IRE_PROTO_ICMP    = 4

-- IPNetToMediaEntry.inme_type — ARP entry provenance.
M.INME_TYPE_OTHER   = 1
M.INME_TYPE_INVALID = 2
M.INME_TYPE_DYNAMIC = 3
M.INME_TYPE_STATIC  = 4

-- IFEntry.if_type — interface link-layer family.  DHCP wants
-- ETHERNET (the loopback IF entry has type IF_TYPE_LOOPBACK == 24
-- per RFC 1213 but in this tree the loopback NTE has its own
-- entity and is filtered out by addresses() callers anyway).
M.IF_TYPE_OTHER     = 1
M.IF_TYPE_ETHERNET  = 6
M.IF_TYPE_TOKENRING = 9
M.IF_TYPE_FDDI      = 15
M.IF_TYPE_LOOPBACK  = 24

-- ------------------------------------------------------------------
-- IOCTL primitive — synchronous, returns (Information as Lua number,
-- normalised NTSTATUS).
--
-- Status source: we use the syscall return value `st`, not iosb.Status.
-- For our FILE_SYNCHRONOUS_IO_NONALERT handle there are two failure
-- modes:
--   1. Pre-IRP rejection (bad handle, bad IOCTL code, access denied
--      at the I/O manager).  `st` carries the error; iosb is untouched
--      (ffi.new zeroed it, which would look like SUCCESS).
--   2. IRP issued + completed.  `st == iosb.Status` because the
--      driver dispatch sets both to the same value and the I/O
--      manager returns the IRP's final status verbatim for
--      synchronous handles.
-- So `st` is a strict superset.  Reading iosb.Status alone would
-- swallow case 1.
--
-- tonumber(iosb.Information): `Information` is ULONG in this tree
-- (init.lua:43), so cdata field access extracts a Lua number by
-- value.  Explicit tonumber documents intent and would raise
-- immediately if a future retype made Information a struct.
-- ------------------------------------------------------------------

local function ioctl(h, code, in_buf, in_len, out_buf, out_len)
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtDeviceIoControlFile(handle.raw(h),
                                           nil, nil, nil,
                                           iosb, code,
                                           in_buf,  in_len  or 0,
                                           out_buf, out_len or 0)
    return tonumber(iosb.Information), err.normalize(st)
end

-- ------------------------------------------------------------------
-- Entity discovery.  The kernel assigns entity instance numbers
-- dynamically at init time (INFO.C:560); the entity list is the
-- discovery channel.  IOCTL is microseconds and the result is small
-- (~4 entries on this build) so we don't cache between calls — each
-- find_instance() / entities() re-issues the query.
-- ------------------------------------------------------------------

local MAX_ENTITIES = 16

-- Internal: issue the ENTITY_LIST query, return (cdata array, count).
-- Caller scans / materialises as appropriate.  The cdata array's
-- lifetime is bounded by the caller's reference to it.
local function _entity_list(h)
    local req = ffi.new('TCP_REQUEST_QUERY_INFORMATION_EX')
    req.ID.toi_entity.tei_entity   = GENERIC_ENTITY
    req.ID.toi_entity.tei_instance = 0
    req.ID.toi_class               = INFO_CLASS_GENERIC
    req.ID.toi_type                = INFO_TYPE_PROVIDER
    req.ID.toi_id                  = ENTITY_LIST_ID

    local out = ffi.new('TDIEntityID[?]', MAX_ENTITIES)
    local entity_sz = ffi.sizeof('TDIEntityID')

    local got, st = ioctl(h, IOCTL_TCP_QUERY_INFORMATION_EX,
                          req, ffi.sizeof(req),
                          out, entity_sz * MAX_ENTITIES)
    if st ~= 0 then
        err.raise('nt.net.info._entity_list', st)
    end

    -- Clamp to MAX_ENTITIES so a kernel that returns an oversized
    -- Information count can't make us read out of bounds.
    local n = math.floor(got / entity_sz)
    if n > MAX_ENTITIES then n = MAX_ENTITIES end
    return out, n
end

-- Internal: scan the entity list for `entity_id`, return its
-- tei_instance.  Errors if the entity isn't present.
local function find_instance(h, entity_id)
    local out, n = _entity_list(h)
    for i = 0, n - 1 do
        if out[i].tei_entity == entity_id then
            return tonumber(out[i].tei_instance)
        end
    end
    error(string.format(
        "nt.net.info: entity 0x%X not in entity list (n=%d)",
        entity_id, n))
end

-- ------------------------------------------------------------------
-- Generic single-shot stats query.  Internal — used by ip_stats and
-- icmp_stats.  Looks up the entity instance, issues QUERY_EX with
-- (entity, mib_id), allocates an output cdata of the named ctype,
-- and returns it.
-- ------------------------------------------------------------------

local function query_stats(h, entity_id, mib_id, ctype, fn_name)
    local instance = find_instance(h, entity_id)
    local req = ffi.new('TCP_REQUEST_QUERY_INFORMATION_EX')
    req.ID.toi_entity.tei_entity   = entity_id
    req.ID.toi_entity.tei_instance = instance
    req.ID.toi_class               = INFO_CLASS_PROTOCOL
    req.ID.toi_type                = INFO_TYPE_PROVIDER
    req.ID.toi_id                  = mib_id

    local out = ffi.new(ctype)

    local _got, st = ioctl(h, IOCTL_TCP_QUERY_INFORMATION_EX,
                           req, ffi.sizeof(req),
                           out, ffi.sizeof(out))
    if st ~= 0 then
        err.raise(fn_name, st)
    end
    return out
end

-- ------------------------------------------------------------------
-- Cursor-walk primitive (internal).  Issues a single QUERY_EX call
-- with an oversized output buffer that fits the full table in one
-- shot — see the kernel-quirk note at the top of this module for
-- why we don't iterate.  Materialises each entry by-value into a
-- Lua table so the caller can hold the result independent of the
-- batch buffer.
-- ------------------------------------------------------------------

-- BATCH_ENTRIES — sized so any realistic single-NIC config fits in
-- one IOCTL.  Bumping it costs (BATCH_ENTRIES * entry_size) bytes
-- of transient allocation per walk; cheap.
local BATCH_ENTRIES = 128

local function walk_table(h, entity_id, mib_id, ctype, fn_name)
    local instance = find_instance(h, entity_id)
    local req = ffi.new('TCP_REQUEST_QUERY_INFORMATION_EX')
    req.ID.toi_entity.tei_entity   = entity_id
    req.ID.toi_entity.tei_instance = instance
    req.ID.toi_class               = INFO_CLASS_PROTOCOL
    req.ID.toi_type                = INFO_TYPE_PROVIDER
    req.ID.toi_id                  = mib_id
    -- Context starts zeroed (ffi.new).  Don't iterate — see header.

    local entry_sz = ffi.sizeof(ctype)
    local out = ffi.new(ctype .. '[?]', BATCH_ENTRIES)

    local got, st = ioctl(h, IOCTL_TCP_QUERY_INFORMATION_EX,
                          req, ffi.sizeof(req),
                          out, entry_sz * BATCH_ENTRIES)

    if st == STATUS_BUFFER_OVERFLOW then
        error(string.format(
            "%s: table exceeds %d entries — kernel cursor writeback " ..
            "is broken (NTDISP.C TCPQueryInformationEx NT_SUCCESS guard); " ..
            "increase BATCH_ENTRIES or fix kernel",
            fn_name, BATCH_ENTRIES))
    end
    if st ~= 0 then
        err.raise(fn_name, st)
    end

    local n = math.floor(got / entry_sz)
    if n > BATCH_ENTRIES then n = BATCH_ENTRIES end
    local result = {}
    for i = 0, n - 1 do
        -- Copy each entry by value so the returned table doesn't
        -- alias into the transient batch buffer (which is about to
        -- go out of scope when this function returns).
        local entry = ffi.new(ctype)
        ffi.copy(entry, out + i, entry_sz)
        result[#result + 1] = entry
    end
    return result
end

-- ------------------------------------------------------------------
-- Public API
-- ------------------------------------------------------------------

-- Common device-open path: synchronous I/O, R+W+SYNC access, share
-- R+W (TDI devices are shareable).  Used for both \Device\Tcp (the
-- info-query channel) and \Device\Ip (the address-set channel).
local function open_device(path)
    local noa = oa.path(path)
    local access = bit.bor(fs.FILE_GENERIC_READ,
                           fs.FILE_GENERIC_WRITE,
                           fs.SYNCHRONIZE)
    local options = bit.bor(fs.FILE_SYNCHRONOUS_IO_NONALERT,
                            fs.FILE_NON_DIRECTORY_FILE)
    local h, _disp = fs.NtCreateFile(access, noa.oa,
                                     nil,                       -- AllocationSize
                                     fs.FILE_ATTRIBUTE_NORMAL,
                                     bit.bor(fs.FILE_SHARE_READ,
                                             fs.FILE_SHARE_WRITE),
                                     fs.FILE_OPEN,
                                     options,
                                     nil, 0)                    -- no EA
    return h
end

function M.open()
    return open_device("\\Device\\Tcp")
end

function M.open_ip()
    return open_device("\\Device\\Ip")
end

function M.ip_stats(h)
    return query_stats(h, CL_NL_ENTITY, IP_MIB_STATS_ID,
                       'IPSNMPInfo', 'nt.net.info.ip_stats')
end

function M.icmp_stats(h)
    return query_stats(h, ER_ENTITY, ICMP_MIB_STATS_ID,
                       'ICMPSNMPInfo', 'nt.net.info.icmp_stats')
end

function M.udp_stats(h)
    return query_stats(h, CL_TL_ENTITY, UDP_MIB_STAT_ID,
                       'UDPStats', 'nt.net.info.udp_stats')
end

function M.addresses(h)
    return walk_table(h, CL_NL_ENTITY, IP_MIB_ADDRTABLE_ENTRY_ID,
                      'IPAddrEntry', 'nt.net.info.addresses')
end

function M.routes(h)
    return walk_table(h, CL_NL_ENTITY, IP_MIB_RTTABLE_ENTRY_ID,
                      'IPRouteEntry', 'nt.net.info.routes')
end

-- ------------------------------------------------------------------
-- Per-interface enumeration.  IF_ENTITY is NOT a cursor walker —
-- each instance returns ONE IFEntry (ARP.C:3497-3560).  To get the
-- full set we iterate the entity list and query each IF_ENTITY
-- instance independently.  DHCP uses this to discover the NIC's
-- MAC for chaddr; also useful for any caller that wants per-NIC
-- counters or MTU.
--
-- Returns an array of IFEntry cdata.  Entries are copied by value
-- so the array survives independent of any transient buffer.  Note
-- we request only IFE_FIXED_SIZE worth of bytes (the description
-- string would round-trip but DHCP doesn't care, and stripping it
-- keeps the cdata fixed-size).
-- ------------------------------------------------------------------

function M.interfaces(h)
    local list, n = _entity_list(h)
    local result = {}
    for i = 0, n - 1 do
        if list[i].tei_entity == IF_ENTITY then
            local req = ffi.new('TCP_REQUEST_QUERY_INFORMATION_EX')
            req.ID.toi_entity.tei_entity   = IF_ENTITY
            req.ID.toi_entity.tei_instance = list[i].tei_instance
            req.ID.toi_class               = INFO_CLASS_PROTOCOL
            req.ID.toi_type                = INFO_TYPE_PROVIDER
            req.ID.toi_id                  = IF_MIB_STATS_ID

            local out = ffi.new('IFEntry')
            -- BUFFER_OVERFLOW here means the descriptor didn't fit
            -- — fine, the fixed part landed and that's all DHCP
            -- wants (ARP.C:3556-3559).
            local _got, st = ioctl(h, IOCTL_TCP_QUERY_INFORMATION_EX,
                                   req, ffi.sizeof(req),
                                   out, ffi.sizeof(out))
            if st ~= 0 and st ~= STATUS_BUFFER_OVERFLOW then
                err.raise('nt.net.info.interfaces', st)
            end

            local entry = ffi.new('IFEntry')
            ffi.copy(entry, out, ffi.sizeof('IFEntry'))
            result[#result + 1] = entry
        end
    end
    return result
end

function M.arp(h)
    -- Multi-interface caveat: find_instance returns the FIRST
    -- AT_ENTITY which on a single-NIC guest is the only one.  When
    -- we grow to multi-NIC, add `arp_for(h, instance)` and have
    -- arp(h) concatenate across all AT_ENTITY instances from
    -- entities(h).
    return walk_table(h, AT_ENTITY, AT_MIB_ADDRXLAT_ENTRY_ID,
                      'IPNetToMediaEntry', 'nt.net.info.arp')
end

-- Escape hatch — full entity list as Lua tables.  Each entry
-- { entity = 0x301, instance = 0 } etc; entity IDs are the TDIINFO.H
-- constants (CL_NL_ENTITY = IP, ER_ENTITY = ICMP, AT_ENTITY = ARP,
-- IF_ENTITY = interfaces, CO_TL_ENTITY = TCP, CL_TL_ENTITY = UDP).
-- The list is short enough that we don't bother with iterators.
function M.entities(h)
    local out, n = _entity_list(h)
    local result = {}
    for i = 0, n - 1 do
        result[#result + 1] = {
            entity   = tonumber(out[i].tei_entity),
            instance = tonumber(out[i].tei_instance),
        }
    end
    return result
end

function M.set_ip_stats(h, info)
    local instance = find_instance(h, CL_NL_ENTITY)
    local req = ffi.new('TCP_REQUEST_SET_INFORMATION_EX_IPSNMP')
    req.ID.toi_entity.tei_entity   = CL_NL_ENTITY
    req.ID.toi_entity.tei_instance = instance
    req.ID.toi_class               = INFO_CLASS_PROTOCOL
    req.ID.toi_type                = INFO_TYPE_PROVIDER
    req.ID.toi_id                  = IP_MIB_STATS_ID
    req.BufferSize                 = ffi.sizeof('IPSNMPInfo')
    -- Value-type struct copy (LuaJIT FFI: assigning a same-typed
    -- struct cdata to a same-typed struct field does memcpy).  After
    -- this line `req.Buffer` and `info` are independent — caller's
    -- info can be reused or GC'd without affecting the IOCTL payload.
    req.Buffer                     = info

    local _got, st = ioctl(h, IOCTL_TCP_SET_INFORMATION_EX,
                           req, ffi.sizeof(req),
                           nil, 0)
    return st
end

-- ------------------------------------------------------------------
-- Route mutation.  Kernel handler at IP/INFO.C:391-496.  The
-- ire_type field doubles as the add/delete discriminator:
-- IRE_TYPE_INVALID = delete, anything else = add (line 478).  We
-- expose two functions for ergonomics; both bottom out at
-- _route_request().
--
-- entry fields (all default to 0 / sensible value):
--   dest      destination IP, network-order ULONG
--   mask      destination mask, network-order ULONG
--   nexthop   gateway IP, network-order ULONG.  Must not be NULL,
--             loopback, classD, or classE — kernel rejects with
--             TDI_INVALID_PARAMETER.  Use the gateway address from
--             DHCP option 3 directly.
--   if_index  outbound interface index — match an iae_index from
--             addresses(h).  LoopIndex is rejected.
--   metric    route metric (default 1)
--   type      IRE_TYPE_DIRECT (on-link) or _INDIRECT (via gateway).
--             Default _INDIRECT — DHCP default-gateway case.
--   proto     IRE_PROTO_* — provenance.  Default _NETMGMT
--             (user-installed); DHCP uses _LOCAL to match the stock
--             NT 3.5 DHCP client.
-- ------------------------------------------------------------------

local function _fill_route_entry(buf, entry)
    buf.ire_dest    = entry.dest    or 0
    buf.ire_mask    = entry.mask    or 0
    buf.ire_nexthop = entry.nexthop or 0
    buf.ire_index   = entry.if_index or 0
    buf.ire_metric1 = entry.metric  or 1
    buf.ire_metric2 = 0
    buf.ire_metric3 = 0
    buf.ire_metric4 = 0
    buf.ire_metric5 = 0
    buf.ire_age     = 0
    buf.ire_type    = entry.type    or M.IRE_TYPE_INDIRECT
    buf.ire_proto   = entry.proto   or M.IRE_PROTO_NETMGMT
end

local function _route_request(h, entry, ire_type_override)
    local instance = find_instance(h, CL_NL_ENTITY)
    local req = ffi.new('TCP_REQUEST_SET_INFORMATION_EX_IPROUTE')
    req.ID.toi_entity.tei_entity   = CL_NL_ENTITY
    req.ID.toi_entity.tei_instance = instance
    req.ID.toi_class               = INFO_CLASS_PROTOCOL
    req.ID.toi_type                = INFO_TYPE_PROVIDER
    req.ID.toi_id                  = IP_MIB_RTTABLE_ENTRY_ID
    req.BufferSize                 = ffi.sizeof('IPRouteEntry')
    _fill_route_entry(req.Buffer, entry)
    if ire_type_override then
        req.Buffer.ire_type = ire_type_override
    end

    local _got, st = ioctl(h, IOCTL_TCP_SET_INFORMATION_EX,
                           req, ffi.sizeof(req),
                           nil, 0)
    return st
end

function M.add_route(h, entry)
    return _route_request(h, entry, nil)
end

function M.del_route(h, entry)
    -- Force IRE_TYPE_INVALID — kernel's wire convention for delete
    -- (IP/INFO.C:478-487).  Caller's entry.type is ignored.
    return _route_request(h, entry, M.IRE_TYPE_INVALID)
end

-- ------------------------------------------------------------------
-- ARP mutation.  Kernel handler at IP/ARP.C:3722-3798.  Same
-- delete-via-INVALID convention as routes.
--
-- entry fields:
--   addr      IP, network-order ULONG
--   if_index  AT_ENTITY instance — must match the AT instance for
--             this interface (find_instance picks the first; multi-
--             NIC support deferred).  The kernel matches by the
--             entity instance, not the field — the inme_index
--             field is informational.
--   mac       6-byte string (Ethernet hardware address)
--   type      INME_TYPE_DYNAMIC / _STATIC.  Default _STATIC for
--             callers (DYNAMIC is what ARP itself installs).
-- ------------------------------------------------------------------

local function _fill_arp_entry(buf, entry)
    buf.inme_index       = entry.if_index or 0
    buf.inme_addr        = entry.addr or 0
    buf.inme_type        = entry.type or M.INME_TYPE_STATIC
    local mac = entry.mac or ""
    buf.inme_physaddrlen = #mac
    -- physaddr buffer is 8 bytes; clear then copy what the caller
    -- gave us (typically 6 for Ethernet).  ffi.fill is faster than
    -- a Lua loop and avoids out-of-bounds risk on the cdata.
    ffi.fill(buf.inme_physaddr, 8, 0)
    if #mac > 0 then
        ffi.copy(buf.inme_physaddr, mac, math.min(#mac, 8))
    end
end

local function _arp_request(h, entry, inme_type_override)
    local instance = find_instance(h, AT_ENTITY)
    local req = ffi.new('TCP_REQUEST_SET_INFORMATION_EX_ARP')
    req.ID.toi_entity.tei_entity   = AT_ENTITY
    req.ID.toi_entity.tei_instance = instance
    req.ID.toi_class               = INFO_CLASS_PROTOCOL
    req.ID.toi_type                = INFO_TYPE_PROVIDER
    req.ID.toi_id                  = AT_MIB_ADDRXLAT_ENTRY_ID
    req.BufferSize                 = ffi.sizeof('IPNetToMediaEntry')
    _fill_arp_entry(req.Buffer, entry)
    if inme_type_override then
        req.Buffer.inme_type = inme_type_override
    end

    local _got, st = ioctl(h, IOCTL_TCP_SET_INFORMATION_EX,
                           req, ffi.sizeof(req),
                           nil, 0)
    return st
end

function M.add_arp(h, entry)
    return _arp_request(h, entry, nil)
end

function M.del_arp(h, entry)
    return _arp_request(h, entry, M.INME_TYPE_INVALID)
end

-- ------------------------------------------------------------------
-- Address push — \Device\Ip + IOCTL_IP_SET_ADDRESS.  Used by DHCP
-- after a successful ACK to bind the leased address + mask to an
-- existing NTE.  `context` comes from addresses(tcp_h)[i].iae_context;
-- the NTE itself is provisioned at NIC init time by the IP driver
-- (one per configured interface), and DHCP reuses the same NTE
-- across lease renewals.
--
-- Returns raw NTSTATUS so DHCP can distinguish kernel refusal
-- (STATUS_INVALID_PARAMETER for bad context, STATUS_ACCESS_DENIED
-- if the handle lacks write access) from STATUS_SUCCESS.
-- ------------------------------------------------------------------

function M.set_address(h_ip, context, address, mask)
    local req = ffi.new('IP_SET_ADDRESS_REQUEST')
    req.Context    = context
    req.Address    = address
    req.SubnetMask = mask
    local _got, st = ioctl(h_ip, IOCTL_IP_SET_ADDRESS,
                           req, ffi.sizeof(req),
                           nil, 0)
    return st
end

return M
