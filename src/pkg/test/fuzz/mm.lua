-- test.fuzz.mm — kernel-range pointer-slot sweep for MM syscalls.
--
-- Part of the deref-before-probe sweep (the bug class written up as
-- NT-BUGS.md entry #5): a syscall that reads a field of an untrusted
-- caller pointer before ProbeForRead/Write -- or a self-probing
-- capture helper -- has validated it. A kernel-range pointer faults
-- past __try and bugchecks 0x50/0x1E; only the probe's range check,
-- which rejects the pointer as data before any deref, stops it.
--
-- A dedicated P14 prologue audit of the MM virtual-memory and section
-- syscalls found ONE bug: NtCreateSection peeked `*MaximumSize` inside
-- its prologue try block with only the SectionHandle probed -- the
-- MaximumSize pointer itself was never probed (CREASECT.C:197). A
-- kernel-range MaximumSize faulted past the try and bugchecked. Fixed
-- with a ProbeForRead ahead of the deref. Every other MM syscall was
-- clean: alloc/free/protect/lock/flush probe each IN OUT pointer
-- (ProbeForWriteUlong, ProbeForWriteIoStatus) before the capture, the
-- query pair probes its output buffer + ReturnLength, read/write
-- range-check the cross-process Buffer and probe the byte-count out,
-- and NtOpenSection hands ObjectAttributes straight to
-- ObOpenObjectByName. So this suite is both the regression test for
-- the NtCreateSection fix and the confirm-net locking the rest in.
--
-- For every pointer argument of each bridged pointer-bearing MM
-- syscall we hand the kernel a kernel-range pointer (0x80000000,
-- dword-aligned so the range check -- not the alignment check -- is
-- the rejecting condition) in that one slot while every other argument
-- stays valid, then assert a clean error NTSTATUS. As with the other
-- fuzz suites the deeper assertion is survival: the in-process runner
-- reaching t.summary() means no probe regressed into a bugcheck.
--
-- NtMapViewOfSection and NtExtendSection need a real SECTION-object
-- handle to reach the probe under test, so the suite creates a small
-- pagefile-backed section as scaffolding. NtCreateSection's optional
-- ObjectAttributes is intentionally not swept: it is OPTIONAL and
-- flows into MmCreateSection -> ObCreateObject (whose
-- ObpCaptureObjectAttributes probes), not a prologue probe. The value
-- args that are not dereferenced -- NtUnmapViewOfSection's BaseAddress,
-- the query/read/write BaseAddress -- have no pointer slot to sweep.

local t      = require('test')
local mm     = require('nt.dll.mm')       -- registers the MM cdefs
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')
local ntdll  = require('nt.dll')
local ffi    = require('ffi')

-- First byte past MmUserProbeAddress. dword-aligned: the range check,
-- not the alignment check, is what must reject this.
local KERNEL_PTR = ffi.cast('void *', 0x80000000)

-- Pseudo-handle for the current process. The VM syscalls probe their
-- caller pointers before referencing the process, so this is valid
-- enough to reach the probe under test.
local CURRENT_PROCESS = ffi.cast('void *', -1)

t.suite("mm: hardening (kernel-range pointer-slot sweep)")

-- Assert a syscall return is a clean error NTSTATUS. Reaching this
-- assertion at all means the kernel rejected the pointer as data and
-- did not fault on it.
local function rejects(st, slot)
    t.ok(st >= 0xC0000000,
         slot .. ": expected error NTSTATUS, got "
         .. string.format("0x%08x", st))
end

-- Valid scratch. Each returns a fresh cdata the caller holds for the
-- duration of the syscall (no ffi.cast indirection -> no GC dangle).
local function hslot() return ffi.new('HANDLE[1]')         end
local function ulong() return ffi.new('ULONG[1]')          end
local function pslot() return ffi.new('void *[1]')         end
local function li()    return ffi.new('LARGE_INTEGER[1]')  end
local function iosb()  return ffi.new('IO_STATUS_BLOCK[1]') end
local function bytes(n) return ffi.new('unsigned char[?]', n or 256) end

-- A valid LARGE_INTEGER[1] carrying a small non-zero size, for the
-- slots that must stay valid while another slot is poisoned.
local function size_li(n)
    local x = li()
    x[0].QuadPart = n or 4096
    return x
end

-- A real SECTION-object handle for NtMapViewOfSection / NtExtendSection,
-- which reference the section before reaching some probes. A 4 KB
-- pagefile-backed commit section is the cheapest source.
local section_h
do
    local ok, h = pcall(mm.NtCreateSection,
        mm.SECTION_ALL_ACCESS, nil, 4096,
        mm.PAGE_READWRITE, mm.SEC_COMMIT, nil)
    if ok then section_h = h end
end

local function section_raw()
    assert(section_h,
           "test.fuzz.mm: could not create scratch SECTION handle")
    return handle.raw(section_h)
end

-- ---- NtAllocateVirtualMemory -- IN OUT BaseAddress, IN OUT RegionSize ----
-- Prologue probes BaseAddress -> RegionSize (ProbeForWriteUlong) then
-- captures both. MEM_RESERVE / PAGE_READWRITE keep the post-probe path
-- valid; every poisoned call is rejected before any VAD is touched.

t.test("NtAllocateVirtualMemory rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtAllocateVirtualMemory(
        CURRENT_PROCESS, KERNEL_PTR, 0, ulong(),
        mm.MEM_RESERVE, mm.PAGE_READWRITE))
    rejects(st, "NtAllocateVirtualMemory/BaseAddress")
