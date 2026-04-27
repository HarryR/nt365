-- Value — registry value pseudo-Node. Leaf; no children, no open.
-- Populated by the Key handler during enumeration: captures the
-- raw data bytes plus REG_* type code at yield time so access is
-- free (no reread against the parent key).
--
-- Fields (lazy, via __index):
--   type    REG_NONE=0, REG_SZ=1, REG_EXPAND_SZ=2, REG_BINARY=3,
--           REG_DWORD=4, REG_MULTI_SZ=7, REG_RESOURCE_LIST=8,
--           REG_FULL_RESOURCE_DESCRIPTOR=9
--   length  bytes in .data
--   data    Lua string of raw bytes (copy — owned, not a view)
--
-- Methods:
--   decode()  Type-aware conversion from raw bytes to a Lua-native
--             value. REG_SZ → string, REG_DWORD → number, REG_MULTI_SZ
--             → array of strings, REG_RESOURCE_LIST → structured
--             {full=[{interface_type, bus_number, partials=[...]}]}
--             table. Rendering for display lives in main.lua — this
--             stays pure semantic conversion.

local ffi = require('ffi')
local str = require('nt.dll.str')

-- CM_RESOURCE_LIST / CM_FULL_RESOURCE_DESCRIPTOR — driver hardware
-- resource bindings, stored as the data payload for REG_RESOURCE_LIST
-- and REG_FULL_RESOURCE_DESCRIPTOR values. Flattened tree:
--   CM_RESOURCE_LIST        ULONG Count; CM_FULL_RESOURCE_DESCRIPTOR[Count]
--     CM_FULL_RESOURCE_DESCRIPTOR                                -- 8-byte header
--         int InterfaceType; ULONG BusNumber;
--         CM_PARTIAL_RESOURCE_LIST                               -- 8-byte header
--             USHORT Version; USHORT Revision; ULONG Count;
--             CM_PARTIAL_RESOURCE_DESCRIPTOR[Count]              -- 16 bytes each
-- Both outer and partial-list arrays are variable-length; walk with
-- pointer arithmetic.
--
-- pack(4): NT 3.5 kernel ABI aligns 8-byte values (PHYSICAL_ADDRESS)
-- on 4-byte boundaries. Without this, LuaJIT's default would push the
-- LARGE_INTEGER inside Port.Start to offset 8 and break the 16-byte
-- descriptor size.
ffi.cdef[[
#pragma pack(push, 4)
typedef struct _CM_PHYS_ADDR {
    ULONG LowPart;
    long  HighPart;
} CM_PHYS_ADDR;

typedef struct _CM_PARTIAL_RESOURCE_DESCRIPTOR {
    unsigned char  Type;
    unsigned char  ShareDisposition;
    unsigned short Flags;
    union {
        struct { CM_PHYS_ADDR Start; ULONG Length; }                  Port;
        struct { ULONG Level; ULONG Vector; ULONG Affinity; }          Interrupt;
        struct { CM_PHYS_ADDR Start; ULONG Length; }                  Memory;
        struct { ULONG Channel; ULONG Port; ULONG Reserved1; }        Dma;
        struct { ULONG DataSize; ULONG Reserved1; ULONG Reserved2; }  DeviceSpecificData;
    } u;
} CM_PARTIAL_RESOURCE_DESCRIPTOR;

typedef struct _CM_PARTIAL_RESOURCE_LIST {
    unsigned short Version;
    unsigned short Revision;
    ULONG Count;
    /* CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[Count] follows */
} CM_PARTIAL_RESOURCE_LIST;

typedef struct _CM_FULL_RESOURCE_DESCRIPTOR {
    int   InterfaceType;
    ULONG BusNumber;
    CM_PARTIAL_RESOURCE_LIST PartialResourceList;
} CM_FULL_RESOURCE_DESCRIPTOR;

typedef struct _CM_RESOURCE_LIST {
    ULONG Count;
    /* CM_FULL_RESOURCE_DESCRIPTOR List[Count] follows (variable length) */
} CM_RESOURCE_LIST;
#pragma pack(pop)
]]

local REG_NONE                     = 0
local REG_SZ                       = 1
local REG_EXPAND_SZ                = 2
local REG_BINARY                   = 3
local REG_DWORD                    = 4
local REG_MULTI_SZ                 = 7
local REG_RESOURCE_LIST            = 8
local REG_FULL_RESOURCE_DESCRIPTOR = 9

