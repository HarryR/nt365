-- test.fuzz.ob — kernel-range pointer-slot sweep for OB syscalls.
--
-- Part of the deref-before-probe sweep (the bug class written up as
-- NT-BUGS.md entry #5): a syscall that reads a field of an untrusted
-- caller pointer before ProbeForRead/Write -- or a self-probing
-- capture routine -- has validated it. A kernel-range pointer faults
-- past __try and bugchecks 0x50/0x1E; only the probe's range check,
-- which rejects the pointer as data before any deref, stops it.
--
-- Auditing the OB syscall prologues turned up one instance:
-- NtCreateSymbolicLinkObject peeked ObjectAttributes->Attributes
-- before probing ObjectAttributes (OBLINK.C). It is fixed in the same
-- commit as this suite, and the NtCreateSymbolicLinkObject /
-- ObjectAttributes case below is its regression test -- it bugchecks
-- the whole runner on the unfixed kernel and returns a clean error
-- NTSTATUS on the fixed one. Every other OB syscall audited clean:
-- their OBJECT_ATTRIBUTES arguments flow straight into ObCreateObject
-- / ObOpenObjectByName, whose ObpCapture* routines probe before any
-- deref. The rest of this suite is their confirm-net.
--
-- For every pointer argument of each bridged OB syscall we hand the
-- kernel a kernel-range pointer (0x80000000, dword-aligned so the
-- range check -- not the alignment check -- is the rejecting
-- condition) in that one slot while every other argument stays valid,
-- then assert a clean error NTSTATUS. As with test.fuzz.se the deeper
-- assertion is survival: the in-process runner reaching t.summary()
-- means no probe regressed into a bugcheck.
--
-- NtSetInformationObject is not bridged in nt.dll.ob and is out of
-- scope here; its prologue audited clean (ProbeForRead precedes the
-- capture deref). Cover it when it is bridged.

local t      = require('test')
local ob     = require('nt.dll.ob')      -- registers the OB cdefs
local str    = require('nt.dll.str')
local err    = require('nt.dll.errors')
local ntdll  = require('nt.dll')
local ffi    = require('ffi')

-- First byte past MmUserProbeAddress. dword-aligned: the range check,
-- not the alignment check, is what must reject this.
local KERNEL_PTR = ffi.cast('void *', 0x80000000)

-- NtCurrentProcess() -- a valid pseudo-handle. The query syscalls
-- probe their caller pointers before referencing this handle, so it
-- is valid enough to reach the probe under test.
local CURRENT_PROCESS = ffi.cast('void *', -1)

t.suite("ob: hardening (kernel-range pointer-slot sweep)")

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
local function hslot() return ffi.new('HANDLE[1]') end
local function ulong() return ffi.new('ULONG[1]')  end
local function bytes(n) return ffi.new('unsigned char[?]', n or 256) end
local function oa()    return ffi.new('OBJECT_ATTRIBUTES[1]') end

-- ---- NtDuplicateObject -- OUT TargetHandle ----
-- TargetHandle is probed (ProbeForWriteHandle) before any source
-- handle is referenced, so bad pseudo-handles in the other slots are
-- irrelevant.

t.test("NtDuplicateObject rejects kernel-range TargetHandle", function()
    local st = err.normalize(ntdll.NtDuplicateObject(
        CURRENT_PROCESS, CURRENT_PROCESS, CURRENT_PROCESS,
        KERNEL_PTR, 0, 0, 0))
    rejects(st, "NtDuplicateObject/TargetHandle")
end)

-- ---- NtCreateDirectoryObject -- OUT DirectoryHandle, IN ObjectAttributes ----
-- DirectoryHandle is probed first; ObjectAttributes then flows into
-- ObCreateObject, whose ObpCaptureObjectAttributes probes it.

t.test("NtCreateDirectoryObject rejects kernel-range DirectoryHandle", function()
    local st = err.normalize(ntdll.NtCreateDirectoryObject(
        KERNEL_PTR, 0, oa()))
    rejects(st, "NtCreateDirectoryObject/DirectoryHandle")
end)

t.test("NtCreateDirectoryObject rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateDirectoryObject(
        hslot(), 0, KERNEL_PTR))
    rejects(st, "NtCreateDirectoryObject/ObjectAttributes")
end)

-- ---- NtOpenDirectoryObject -- OUT DirectoryHandle, IN ObjectAttributes ----

t.test("NtOpenDirectoryObject rejects kernel-range DirectoryHandle", function()
    local st = err.normalize(ntdll.NtOpenDirectoryObject(
        KERNEL_PTR, 0, oa()))
    rejects(st, "NtOpenDirectoryObject/DirectoryHandle")
end)

t.test("NtOpenDirectoryObject rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtOpenDirectoryObject(
        hslot(), 0, KERNEL_PTR))
    rejects(st, "NtOpenDirectoryObject/ObjectAttributes")
end)

-- ---- NtQueryDirectoryObject -- OUT Buffer, IN-OUT Context, OUT ReturnLength ----
-- Buffer, Context and ReturnLength are all probed in the prologue
-- before the directory handle is referenced.

