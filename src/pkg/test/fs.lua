-- nt.dll.fs — File / device I/O, info queries, directory enumeration.

local ffi = require('ffi')
local t   = require('test')
local fs  = require('nt.dll.fs')
local oa  = require('nt.dll.oa')
local str = require('nt.dll.str')

t.suite("fs")

local FILE_READ_DATA               = 0x0001
local FILE_WRITE_DATA              = 0x0002
local FILE_READ_ATTRIBUTES         = 0x0080
local FILE_SHARE_READ              = 0x0001
local FILE_SHARE_WRITE             = 0x0002
local SYNCHRONIZE                  = 0x00100000
local FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
local STATUS_END_OF_FILE           = 0xC0000011
local STATUS_NO_MORE_FILES         = 0x80000006

local function open_with(access, path)
    return fs.NtOpenFile(access + SYNCHRONIZE, oa.path(path).oa,
                         FILE_SHARE_READ + FILE_SHARE_WRITE,
                         FILE_SYNCHRONOUS_IO_NONALERT)
end

local function open_ro(path)
    return open_with(FILE_READ_DATA, path)
end

t.test("NtOpenFile on \\Device\\Null", function()
    local h = open_ro("\\Device\\Null")
    t.ne(h, nil)
    h:close()
end)

t.test("NtOpenFile on missing path raises", function()
    t.raises(function() open_ro("\\Device\\NoSuchThing") end)
end)

t.test("Null device reads return EOF (0 bytes)", function()
    local h = open_ro("\\Device\\Null")
    local buf = ffi.new('char[16]')
    local n, st = fs.NtReadFile(h, buf, 16, nil)
    -- Some NT versions return 0 bytes with SUCCESS; others with
    -- STATUS_END_OF_FILE. Either is acceptable — we just assert no
    -- data came out.
    t.eq(n, 0)
    t.ok(st == 0 or st == STATUS_END_OF_FILE,
         string.format("status=0x%x", st))
    h:close()
end)

-- Use kernel32.dll as the canonical large PE on \SystemRoot — both
-- ntoskrnl.exe and hal.dll are routed where='esp' (boot-efi reads them
-- pre-handoff) and so don't appear under \SystemRoot in split layouts.
-- kernel32.dll is default-routed (system partition), is a real PE so
-- its MZ magic + size assertions still mean something, and is a more
-- honest target for "the kind of file user-mode actually opens".
t.test("NtQueryInformationFile(Standard) on a real file reports size", function()
    local h = open_ro("\\SystemRoot\\SYSTEM32\\KERNEL32.DLL")
    local info = fs.query_standard(h)
    t.ok(info.EndOfFile.QuadPart > 100000,
         "kernel32.dll is larger than 100KB")
    t.eq(info.Directory, 0, "kernel32.dll is not a directory")
    t.eq(info.NumberOfLinks, 1)
    h:close()
end)

t.test("NtQueryInformationFile(Basic) on kernel32 reports attributes", function()
    -- FileBasicInformation needs FILE_READ_ATTRIBUTES on the handle;
    -- FILE_READ_DATA alone gives STATUS_ACCESS_DENIED on NT 3.5.
    local h = open_with(FILE_READ_DATA + FILE_READ_ATTRIBUTES,
                        "\\SystemRoot\\SYSTEM32\\KERNEL32.DLL")
    local info = fs.query_basic(h)
    -- FILE_ATTRIBUTE_ARCHIVE = 0x20, _DIRECTORY = 0x10. We don't insist
    -- on an exact mask — just that the directory bit is clear.
    t.eq(ffi.cast('uint32_t', info.FileAttributes) % 0x20, 0,
         "directory bit clear")
    h:close()
end)

t.test("NtReadFile reads MZ magic from kernel32", function()
    local h = open_ro("\\SystemRoot\\SYSTEM32\\KERNEL32.DLL")
    local buf = ffi.new('char[2]')
    local n = fs.NtReadFile(h, buf, 2, nil)
    t.eq(n, 2)
    t.eq(buf[0], string.byte("M"))
    t.eq(buf[1], string.byte("Z"))
    h:close()
end)

t.test("NtQueryDirectoryFile enumerates \\SystemRoot\\", function()
    local h = open_ro("\\SystemRoot\\")
    -- Confirm it's flagged as a directory first.
    local info = fs.query_standard(h)
    t.ok(info.Directory ~= 0, "opened \\SystemRoot\\ is a directory")
    local buf   = ffi.new('char[4096]')
    local seen  = {}
    local first = true
    while true do
        local len, st = fs.NtQueryDirectoryFile(h, buf, 4096, first)
        first = false
        if st == STATUS_NO_MORE_FILES or len == 0 then break end
        local off = 0
        while true do
            local e = ffi.cast('FILE_DIRECTORY_INFORMATION *',
                                ffi.cast('char *', buf) + off)
            local name = str.from_wchars(e.FileName, e.FileNameLength / 2)
            if name ~= "." and name ~= ".." then
                seen[name] = e.FileAttributes
            end
            if e.NextEntryOffset == 0 then break end
            off = off + e.NextEntryOffset
        end
    end
    h:close()
    -- FAT up-cases; the system partition has SYSTEM32 / LUA / PKG / TMP.
    -- (EFI lives only on the ESP, which isn't \SystemRoot in split layouts.)
    t.ne(seen.SYSTEM32, nil, "SYSTEM32 present")
    t.ne(seen.LUA,      nil, "LUA present")
    t.ne(seen.PKG,      nil, "PKG present")
end)

t.test("Directory enumeration on non-directory returns empty/fails", function()
    -- Opening a regular file then issuing NtQueryDirectoryFile should
    -- fail with a parameter/device error — our wrapper raises on it.
    local h = open_ro("\\SystemRoot\\SYSTEM32\\KERNEL32.DLL")
    local buf = ffi.new('char[1024]')
    t.raises(function() fs.NtQueryDirectoryFile(h, buf, 1024, true) end)
    h:close()
end)
