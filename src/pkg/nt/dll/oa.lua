-- nt.dll.oa — OBJECT_ATTRIBUTES construction.
--
-- Every NT namespace-facing syscall (NtOpenFile, NtOpenKey, NtOpen*Object,
-- NtCreate*, ...) takes an OBJECT_ATTRIBUTES that embeds a UNICODE_STRING
-- (.ObjectName) that embeds a wchar_t buffer (.Buffer). Three pointer-
-- linked structs.
--
-- A naive construction (three separate ffi.new cdata with cross-pointers)
-- is Shape 1 in NOTES.md — LuaJIT's GC can't follow the aliasing, so
-- any one of them going out of scope while the outer call is still in
-- flight dangles a pointer. We fuse all three into one cdata:
--
--   typedef struct _NT_OA_PATH {
--       OBJECT_ATTRIBUTES oa;          -- at offset 0
--       UNICODE_STRING    us;          -- oa.ObjectName points here
--       wchar_t           wbuf[?];     -- us.Buffer points here
--   } NT_OA_PATH;
--
-- All internal pointers are set to addresses inside the same allocation.
-- Caller holds one ref (`noa`); when dropped, everything goes together.
-- Pass to syscalls as `noa.oa` — LuaJIT takes the address automatically
-- when the ffi signature expects OBJECT_ATTRIBUTES *.
--
-- Exports:
--   path(utf8_path, attributes, root)   Construct NT_OA_PATH naming
--                                       `utf8_path`. `attributes`
--                                       defaults to OBJ_CASE_INSENSITIVE.
--                                       `root` is an optional
--                                       RootDirectory handle (raw or
--                                       NT_HANDLE).

local bit    = require('bit')
local ffi    = require('ffi')
require('nt.dll')                       -- OBJECT_ATTRIBUTES, UNICODE_STRING
local str    = require('nt.dll.str')
local handle = require('nt.dll.handle')

ffi.cdef[[
#pragma pack(push, 4)
typedef struct _NT_OA_PATH {
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    us;
    wchar_t           wbuf[?];
} NT_OA_PATH;
#pragma pack(pop)
]]

local M = {}

-- OBJECT_ATTRIBUTES.Attributes flag bits. Single source of truth for
-- the codebase — callers use M.OBJ_PERMANENT etc. instead of inline hex.
M.OBJ_INHERIT          = 0x00000002
M.OBJ_PERMANENT        = 0x00000010
M.OBJ_EXCLUSIVE        = 0x00000020
M.OBJ_CASE_INSENSITIVE = 0x00000040
M.OBJ_OPENIF           = 0x00000080
M.OBJ_OPENLINK         = 0x00000100
M.OBJ_KERNEL_HANDLE    = 0x00000200

-- Coerce the `attributes` argument of M.path to a single ULONG mask.
-- Accepts:
--   nil        — defaults to OBJ_CASE_INSENSITIVE (the common case).
--   number     — used as-is.
--   table[int] — bit.bor of every entry. Empty table → 0 (no flags).
-- A name-string table form is intentionally not supported: the OBJ_*
-- values are ABI constants matching the C headers, and a second
-- string-namespace would compete with the canonical names.
local function attrs_mask(v)
    if v == nil then return M.OBJ_CASE_INSENSITIVE end
    if type(v) == 'number' then return v end
    local m = 0
    for _, f in ipairs(v) do m = bit.bor(m, f) end
    return m
end

function M.path(utf8_path, attributes, root)
    local wchars = str.decode_utf8(utf8_path)
    local n      = #wchars
    local noa    = ffi.new('NT_OA_PATH', n + 1)   -- +1 for trailing NUL

    -- Inline wbuf.
    for k = 1, n do
        noa.wbuf[k-1] = wchars[k]
    end
    noa.wbuf[n] = 0

    -- Inline UNICODE_STRING, pointing at own wbuf.
    noa.us.Buffer        = noa.wbuf
    noa.us.Length        = n * 2
    noa.us.MaximumLength = (n + 1) * 2

    -- OBJECT_ATTRIBUTES pointing at own us.
    noa.oa.Length                   = ffi.sizeof('OBJECT_ATTRIBUTES')
    noa.oa.RootDirectory            = root and handle.raw(root) or nil
    noa.oa.ObjectName               = ffi.cast('UNICODE_STRING *', noa.us)
    noa.oa.Attributes               = attrs_mask(attributes)
    noa.oa.SecurityDescriptor       = nil
    noa.oa.SecurityQualityOfService = nil

    return noa
end

return M