t.test("NtQueryDirectoryObject rejects kernel-range Buffer", function()
    local st = err.normalize(ntdll.NtQueryDirectoryObject(
        CURRENT_PROCESS, KERNEL_PTR, 256, 0, 0, ulong(), ulong()))
    rejects(st, "NtQueryDirectoryObject/Buffer")
end)

t.test("NtQueryDirectoryObject rejects kernel-range Context", function()
    local st = err.normalize(ntdll.NtQueryDirectoryObject(
        CURRENT_PROCESS, bytes(256), 256, 0, 0, KERNEL_PTR, ulong()))
    rejects(st, "NtQueryDirectoryObject/Context")
end)

t.test("NtQueryDirectoryObject rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryDirectoryObject(
        CURRENT_PROCESS, bytes(256), 256, 0, 0, ulong(), KERNEL_PTR))
    rejects(st, "NtQueryDirectoryObject/ReturnLength")
end)

-- ---- NtCreateSymbolicLinkObject -- OUT LinkHandle, IN ObjectAttributes, IN LinkTarget ----
-- The prologue probes ObjectAttributes (the fix), then LinkTarget,
-- then LinkHandle -- so the poisoned slot is always rejected first.

t.test("NtCreateSymbolicLinkObject rejects kernel-range LinkHandle", function()
    local lt = str.new_utf16(16)
    local st = err.normalize(ntdll.NtCreateSymbolicLinkObject(
        KERNEL_PTR, 0, oa(), lt.us))
    rejects(st, "NtCreateSymbolicLinkObject/LinkHandle")
end)

-- Regression test for the OBLINK.C deref-before-probe fix. Before the
-- fix the prologue read ObjectAttributes->Attributes with no probe;
-- a kernel-range ObjectAttributes faulted past __try and bugchecked.
t.test("NtCreateSymbolicLinkObject rejects kernel-range ObjectAttributes", function()
    local lt = str.new_utf16(16)
    local st = err.normalize(ntdll.NtCreateSymbolicLinkObject(
        hslot(), 0, KERNEL_PTR, lt.us))
    rejects(st, "NtCreateSymbolicLinkObject/ObjectAttributes")
end)

t.test("NtCreateSymbolicLinkObject rejects kernel-range LinkTarget", function()
    local st = err.normalize(ntdll.NtCreateSymbolicLinkObject(
        hslot(), 0, oa(), KERNEL_PTR))
    rejects(st, "NtCreateSymbolicLinkObject/LinkTarget")
end)

-- ---- NtOpenSymbolicLinkObject -- OUT LinkHandle, IN ObjectAttributes ----

t.test("NtOpenSymbolicLinkObject rejects kernel-range LinkHandle", function()
    local st = err.normalize(ntdll.NtOpenSymbolicLinkObject(
        KERNEL_PTR, 0, oa()))
    rejects(st, "NtOpenSymbolicLinkObject/LinkHandle")
end)

t.test("NtOpenSymbolicLinkObject rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtOpenSymbolicLinkObject(
        hslot(), 0, KERNEL_PTR))
    rejects(st, "NtOpenSymbolicLinkObject/ObjectAttributes")
end)

-- ---- NtQuerySymbolicLinkObject -- IN-OUT LinkTarget, OUT ReturnedLength ----
-- LinkTarget is probed first; for the ReturnedLength slot a fully
-- valid UNICODE_STRING (real writable Buffer) lets the prologue reach
-- the ReturnedLength probe.

t.test("NtQuerySymbolicLinkObject rejects kernel-range LinkTarget", function()
    local st = err.normalize(ntdll.NtQuerySymbolicLinkObject(
        CURRENT_PROCESS, KERNEL_PTR, ulong()))
    rejects(st, "NtQuerySymbolicLinkObject/LinkTarget")
end)

t.test("NtQuerySymbolicLinkObject rejects kernel-range ReturnedLength", function()
    local lt = str.new_utf16(256)
    local st = err.normalize(ntdll.NtQuerySymbolicLinkObject(
        CURRENT_PROCESS, lt.us, KERNEL_PTR))
    rejects(st, "NtQuerySymbolicLinkObject/ReturnedLength")
end)

-- ---- NtQueryObject -- OUT ObjectInformation, OUT ReturnLength ----
-- Both are probed in the prologue; class 0 (ObjectBasicInformation)
-- just selects the probe alignment.

t.test("NtQueryObject rejects kernel-range ObjectInformation", function()
    local st = err.normalize(ntdll.NtQueryObject(
        CURRENT_PROCESS, 0, KERNEL_PTR, 256, ulong()))
    rejects(st, "NtQueryObject/ObjectInformation")
end)

t.test("NtQueryObject rejects kernel-range ReturnLength", function()
    local st = err.normalize(ntdll.NtQueryObject(
        CURRENT_PROCESS, 0, bytes(256), 256, KERNEL_PTR))
    rejects(st, "NtQueryObject/ReturnLength")
end)
