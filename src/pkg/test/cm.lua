-- nt.dll.cm — Configuration Manager (registry).

local ffi = require('ffi')
local t   = require('test')
local cm  = require('nt.dll.cm')
local oa  = require('nt.dll.oa')
local str = require('nt.dll.str')

t.suite("cm")

local KEY_READ_ACCESS        = 0x9   -- QUERY_VALUE | ENUMERATE_SUB_KEYS
local STATUS_NO_MORE_ENTRIES = 0x8000001A
local STATUS_BUFFER_OVERFLOW = 0x80000005

t.test("NtOpenKey on \\Registry\\Machine\\System", function()
    local k = cm.NtOpenKey(KEY_READ_ACCESS,
                           oa.path("\\Registry\\Machine\\System").oa)
    t.ne(k, nil)
    k:close()
end)

t.test("NtOpenKey on missing path raises", function()
    t.raises(function()
        cm.NtOpenKey(KEY_READ_ACCESS,
                     oa.path("\\Registry\\Machine\\NoSuchHive").oa)
    end)
end)

t.test("NtEnumerateKey finds CurrentControlSet under \\Registry\\Machine\\System", function()
    local k = cm.NtOpenKey(KEY_READ_ACCESS,
                           oa.path("\\Registry\\Machine\\System").oa)
    local buf  = ffi.new('char[1024]')
    local seen = {}
    local i = 0
    while true do
        local len, st = cm.NtEnumerateKey(k, i, cm.KeyBasicInformation,
                                          buf, 1024)
        if st == STATUS_NO_MORE_ENTRIES then break end
        if st ~= STATUS_BUFFER_OVERFLOW then
            local info = ffi.cast('KEY_BASIC_INFORMATION *', buf)
            seen[str.from_wchars(info.Name, info.NameLength / 2)] = true
        end
        i = i + 1
    end
    k:close()
    t.ok(seen.CurrentControlSet, "CurrentControlSet subkey present")
end)

t.test("NtEnumerateValueKey on Init key finds Exe and Stdio", function()
    local k = cm.NtOpenKey(KEY_READ_ACCESS,
        oa.path("\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Init").oa)
    local buf  = ffi.new('char[1024]')
    local seen = {}
    local i = 0
    while true do
        local len, st = cm.NtEnumerateValueKey(k, i, cm.KeyValueFullInformation,
                                               buf, 1024)
        if st == STATUS_NO_MORE_ENTRIES then break end
        if st ~= STATUS_BUFFER_OVERFLOW then
            local info = ffi.cast('KEY_VALUE_FULL_INFORMATION *', buf)
            seen[str.from_wchars(info.Name, info.NameLength / 2)] = info.Type
        end
        i = i + 1
    end
    k:close()
    t.ne(seen.Exe,   nil, "Exe value present")
    t.ne(seen.Stdio, nil, "Stdio value present")
    t.eq(seen.Exe,   1 --[[ REG_SZ ]])
end)

t.test("NtQueryValueKey reads a DWORD from an atdisk service key", function()
    local k = cm.NtOpenKey(KEY_READ_ACCESS,
        oa.path("\\Registry\\Machine\\System\\CurrentControlSet\\Services\\atdisk").oa)
    local buf = ffi.new('char[256]')
    local len, st = cm.NtQueryValueKey(k, "Type", cm.KeyValueFullInformation,
                                       buf, 256)
    t.eq(st, 0, "STATUS_SUCCESS from NtQueryValueKey")
    local info = ffi.cast('KEY_VALUE_FULL_INFORMATION *', buf)
    t.eq(info.Type, 4 --[[ REG_DWORD ]])
    t.eq(info.DataLength, 4)
    local data = ffi.cast('uint32_t *',
                          ffi.cast('char *', buf) + info.DataOffset)[0]
    -- atdisk's Type should be 1 (kernel driver). Exact value comes from
    -- mkhive.py so this is stable.
    t.eq(data, 1)
    k:close()
end)

t.test("NtQueryValueKey on missing value raises", function()
    local k = cm.NtOpenKey(KEY_READ_ACCESS,
        oa.path("\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Init").oa)
    local buf = ffi.new('char[256]')
    t.raises(function()
        cm.NtQueryValueKey(k, "NoSuchValue", cm.KeyValueFullInformation,
                           buf, 256)
    end)
    k:close()
end)