-- Round-trip data (Lua string) → char[?] buffer so we can cast to
-- struct pointers. Lua strings can't be pointer-cast directly through
-- LuaJIT without making the buffer first.
local function to_buf(data)
    local n = #data > 0 and #data or 1
    local buf = ffi.new('char[?]', n)
    ffi.copy(buf, data, #data)
    return buf
end

local function decode_sz(data)
    local buf = to_buf(data)
    local wp  = ffi.cast('wchar_t *', buf)
    local nc  = #data / 2
    if nc > 0 and wp[nc-1] == 0 then nc = nc - 1 end   -- drop trailing NUL
    return str.from_wchars(wp, nc)
end

local function decode_multi_sz(data)
    local buf = to_buf(data)
    local wp  = ffi.cast('wchar_t *', buf)
    local nc  = #data / 2
    local out = {}
    local start = 0
    for j = 0, nc - 1 do
        if wp[j] == 0 then
            if j > start then
                out[#out+1] = str.from_wchars(wp + start, j - start)
            end
            start = j + 1
        end
    end
    return out
end

local function decode_dword(data)
    local buf = to_buf(data)
    return ffi.cast('uint32_t *', buf)[0]
end

local function decode_partial(p)
    local t = p.Type
    if t == 1 then
        return { kind = "Port",
                 start = p.u.Port.Start.LowPart,
                 length = p.u.Port.Length }
    elseif t == 2 then
        return { kind = "Interrupt",
                 level = p.u.Interrupt.Level,
                 vector = p.u.Interrupt.Vector,
                 affinity = p.u.Interrupt.Affinity }
    elseif t == 3 then
        return { kind = "Memory",
                 start = p.u.Memory.Start.LowPart,
                 length = p.u.Memory.Length }
    elseif t == 4 then
        return { kind = "Dma",
                 channel = p.u.Dma.Channel,
                 port = p.u.Dma.Port }
    elseif t == 5 then
        return { kind = "DeviceSpecific",
                 data_size = p.u.DeviceSpecificData.DataSize }
    end
    return { kind = "Unknown", type = t }
end

local function decode_full(fd_ptr)
    local fd    = ffi.cast('CM_FULL_RESOURCE_DESCRIPTOR *', fd_ptr)
    local n     = fd.PartialResourceList.Count
    local base  = ffi.cast('char *', fd_ptr) + 16
    local parts = {}
    for i = 0, n - 1 do
        local pd = ffi.cast('CM_PARTIAL_RESOURCE_DESCRIPTOR *', base + i * 16)
        parts[i+1] = decode_partial(pd)
    end
    return {
        interface_type = fd.InterfaceType,
        bus_number     = fd.BusNumber,
        version        = fd.PartialResourceList.Version,
        revision       = fd.PartialResourceList.Revision,
        partials       = parts,
    }
end

local function decode_resource_list(data)
    local buf  = to_buf(data)
    local rl   = ffi.cast('CM_RESOURCE_LIST *', buf)
    local full = {}
    local ptr  = ffi.cast('char *', buf) + 4      -- past ULONG Count
    for f = 0, rl.Count - 1 do
        full[f+1] = decode_full(ptr)
        local fd = ffi.cast('CM_FULL_RESOURCE_DESCRIPTOR *', ptr)
        ptr = ptr + 16 + fd.PartialResourceList.Count * 16
    end
    return { full = full }
end

local function decode_full_top(data)
    local buf = to_buf(data)
    return decode_full(buf)
end

local DECODERS = {
    [REG_NONE]                     = function()      return nil  end,
    [REG_SZ]                       = decode_sz,
    [REG_EXPAND_SZ]                = decode_sz,
    [REG_DWORD]                    = decode_dword,
    [REG_MULTI_SZ]                 = decode_multi_sz,
    [REG_BINARY]                   = function(data) return data end,
    [REG_RESOURCE_LIST]            = decode_resource_list,
    [REG_FULL_RESOURCE_DESCRIPTOR] = decode_full_top,
}

local M = {}

M.fields = {
    type   = function(n) return n.__value_type   end,
    length = function(n) return n.__value_length end,
    data   = function(n) return n.__value_data   end,
}

M.methods = {
    decode = function(node)
        local d = DECODERS[node.__value_type]
        if d then return d(node.__value_data) end
        return node.__value_data   -- unknown type → raw bytes
    end,
}

M.descriptions = {
    type   = "REG_* type code (1=SZ, 2=EXPAND_SZ, 3=BINARY, 4=DWORD, 7=MULTI_SZ, 8=RESOURCE_LIST, 9=FULL_RESOURCE_DESCRIPTOR).",
    length = "Size of .data in bytes.",
    data   = "Raw bytes of the value as a Lua string.",
    decode = "decode() → Lua-native value converted from .data per .type (string, number, table-of-strings, structured resource list, ...).",
}

return M