end)

t.test("NtAllocateVirtualMemory rejects kernel-range RegionSize", function()
    local rs = ulong(); rs[0] = 4096
    local st = err.normalize(ntdll.NtAllocateVirtualMemory(
        CURRENT_PROCESS, pslot(), 0, KERNEL_PTR,
        mm.MEM_RESERVE, mm.PAGE_READWRITE))
    rejects(st, "NtAllocateVirtualMemory/RegionSize")
end)

-- ---- NtFreeVirtualMemory -- IN OUT BaseAddress, IN OUT RegionSize ----

t.test("NtFreeVirtualMemory rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtFreeVirtualMemory(
        CURRENT_PROCESS, KERNEL_PTR, ulong(), mm.MEM_RELEASE))
    rejects(st, "NtFreeVirtualMemory/BaseAddress")
end)

t.test("NtFreeVirtualMemory rejects kernel-range RegionSize", function()
    local st = err.normalize(ntdll.NtFreeVirtualMemory(
        CURRENT_PROCESS, pslot(), KERNEL_PTR, mm.MEM_RELEASE))
    rejects(st, "NtFreeVirtualMemory/RegionSize")
end)

-- ---- NtProtectVirtualMemory -- IN OUT BaseAddress, IN OUT
--      NumberOfBytesToProtect, OUT OldAccessProtection ----
-- Prologue probes all three before the base/size capture.

t.test("NtProtectVirtualMemory rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtProtectVirtualMemory(
        CURRENT_PROCESS, KERNEL_PTR, ulong(),
        mm.PAGE_READWRITE, ulong()))
    rejects(st, "NtProtectVirtualMemory/BaseAddress")
end)

t.test("NtProtectVirtualMemory rejects kernel-range NumberOfBytesToProtect", function()
    local st = err.normalize(ntdll.NtProtectVirtualMemory(
        CURRENT_PROCESS, pslot(), KERNEL_PTR,
        mm.PAGE_READWRITE, ulong()))
    rejects(st, "NtProtectVirtualMemory/NumberOfBytesToProtect")
end)

t.test("NtProtectVirtualMemory rejects kernel-range OldAccessProtection", function()
    local st = err.normalize(ntdll.NtProtectVirtualMemory(
        CURRENT_PROCESS, pslot(), ulong(),
        mm.PAGE_READWRITE, KERNEL_PTR))
    rejects(st, "NtProtectVirtualMemory/OldAccessProtection")
end)

-- ---- NtQueryVirtualMemory -- OUT MemoryInformation, OUT ReturnLength ----
-- Prologue probes MemoryInformation (ProbeForWrite, length-sized) then
-- ReturnLength before the VAD is walked; class 0 just selects the
-- post-probe branch.

local MBI_LEN = ffi.sizeof('MEMORY_BASIC_INFORMATION')

