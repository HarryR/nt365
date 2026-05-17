-- test.fuzz.ps — kernel-range pointer-slot sweep for PS syscalls.
--
-- Part of the deref-before-probe sweep (the bug class written up as
-- NT-BUGS.md entry #5): a syscall that reads a field of an untrusted
-- caller pointer before ProbeForRead/Write -- or a self-probing
-- capture helper -- has validated it. A kernel-range pointer faults
-- past __try and bugchecks 0x50/0x1E; only the probe's range check,
-- which rejects the pointer as data before any deref, stops it.
--
-- A dedicated P14 prologue audit of all 22 PS syscalls found the
-- subsystem clean: it probes every caller pointer (ProbeForWriteHandle,
-- ProbeForRead/Write, ProbeForWriteUlong) before any field read, and
-- the context syscalls' mode-conditional probe still runs ahead of the
-- deref on the user path. So PS needs no kernel fix; this suite is the
-- confirm-net that locks the clean audit in.
--
-- For every pointer argument of each bridged pointer-bearing PS
-- syscall we hand the kernel a kernel-range pointer (0x80000000,
-- dword-aligned so the range check -- not the alignment check -- is
-- the rejecting condition) in that one slot while every other argument
-- stays valid, then assert a clean error NTSTATUS. As with test.fuzz.se
-- the deeper assertion is survival: the in-process runner reaching
-- t.summary() means no probe regressed into a bugcheck.
--
-- NtCreateThread's ObjectAttributes is intentionally not swept: it is
-- OPTIONAL and flows into ObCreateObject (whose ObpCaptureObjectAttributes
-- probes), not a prologue probe -- it is not a P14 prologue surface.
-- The four prologue-probed NtCreateThread pointers are covered. The
-- handle-only PS syscalls (NtTerminate*, NtAlert*, NtResume's siblings
-- with no out-param, ...) have no caller pointer to sweep; the
-- unbridged PS syscalls are out of scope here -- their prologues
-- audited clean too. Cover them when bridged.

local t      = require('test')
local ps     = require('nt.dll.ps')       -- registers the PS cdefs
local err    = require('nt.dll.errors')
local ntdll  = require('nt.dll')
local ffi    = require('ffi')

-- First byte past MmUserProbeAddress. dword-aligned: the range check,
-- not the alignment check, is what must reject this.
local KERNEL_PTR = ffi.cast('void *', 0x80000000)

-- NT pseudo-handles. The query/suspend syscalls probe their caller
-- pointers before referencing these, so they are valid enough to
-- reach the probe under test.
local CURRENT_PROCESS = ffi.cast('void *', -1)
local CURRENT_THREAD  = ffi.cast('void *', -2)

t.suite("ps: hardening (kernel-range pointer-slot sweep)")

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
local function hslot() return ffi.new('HANDLE[1]')            end
local function ulong() return ffi.new('ULONG[1]')             end
local function cid()   return ffi.new('CLIENT_ID[1]')         end
local function teb()   return ffi.new('INITIAL_TEB[1]')       end
local function oattr() return ffi.new('OBJECT_ATTRIBUTES[1]') end
local function bytes(n) return ffi.new('unsigned char[?]', n or 256) end

-- ---- NtOpenProcess -- OUT ProcessHandle, IN ObjectAttributes, IN ClientId ----
-- Prologue probes ProcessHandle -> ObjectAttributes -> ClientId.

t.test("NtOpenProcess rejects kernel-range ProcessHandle", function()
    local st = err.normalize(ntdll.NtOpenProcess(
        KERNEL_PTR, 0, oattr(), nil))
    rejects(st, "NtOpenProcess/ProcessHandle")
end)

t.test("NtOpenProcess rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtOpenProcess(
        hslot(), 0, KERNEL_PTR, nil))
    rejects(st, "NtOpenProcess/ObjectAttributes")
end)

t.test("NtOpenProcess rejects kernel-range ClientId", function()
    local st = err.normalize(ntdll.NtOpenProcess(
        hslot(), 0, oattr(), KERNEL_PTR))
    rejects(st, "NtOpenProcess/ClientId")
end)

