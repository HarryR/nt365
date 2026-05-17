-- test.fuzz.ex — kernel-range pointer-slot sweep for EX syscalls.
--
-- Part of the deref-before-probe sweep (the bug class written up as
-- NT-BUGS.md entry #5): a syscall that reads a field of an untrusted
-- caller pointer before ProbeForRead/Write -- or a self-probing
-- capture helper -- has validated it. A kernel-range pointer faults
-- past __try and bugchecks 0x50/0x1E; only the probe's range check,
-- which rejects the pointer as data before any deref, stops it.
--
-- A dedicated P14 prologue audit of every Executive syscall
-- (EX/EVENT.C, EVENTPR.C, MUTANT.C, SEMPHORE.C, TIMER.C, PROFILE.C,
-- LUID.C, SYSTIME.C, SYSINFO.C, SYSENV.C, HARDERR.C, DBGCTRL.C,
-- DELAY.C, EXINIT.C, VDMSTUB.C) found the subsystem clean. EX is the
-- most uniform NTOS subsystem: create/open probe the OUT handle
-- (ProbeForWriteHandle) and hand OBJECT_ATTRIBUTES to ObCreateObject /
-- ObOpenObjectByName; query probes the output buffer + ReturnLength;
-- the signal syscalls probe their optional previous-state OUT param
-- (ProbeForWriteLong/Boolean); NtSetTimer probes DueTime
-- (ProbeForRead) before the capture; NtRaiseHardError bounds-checks
-- NumberOfParameters, probes Response + the Parameters array, then
-- probe-captures each nested UNICODE_STRING. No deref-before-probe.
-- So EX needs no kernel fix; this suite is the confirm-net.
--
-- For every pointer argument of each bridged pointer-bearing EX
-- syscall we hand the kernel a kernel-range pointer (0x80000000,
-- dword-aligned so the range check -- not the alignment check -- is
-- the rejecting condition) in that one slot while every other argument
-- stays valid, then assert a clean error NTSTATUS. As with the other
-- fuzz suites the deeper assertion is survival: the in-process runner
-- reaching t.summary() means no probe regressed into a bugcheck.
--
-- Scope: this suite sweeps the syscalls whose kernel code lives in
-- EX/*.C -- the event / event-pair / mutant / semaphore / timer
-- family. The other syscalls nt.dll.ex bridges belong to other
-- subsystems and are swept by their own suites: the IoCompletion
-- family is IO (test/fuzz/iocp.lua), the Section family is MM
-- (test/fuzz/mm.lua), NtQueryObject is OB (test/fuzz/ob.lua),
-- NtSetInformationFile is IO (test/fuzz/io.lua). NtWaitForSingleObject
-- is a KE syscall, out of the NTOS P14 sweep's scope. The handle-only
-- EX syscalls (NtSetHigh/LowEventPair, NtWaitHigh/LowEventPair, ...)
-- have no caller pointer to sweep. NtCreateEvent is not bridged, so
-- NtQueryEvent is swept with a null handle -- its prologue probes the
-- output buffer and ReturnLength before ObReferenceObjectByHandle
-- (confirmed EVENT.C), so the poisoned slot is the rejecting
-- condition regardless of the handle.

local t      = require('test')
local ex     = require('nt.dll.ex')       -- registers the EX cdefs
local oa     = require('nt.dll.oa')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')
local ntdll  = require('nt.dll')
local ffi    = require('ffi')

-- First byte past MmUserProbeAddress. dword-aligned: the range check,
-- not the alignment check, is what must reject this.
local KERNEL_PTR = ffi.cast('void *', 0x80000000)

-- A plausible all-access mask. The create/open prologues probe the
-- OUT handle before the access mask matters, so the exact value is
-- not load-bearing -- every poisoned call fails first.
local ALL_ACCESS = 0x1F0003

t.suite("ex: hardening (kernel-range pointer-slot sweep)")

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
local function hslot()  return ffi.new('HANDLE[1]')          end
local function ulong()  return ffi.new('ULONG[1]')           end
local function long()   return ffi.new('long[1]')            end
local function uchar()  return ffi.new('unsigned char[1]')   end
local function li()     return ffi.new('LARGE_INTEGER[1]')   end
local function bytes(n) return ffi.new('unsigned char[?]', n or 64) end

-- A valid OBJECT_ATTRIBUTES. The name is never created -- every
-- poisoned call fails before any object is made -- so a plain unique
-- name is fine. oa.path(...).oa anchors its own backing memory.
local function valid_oa() return oa.path("\\FuzzExProbe").oa end

-- Real EX-object handles for the query / release / set / cancel
-- syscalls, which reference the handle around the probe. The
-- idiomatic ex.* factories build them.
local mutant_h, sem_h, timer_h
do
    local ok, m = pcall(ex.mutex)
    if ok then mutant_h = m end
    local ok2, s = pcall(ex.semaphore, { maximum = 4 })
    if ok2 then sem_h = s end
    local ok3, tm = pcall(ex.timer)
    if ok3 then timer_h = tm end
end

local function mutant_raw()
    assert(mutant_h, "test.fuzz.ex: could not create scratch mutant")
    return handle.raw(mutant_h:handle())
end
local function sem_raw()
    assert(sem_h, "test.fuzz.ex: could not create scratch semaphore")
    return handle.raw(sem_h:handle())
end
local function timer_raw()
    assert(timer_h, "test.fuzz.ex: could not create scratch timer")
    return handle.raw(timer_h:handle())
end

-- ---- Open family -- OUT Handle, IN ObjectAttributes ----
-- Prologue probes the OUT handle (ProbeForWriteHandle); the
-- OBJECT_ATTRIBUTES flow into ObOpenObjectByName, whose
-- ObpCaptureObjectAttributes probes them.

local openers = {
    { "NtOpenEvent",     ntdll.NtOpenEvent     },
    { "NtOpenEventPair", ntdll.NtOpenEventPair },
    { "NtOpenMutant",    ntdll.NtOpenMutant    },
    { "NtOpenSemaphore", ntdll.NtOpenSemaphore },
    { "NtOpenTimer",     ntdll.NtOpenTimer     },
}

for _, o in ipairs(openers) do
    local name, fn = o[1], o[2]
    t.test(name .. " rejects kernel-range Handle", function()
        local st = err.normalize(fn(KERNEL_PTR, ALL_ACCESS, valid_oa()))
        rejects(st, name .. "/Handle")
    end)
    t.test(name .. " rejects kernel-range ObjectAttributes", function()
        local st = err.normalize(fn(hslot(), ALL_ACCESS, KERNEL_PTR))
        rejects(st, name .. "/ObjectAttributes")
    end)
end

-- ---- Create family -- OUT Handle, IN ObjectAttributes ----
-- Same prologue shape as the openers; ObjectAttributes route into
-- ObCreateObject's ObpCaptureObjectAttributes.

t.test("NtCreateMutant rejects kernel-range Handle", function()
    local st = err.normalize(ntdll.NtCreateMutant(
        KERNEL_PTR, ALL_ACCESS, valid_oa(), 0))
    rejects(st, "NtCreateMutant/Handle")
end)

t.test("NtCreateMutant rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateMutant(
        hslot(), ALL_ACCESS, KERNEL_PTR, 0))
    rejects(st, "NtCreateMutant/ObjectAttributes")
end)

t.test("NtCreateSemaphore rejects kernel-range Handle", function()
    local st = err.normalize(ntdll.NtCreateSemaphore(
        KERNEL_PTR, ALL_ACCESS, valid_oa(), 0, 4))
    rejects(st, "NtCreateSemaphore/Handle")
end)

t.test("NtCreateSemaphore rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateSemaphore(
        hslot(), ALL_ACCESS, KERNEL_PTR, 0, 4))
    rejects(st, "NtCreateSemaphore/ObjectAttributes")
end)

t.test("NtCreateTimer rejects kernel-range Handle", function()
    local st = err.normalize(ntdll.NtCreateTimer(
        KERNEL_PTR, ALL_ACCESS, valid_oa(), 0))
    rejects(st, "NtCreateTimer/Handle")
end)

t.test("NtCreateTimer rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateTimer(
        hslot(), ALL_ACCESS, KERNEL_PTR, 0))
    rejects(st, "NtCreateTimer/ObjectAttributes")
end)

t.test("NtCreateEventPair rejects kernel-range Handle", function()
    local st = err.normalize(ntdll.NtCreateEventPair(
        KERNEL_PTR, ALL_ACCESS, valid_oa()))
    rejects(st, "NtCreateEventPair/Handle")
end)

t.test("NtCreateEventPair rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateEventPair(
        hslot(), ALL_ACCESS, KERNEL_PTR))
    rejects(st, "NtCreateEventPair/ObjectAttributes")
end)

-- ---- Query family -- OUT Information, OUT ReturnLength ----
-- Prologue probes Information (ProbeForWrite, length-sized) then
-- ReturnLength (ProbeForWriteUlong) before the handle is referenced;
-- class 0 (the basic-information class) selects the post-probe branch.

t.test("NtQueryEvent rejects kernel-range Information", function()
    -- Null handle: the prologue probe runs ahead of the handle ref.
    local st = err.normalize(ntdll.NtQueryEvent(
        nil, 0, KERNEL_PTR, 64, ulong()))
    rejects(st, "NtQueryEvent/Information")
end)

t.test("NtQueryEvent rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryEvent(
        nil, 0, bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtQueryEvent/ReturnLength")
end)

t.test("NtQueryMutant rejects kernel-range Information", function()
    local st = err.normalize(ntdll.NtQueryMutant(
        mutant_raw(), 0, KERNEL_PTR, 64, ulong()))
    rejects(st, "NtQueryMutant/Information")
end)

t.test("NtQueryMutant rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryMutant(
        mutant_raw(), 0, bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtQueryMutant/ReturnLength")
end)

t.test("NtQuerySemaphore rejects kernel-range Information", function()
    local st = err.normalize(ntdll.NtQuerySemaphore(
        sem_raw(), 0, KERNEL_PTR, 64, ulong()))
    rejects(st, "NtQuerySemaphore/Information")
end)

t.test("NtQuerySemaphore rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQuerySemaphore(
        sem_raw(), 0, bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtQuerySemaphore/ReturnLength")
end)

t.test("NtQueryTimer rejects kernel-range Information", function()
    local st = err.normalize(ntdll.NtQueryTimer(
        timer_raw(), 0, KERNEL_PTR, 64, ulong()))
    rejects(st, "NtQueryTimer/Information")
end)

t.test("NtQueryTimer rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryTimer(
        timer_raw(), 0, bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtQueryTimer/ReturnLength")
end)

-- ---- NtReleaseMutant / NtReleaseSemaphore -- OUT PreviousCount ----
-- PreviousCount is an optional OUT probed (ProbeForWriteLong) in the
-- prologue ahead of the release work.

t.test("NtReleaseMutant rejects kernel-range PreviousCount", function()
    local st = err.normalize(ntdll.NtReleaseMutant(
        mutant_raw(), KERNEL_PTR))
    rejects(st, "NtReleaseMutant/PreviousCount")
end)

t.test("NtReleaseSemaphore rejects kernel-range PreviousCount", function()
    local st = err.normalize(ntdll.NtReleaseSemaphore(
        sem_raw(), 1, KERNEL_PTR))
    rejects(st, "NtReleaseSemaphore/PreviousCount")
end)

-- ---- NtSetTimer -- IN DueTime, OUT PreviousState ----
-- Prologue probes PreviousState (ProbeForWriteBoolean, optional) and
-- DueTime (ProbeForRead) before capturing the due time.

t.test("NtSetTimer rejects kernel-range DueTime", function()
    local st = err.normalize(ntdll.NtSetTimer(
        timer_raw(), KERNEL_PTR, nil, nil, uchar()))
    rejects(st, "NtSetTimer/DueTime")
end)

t.test("NtSetTimer rejects kernel-range PreviousState", function()
    local st = err.normalize(ntdll.NtSetTimer(
        timer_raw(), li(), nil, nil, KERNEL_PTR))
    rejects(st, "NtSetTimer/PreviousState")
end)

-- ---- NtCancelTimer -- OUT CurrentState ----

t.test("NtCancelTimer rejects kernel-range CurrentState", function()
    local st = err.normalize(ntdll.NtCancelTimer(
        timer_raw(), KERNEL_PTR))
    rejects(st, "NtCancelTimer/CurrentState")
end)