t.test("NtQueryVirtualMemory rejects kernel-range MemoryInformation", function()
    local st = err.normalize(ntdll.NtQueryVirtualMemory(
        CURRENT_PROCESS, ffi.cast('void *', bytes(64)),
        mm.MemoryBasicInformation, KERNEL_PTR, MBI_LEN, ulong()))
    rejects(st, "NtQueryVirtualMemory/MemoryInformation")
end)

t.test("NtQueryVirtualMemory rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryVirtualMemory(
        CURRENT_PROCESS, ffi.cast('void *', bytes(64)),
        mm.MemoryBasicInformation, bytes(MBI_LEN), MBI_LEN, KERNEL_PTR))
    rejects(st, "NtQueryVirtualMemory/ReturnLength")
end)

-- ---- NtReadVirtualMemory -- IN BaseAddress, OUT Buffer,
--      OUT NumberOfBytesRead ----
-- The prologue range-checks Buffer against MM_HIGHEST_USER_ADDRESS
-- (a kernel-range Buffer is rejected as ACCESS_VIOLATION before any
-- copy) and probes NumberOfBytesRead with ProbeForWriteUlong.

t.test("NtReadVirtualMemory rejects kernel-range Buffer", function()
    local st = err.normalize(ntdll.NtReadVirtualMemory(
        CURRENT_PROCESS, ffi.cast('void *', bytes(64)),
        KERNEL_PTR, 64, ulong()))
    rejects(st, "NtReadVirtualMemory/Buffer")
end)

t.test("NtReadVirtualMemory rejects kernel-range NumberOfBytesRead", function()
    local st = err.normalize(ntdll.NtReadVirtualMemory(
        CURRENT_PROCESS, ffi.cast('void *', bytes(64)),
        bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtReadVirtualMemory/NumberOfBytesRead")
end)

-- ---- NtWriteVirtualMemory -- IN BaseAddress, IN Buffer,
--      OUT NumberOfBytesWritten ----

t.test("NtWriteVirtualMemory rejects kernel-range Buffer", function()
    local st = err.normalize(ntdll.NtWriteVirtualMemory(
        CURRENT_PROCESS, ffi.cast('void *', bytes(64)),
        KERNEL_PTR, 64, ulong()))
    rejects(st, "NtWriteVirtualMemory/Buffer")
end)

t.test("NtWriteVirtualMemory rejects kernel-range NumberOfBytesWritten", function()
    local st = err.normalize(ntdll.NtWriteVirtualMemory(
        CURRENT_PROCESS, ffi.cast('void *', bytes(64)),
        bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtWriteVirtualMemory/NumberOfBytesWritten")
end)

-- ---- NtFlushVirtualMemory -- IN OUT BaseAddress, IN OUT
--      NumberOfBytesToFlush, OUT IoStatus ----
-- Prologue probes BaseAddress -> NumberOfBytesToFlush ->
-- IoStatus (ProbeForWriteIoStatus) before the capture.

t.test("NtFlushVirtualMemory rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtFlushVirtualMemory(
        CURRENT_PROCESS, KERNEL_PTR, ulong(), iosb()))
    rejects(st, "NtFlushVirtualMemory/BaseAddress")
end)

t.test("NtFlushVirtualMemory rejects kernel-range NumberOfBytesToFlush", function()
    local st = err.normalize(ntdll.NtFlushVirtualMemory(
        CURRENT_PROCESS, pslot(), KERNEL_PTR, iosb()))
    rejects(st, "NtFlushVirtualMemory/NumberOfBytesToFlush")
end)

t.test("NtFlushVirtualMemory rejects kernel-range IoStatus", function()
    local st = err.normalize(ntdll.NtFlushVirtualMemory(
        CURRENT_PROCESS, pslot(), ulong(), KERNEL_PTR))
    rejects(st, "NtFlushVirtualMemory/IoStatus")
end)

-- ---- NtLockVirtualMemory -- IN OUT BaseAddress, IN OUT
--      NumberOfBytesToLock ----

t.test("NtLockVirtualMemory rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtLockVirtualMemory(
        CURRENT_PROCESS, KERNEL_PTR, ulong(), 1))
    rejects(st, "NtLockVirtualMemory/BaseAddress")
end)