-- ---- NtOpenThread -- OUT ThreadHandle, IN ObjectAttributes, IN ClientId ----

t.test("NtOpenThread rejects kernel-range ThreadHandle", function()
    local st = err.normalize(ntdll.NtOpenThread(
        KERNEL_PTR, 0, oattr(), nil))
    rejects(st, "NtOpenThread/ThreadHandle")
end)

t.test("NtOpenThread rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtOpenThread(
        hslot(), 0, KERNEL_PTR, nil))
    rejects(st, "NtOpenThread/ObjectAttributes")
end)

t.test("NtOpenThread rejects kernel-range ClientId", function()
    local st = err.normalize(ntdll.NtOpenThread(
        hslot(), 0, oattr(), KERNEL_PTR))
    rejects(st, "NtOpenThread/ClientId")
end)

-- ---- NtCreateThread -- OUT ThreadHandle, OUT ClientId, IN ThreadContext,
--                        IN InitialTeb ----
-- Prologue probes ThreadHandle -> ClientId -> ThreadContext -> InitialTeb,
-- all before PspCreateThread runs, so every poisoned call is rejected
-- before any thread object is created.

t.test("NtCreateThread rejects kernel-range ThreadHandle", function()
    local st = err.normalize(ntdll.NtCreateThread(
        KERNEL_PTR, 0, nil, CURRENT_PROCESS, cid(),
        ffi.cast('void *', bytes(1024)), teb(), 1))
    rejects(st, "NtCreateThread/ThreadHandle")
end)

t.test("NtCreateThread rejects kernel-range ClientId", function()
    local st = err.normalize(ntdll.NtCreateThread(
        hslot(), 0, nil, CURRENT_PROCESS, KERNEL_PTR,
        ffi.cast('void *', bytes(1024)), teb(), 1))
    rejects(st, "NtCreateThread/ClientId")
end)

t.test("NtCreateThread rejects kernel-range ThreadContext", function()
    local st = err.normalize(ntdll.NtCreateThread(
        hslot(), 0, nil, CURRENT_PROCESS, cid(),
        KERNEL_PTR, teb(), 1))
    rejects(st, "NtCreateThread/ThreadContext")
end)

t.test("NtCreateThread rejects kernel-range InitialTeb", function()
    -- ThreadContext valid (CONTEXT-sized scratch) so the prologue
    -- clears it and reaches the InitialTeb probe.
    local ctx = bytes(1024)
    local st = err.normalize(ntdll.NtCreateThread(
        hslot(), 0, nil, CURRENT_PROCESS, cid(),
        ffi.cast('void *', ctx), KERNEL_PTR, 1))
    rejects(st, "NtCreateThread/InitialTeb")
end)

-- ---- NtSuspendThread / NtResumeThread -- OUT PreviousCount ----
-- PreviousCount is probed before the thread handle is referenced.

t.test("NtSuspendThread rejects kernel-range PreviousCount", function()
    local st = err.normalize(ntdll.NtSuspendThread(
        CURRENT_THREAD, KERNEL_PTR))
    rejects(st, "NtSuspendThread/PreviousCount")
end)

t.test("NtResumeThread rejects kernel-range PreviousCount", function()
    local st = err.normalize(ntdll.NtResumeThread(
        CURRENT_THREAD, KERNEL_PTR))
    rejects(st, "NtResumeThread/PreviousCount")
end)

-- ---- NtQueryInformationProcess -- OUT ProcessInformation, OUT ReturnLength ----
-- Both probed in the prologue before the process handle is referenced;
-- class 0 (ProcessBasicInformation) just selects the post-probe branch.

t.test("NtQueryInformationProcess rejects kernel-range ProcessInformation", function()
    local st = err.normalize(ntdll.NtQueryInformationProcess(
        CURRENT_PROCESS, 0, KERNEL_PTR, 64, ulong()))
    rejects(st, "NtQueryInformationProcess/ProcessInformation")
end)

t.test("NtQueryInformationProcess rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryInformationProcess(
        CURRENT_PROCESS, 0, bytes(64), 64, KERNEL_PTR))
    rejects(st, "NtQueryInformationProcess/ReturnLength")
end)
