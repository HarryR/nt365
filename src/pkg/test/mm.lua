-- nt.dll.mm — virtual memory + sections.

local ffi = require('ffi')
local t   = require('test')
local mm  = require('nt.dll.mm')

t.suite("mm")

t.test("NtAllocateVirtualMemory 4K round-trip", function()
    local base, size = mm.NtAllocateVirtualMemory(nil, nil, 4096,
        mm.MEM_COMMIT + mm.MEM_RESERVE, mm.PAGE_READWRITE)
    t.ne(base, nil, "base should be non-null")
    t.ok(size >= 4096, "size rounded to page granularity")
    -- Write + read back verifies the region is actually committed RW.
    ffi.cast('uint32_t *', base)[0] = 0xCAFEBABE
    t.eq(ffi.cast('uint32_t *', base)[0], 0xCAFEBABE)
    mm.NtFreeVirtualMemory(nil, base, 0, mm.MEM_RELEASE)
end)

t.test("NtProtectVirtualMemory flips RW -> RO", function()
    local base, size = mm.NtAllocateVirtualMemory(nil, nil, 4096,
        mm.MEM_COMMIT + mm.MEM_RESERVE, mm.PAGE_READWRITE)
    local old = mm.NtProtectVirtualMemory(nil, base, size, mm.PAGE_READONLY)
    t.eq(old, mm.PAGE_READWRITE, "old protect should be the initial RW")
    -- Flip back before freeing so nothing weird happens.
    mm.NtProtectVirtualMemory(nil, base, size, mm.PAGE_READWRITE)
    mm.NtFreeVirtualMemory(nil, base, 0, mm.MEM_RELEASE)
end)

t.test("NtQueryVirtualMemory_Basic reports committed RW", function()
    local base, size = mm.NtAllocateVirtualMemory(nil, nil, 4096,
        mm.MEM_COMMIT + mm.MEM_RESERVE, mm.PAGE_READWRITE)
    local info = mm.NtQueryVirtualMemory_Basic(nil, base)
    t.eq(info.State, mm.MEM_COMMIT)
    t.eq(info.Protect, mm.PAGE_READWRITE)
    t.eq(info.Type, mm.MEM_PRIVATE)
    t.ok(info.RegionSize >= 4096)
    mm.NtFreeVirtualMemory(nil, base, 0, mm.MEM_RELEASE)
end)

t.test("anonymous section map/unmap round-trip", function()
    -- Unnamed pagefile-backed section — no namespace entry needed.
    local sec = mm.NtCreateSection(
        mm.SECTION_ALL_ACCESS, nil, 65536,
        mm.PAGE_READWRITE, mm.SEC_COMMIT, nil)
    local base, view_size = mm.NtMapViewOfSection(
        sec, nil, nil, 0, 0, 0,
        mm.ViewUnmap, 0, mm.PAGE_READWRITE)
    t.ne(base, nil)
    t.ok(view_size >= 65536)
    -- Write at several offsets so we confirm the whole view is live.
    local p = ffi.cast('uint32_t *', base)
    p[0]        = 0xF00DFACE
    p[1024]     = 0xDEADBEEF
    p[16383]    = 0x12345678
    t.eq(p[0],     0xF00DFACE)
    t.eq(p[1024],  0xDEADBEEF)
    t.eq(p[16383], 0x12345678)
    mm.NtUnmapViewOfSection(nil, base)
    sec:close()
end)

t.test("section two independent views share pages", function()
    -- Create a section, map it twice in this process; writes via one
    -- view must appear in the other.
    local sec = mm.NtCreateSection(
        mm.SECTION_ALL_ACCESS, nil, 4096,
        mm.PAGE_READWRITE, mm.SEC_COMMIT, nil)
    local a, _ = mm.NtMapViewOfSection(sec, nil, nil, 0, 0, 0,
        mm.ViewUnmap, 0, mm.PAGE_READWRITE)
    local b, _ = mm.NtMapViewOfSection(sec, nil, nil, 0, 0, 0,
        mm.ViewUnmap, 0, mm.PAGE_READWRITE)
    t.ne(a, b, "distinct virtual bases for two views")
    ffi.cast('uint32_t *', a)[0] = 0x13371337
    t.eq(ffi.cast('uint32_t *', b)[0], 0x13371337,
         "second view reflects writes via first")
    mm.NtUnmapViewOfSection(nil, a)
    mm.NtUnmapViewOfSection(nil, b)
    sec:close()
end)