t.test("NtLockVirtualMemory rejects kernel-range NumberOfBytesToLock", function()
    local st = err.normalize(ntdll.NtLockVirtualMemory(
        CURRENT_PROCESS, pslot(), KERNEL_PTR, 1))
    rejects(st, "NtLockVirtualMemory/NumberOfBytesToLock")
end)

-- ---- NtUnlockVirtualMemory -- IN OUT BaseAddress, IN OUT
--      NumberOfBytesToUnlock ----

t.test("NtUnlockVirtualMemory rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtUnlockVirtualMemory(
        CURRENT_PROCESS, KERNEL_PTR, ulong(), 1))
    rejects(st, "NtUnlockVirtualMemory/BaseAddress")
end)

t.test("NtUnlockVirtualMemory rejects kernel-range NumberOfBytesToUnlock", function()
    local st = err.normalize(ntdll.NtUnlockVirtualMemory(
        CURRENT_PROCESS, pslot(), KERNEL_PTR, 1))
    rejects(st, "NtUnlockVirtualMemory/NumberOfBytesToUnlock")
end)

-- ---- NtCreateSection -- OUT SectionHandle, IN MaximumSize ----
-- The MaximumSize slot is the P14 regression test: before the fix the
-- prologue peeked `*MaximumSize` with only SectionHandle probed.
-- FileHandle nil = pagefile-backed, so MaximumSize is required and the
-- prologue always reaches the deref.

t.test("NtCreateSection rejects kernel-range SectionHandle", function()
    local st = err.normalize(ntdll.NtCreateSection(
        KERNEL_PTR, mm.SECTION_ALL_ACCESS, nil, size_li(4096),
        mm.PAGE_READWRITE, mm.SEC_COMMIT, nil))
    rejects(st, "NtCreateSection/SectionHandle")
end)

t.test("NtCreateSection rejects kernel-range MaximumSize", function()
    local st = err.normalize(ntdll.NtCreateSection(
        hslot(), mm.SECTION_ALL_ACCESS, nil, KERNEL_PTR,
        mm.PAGE_READWRITE, mm.SEC_COMMIT, nil))
    rejects(st, "NtCreateSection/MaximumSize")
end)

-- ---- NtMapViewOfSection -- IN OUT BaseAddress, IN OUT SectionOffset,
--      IN OUT ViewSize ----
-- Prologue probes BaseAddress -> ViewSize then, if present,
-- SectionOffset, all inside one try before the section is mapped.

t.test("NtMapViewOfSection rejects kernel-range BaseAddress", function()
    local st = err.normalize(ntdll.NtMapViewOfSection(
        section_raw(), CURRENT_PROCESS, KERNEL_PTR, 0, 0,
        nil, ulong(), mm.ViewUnmap, 0, mm.PAGE_READWRITE))
    rejects(st, "NtMapViewOfSection/BaseAddress")
end)

t.test("NtMapViewOfSection rejects kernel-range SectionOffset", function()
    local st = err.normalize(ntdll.NtMapViewOfSection(
        section_raw(), CURRENT_PROCESS, pslot(), 0, 0,
        KERNEL_PTR, ulong(), mm.ViewUnmap, 0, mm.PAGE_READWRITE))
    rejects(st, "NtMapViewOfSection/SectionOffset")
end)

t.test("NtMapViewOfSection rejects kernel-range ViewSize", function()
    local st = err.normalize(ntdll.NtMapViewOfSection(
        section_raw(), CURRENT_PROCESS, pslot(), 0, 0,
        nil, KERNEL_PTR, mm.ViewUnmap, 0, mm.PAGE_READWRITE))
    rejects(st, "NtMapViewOfSection/ViewSize")
end)

-- ---- NtExtendSection -- IN OUT NewSectionSize ----
-- Prologue probes NewSectionSize (ProbeForWrite, LARGE_INTEGER-sized)
-- before the capture.

t.test("NtExtendSection rejects kernel-range NewSectionSize", function()
    local st = err.normalize(ntdll.NtExtendSection(
        section_raw(), KERNEL_PTR))
    rejects(st, "NtExtendSection/NewSectionSize")
end)
