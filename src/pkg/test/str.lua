-- nt.dll.str — UTF-16 / UTF-8 conversion.

local ffi = require('ffi')
local t   = require('test')
local str = require('nt.dll.str')

t.suite("str")

t.test("from_wchars(nil, 0) returns empty string", function()
    t.eq(str.from_wchars(nil, 0), "")
end)

t.test("from_wchars(nil, 5) tolerates null ptr, returns empty", function()
    -- Null guard added in the GC audit pass — without it this used to
    -- deref wp[0] and fault.
    t.eq(str.from_wchars(nil, 5), "")
end)

t.test("from_wchars(wp, 0) returns empty for any ptr", function()
    local wp = ffi.new('wchar_t[4]', { 0x48, 0x65, 0x6C, 0x6C })
    t.eq(str.from_wchars(wp, 0), "")
end)

t.test("ASCII round-trips", function()
    local ns = str.to_utf16("hello")
    t.eq(str.from_utf16(ns.us), "hello")
end)

t.test("empty string round-trips", function()
    local ns = str.to_utf16("")
    t.eq(str.from_utf16(ns.us), "")
end)

t.test("multi-byte UTF-8 round-trips", function()
    -- U+00E9 é (2-byte UTF-8 → 1 wchar), U+1F600 😀 (4-byte UTF-8 → 2 wchars)
    local s = "r\xC3\xA9sum\xC3\xA9"   -- "résumé"
    local ns = str.to_utf16(s)
    t.eq(str.from_utf16(ns.us), s)
end)

t.test("from_utf16 on zero-length UNICODE_STRING", function()
    local ns = str.new_utf16(4)   -- capacity > 0, Length = 0
    t.eq(str.from_utf16(ns.us), "")
end)
