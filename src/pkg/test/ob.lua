-- nt.dll.ob — Object Manager.

local ffi = require('ffi')
local t   = require('test')
local ob  = require('nt.dll.ob')
local oa  = require('nt.dll.oa')
local str = require('nt.dll.str')

t.suite("ob")

local DIR_ACCESS           = 0x3   -- DIRECTORY_QUERY | DIRECTORY_TRAVERSE
local SYMBOLIC_LINK_QUERY  = 0x1
local STATUS_NO_MORE_ENTRIES = 0x8000001A

t.test("NtOpenDirectoryObject on root", function()
    local h = ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path("\\").oa)
    t.ne(h, nil)
    h:close()
end)

t.test("NtOpenDirectoryObject on missing path raises", function()
    t.raises(function()
        ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path("\\NoSuchDir").oa)
    end)
end)

t.test("NtQueryDirectoryObject enumerates root", function()
    local h = ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path("\\").oa)
    local buf   = ffi.new('char[4096]')
    local ctx   = ffi.new('ULONG[1]')
    local seen  = {}
    local first = true
    while true do
        local len, st = ob.NtQueryDirectoryObject(h, buf, 4096,
                                                  true, first, ctx)
        first = false
        if st == STATUS_NO_MORE_ENTRIES or len == 0 then break end
        local info = ffi.cast('OBJECT_DIRECTORY_INFORMATION *', buf)
        seen[str.from_utf16(info.Name)] = str.from_utf16(info.TypeName)
    end
    h:close()
    -- REGISTRY is usually uppercased by the kernel at this level.
    t.ok(seen.REGISTRY == "Key" or seen.Registry == "Key",
         "Registry key is in root enumeration")
    t.eq(seen.Device, "Directory")
    t.eq(seen.ObjectTypes, "Directory")
end)

t.test("NtOpenSymbolicLinkObject + NtQuerySymbolicLinkObject", function()
    local sr = ob.NtOpenSymbolicLinkObject(SYMBOLIC_LINK_QUERY,
                                            oa.path("\\SystemRoot").oa)
    t.ne(sr, nil)
    local target = ob.NtQuerySymbolicLinkObject(sr)
    t.ok(target:match("^\\Device\\Harddisk"),
         "SystemRoot target is a Harddisk path: " .. target)
    sr:close()
end)

t.test("NtQueryObject Name on root directory", function()
    local h = ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path("\\").oa)
    local buf = ffi.new('char[4096]')
    ob.NtQueryObject(h, 1 --[[ Name ]], buf, 4096)
    local ni = ffi.cast('OBJECT_NAME_INFORMATION *', buf)
    t.eq(str.from_utf16(ni.Name), "\\")
    h:close()
end)

t.test("NtQueryObject Type on root directory reports 'Directory'", function()
    local h = ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path("\\").oa)
    local buf = ffi.new('char[4096]')
    ob.NtQueryObject(h, 2 --[[ Type ]], buf, 4096)
    local ti = ffi.cast('OBJECT_TYPE_INFORMATION *', buf)
    t.eq(str.from_utf16(ti.Name), "Directory")
    h:close()
end)

t.test("NtQueryObject Basic info requires exact size", function()
    local h = ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path("\\").oa)
    local bi = ffi.new('OBJECT_BASIC_INFORMATION')
    ob.NtQueryObject(h, 0 --[[ Basic ]], bi,
                     ffi.sizeof('OBJECT_BASIC_INFORMATION'))
    t.ok(bi.GrantedAccess ~= 0, "some access granted")
    t.ok(bi.PointerCount > 0)
    -- And: passing a wrong-sized buffer must raise (NT 3.5 checks ==).
    t.raises(function()
        local buf = ffi.new('char[4096]')
        ob.NtQueryObject(h, 0, buf, 4096)
    end)
    h:close()
end)
