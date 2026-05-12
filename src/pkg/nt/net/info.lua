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

/* SET_INFORMATION_EX with an IPSNMPInfo payload baked in.  The
 * kernel struct uses Buffer[1] with the real payload immediately
 * following; we embed the whole IPSNMPInfo so the cdata is one
 * contiguous allocation. */
typedef struct _TCP_REQUEST_SET_INFORMATION_EX_IPSNMP {
    TDIObjectID   ID;
    unsigned int  BufferSize;
    IPSNMPInfo    Buffer;
} TCP_REQUEST_SET_INFORMATION_EX_IPSNMP;

#pragma pack(pop)
]]

-- ------------------------------------------------------------------
-- Internal constants (TDIINFO.H + IPINFO.H).  Hidden from callers
-- because the surface above is keyed on names, not entity IDs.
-- ------------------------------------------------------------------

local GENERIC_ENTITY        = 0
local CL_NL_ENTITY          = 0x301   -- IP
local ER_ENTITY             = 0x380   -- ICMP

local INFO_CLASS_GENERIC    = 0x100
local INFO_CLASS_PROTOCOL   = 0x200
local INFO_TYPE_PROVIDER    = 0x100

local ENTITY_LIST_ID        = 0
local IP_MIB_STATS_ID       = 1
local ICMP_MIB_STATS_ID     = 1

-- IOCTL codes — CTL_CODE(FILE_DEVICE_NETWORK=0x12, fn, method, access).
-- QUERY = (0x12<<16) | (0<<14) | (0<<2) | METHOD_NEITHER(3)             = 0x00120003
-- SET   = (0x12<<16) | (FILE_WRITE_ACCESS=2 <<14) | (1<<2) | BUFFERED(0) = 0x00128004
local IOCTL_TCP_QUERY_INFORMATION_EX = 0x00120003
local IOCTL_TCP_SET_INFORMATION_EX   = 0x00128004

-- ------------------------------------------------------------------
-- Public constants — values callers write into IPSNMPInfo, and the
-- two NTSTATUS codes the H-020 refusal test asserts against.
-- ------------------------------------------------------------------

M.IP_FORWARDING         = 1
M.IP_NOT_FORWARDING     = 2

M.STATUS_SUCCESS        = 0x00000000
M.TDI_INVALID_PARAMETER = 0xC000000D   -- == STATUS_INVALID_PARAMETER

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
-- Public API
-- ------------------------------------------------------------------

function M.open()
    local noa = oa.path("\\Device\\Tcp")
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

function M.ip_stats(h)
    return query_stats(h, CL_NL_ENTITY, IP_MIB_STATS_ID,
                       'IPSNMPInfo', 'nt.net.info.ip_stats')
end

function M.icmp_stats(h)
    return query_stats(h, ER_ENTITY, ICMP_MIB_STATS_ID,
                       'ICMPSNMPInfo', 'nt.net.info.icmp_stats')
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

return M
