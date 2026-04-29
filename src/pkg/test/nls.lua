-- nt.nls — \NLS\ namespace publisher.
--
-- The publisher is invoked once at selftest startup (see selftest.lua)
-- before this suite runs. These tests then verify the resulting
-- namespace from a non-publisher process: the named sections must be
-- openable, mappable, and contain the same bytes as the on-disk .nls
-- files. Re-running publish must be idempotent (OBJ_OPENIF).

local bit = require('bit')
local ffi = require('ffi')
local t   = require('test')
local fs  = require('nt.dll.fs')
local mm  = require('nt.dll.mm')
local oa  = require('nt.dll.oa')
local nls = require('nt.nls')
local se  = require('nt.dll.se')

t.suite("nls")

local SECTION_NAMES = {
    "\\NLS\\NlsSectionUnicode",
    "\\NLS\\NlsSectionLocale",
    "\\NLS\\NlsSectionCType",
    "\\NLS\\NlsSectionLANG_INTL",
    "\\NLS\\NlsSectionCP1252",
    "\\NLS\\NlsSectionCP437",
    "\\NLS\\NlsSectionSortkey",
    "\\NLS\\NlsSectionSortTbls",
}

ffi.cdef[[
NTSTATUS __stdcall NtOpenSection(HANDLE *SectionHandle,
                                 ULONG DesiredAccess,
                                 OBJECT_ATTRIBUTES *ObjectAttributes);
]]

-- Wrap NtOpenSection thinly — mm.lua doesn't yet expose it (the wrapper
-- is queued for nt.dll.ex per the comment at the bottom of mm.lua). The
-- raw call lets us exercise the publisher without preempting that work.
local ntdll = require('nt.dll')
local err   = require('nt.dll.errors')
local handle = require('nt.dll.handle')

local function open_section(name, access)
    local noa = oa.path(name)
    local h   = ffi.new('HANDLE[1]')
    local st  = ntdll.NtOpenSection(h, access, noa.oa)
    if err.is_error(st) then err.raise('NtOpenSection', st) end
    return handle.wrap(h[0])
end

t.test("every published section opens read-only", function()
    for _, name in ipairs(SECTION_NAMES) do
        local h = open_section(name, mm.SECTION_MAP_READ)
        t.ok(h, "open " .. name)
        h:close()
    end
end)

t.test("CP1252 section maps and contents match on-disk file", function()
    local sec = open_section("\\NLS\\NlsSectionCP1252", mm.SECTION_MAP_READ)
    local base, view_size = mm.NtMapViewOfSection(
        sec, nil, nil, 0, 0, 0,
        mm.ViewUnmap, 0, mm.PAGE_READONLY)
    t.ne(base, nil, "view base non-null")
    t.ok(view_size >= 4096, "view at least one page")

    -- Cross-check: open the file directly and compare the first 64 bytes.
    -- Both the section and the file see the same FAT pages, so the
    -- mapped view must equal the file's prefix byte-for-byte.
    local file_oa = oa.path("\\SystemRoot\\System32\\c_1252.nls")
    local hf = fs.NtOpenFile(
        bit.bor(fs.FILE_GENERIC_READ, fs.SYNCHRONIZE),
        file_oa.oa,
        fs.FILE_SHARE_READ,
        fs.FILE_SYNCHRONOUS_IO_NONALERT)
    local buf = ffi.new('unsigned char[64]')
    fs.NtReadFile(hf, buf, 64)
    hf:close()

    local view = ffi.cast('unsigned char *', base)
    for i = 0, 63 do
        t.eq(view[i], buf[i], string.format("byte %d", i))
    end

    mm.NtUnmapViewOfSection(nil, base)
    sec:close()
end)

t.test("publish() is idempotent under SeCreatePermanentPrivilege", function()
    -- The selftest prelude already published once. A second run must
    -- not raise (OBJ_OPENIF makes the kernel return the existing
    -- objects rather than STATUS_OBJECT_NAME_COLLISION) and the
    -- namespace must remain intact afterwards.
    se.with_privileges({"SeCreatePermanentPrivilege"}, nls.publish)
    local h = open_section("\\NLS\\NlsSectionCP1252", mm.SECTION_MAP_READ)
    h:close()
end)
