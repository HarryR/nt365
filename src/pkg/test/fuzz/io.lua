-- test.fuzz.io — kernel-range pointer-slot sweep for IO syscalls.
--
-- Part of the deref-before-probe sweep (the bug class written up as
-- NT-BUGS.md entry #5): a syscall that reads a field of an untrusted
-- caller pointer before ProbeForRead/Write -- or a self-probing
-- capture/probe helper -- has validated it. A kernel-range pointer
-- faults past __try and bugchecks 0x50/0x1E; only the probe's range
-- check, which rejects the pointer as data before any deref, stops it.
--
-- A dedicated P14 prologue audit of all 24 IO syscalls found the
-- subsystem clean: it routes every untrusted pointer through a
-- probe-and-capture helper (ProbeForWriteIoStatus, ProbeForRead/Write,
-- ProbeAndReadUnicodeString, ProbeAndReadUlong, ProbeAndReadLargeInteger)
-- before any field read. The create/open family funnels through
-- IoCreateFile, which probes FileHandle -> IoStatusBlock ->
-- AllocationSize -> EaBuffer in order and hands ObjectAttributes to
-- ObOpenObjectByName untouched. So IO needs no kernel fix; this suite
-- is the confirm-net that locks the clean audit in.
--
-- For every pointer argument of each bridged IO syscall we hand the
-- kernel a kernel-range pointer (0x80000000, dword-aligned so the
-- range check -- not the alignment check -- is the rejecting
-- condition) in that one slot while every other argument stays valid,
-- then assert a clean error NTSTATUS. As with test.fuzz.se the deeper
-- assertion is survival: the in-process runner reaching t.summary()
-- means no probe regressed into a bugcheck.
--
-- NtReadFile and NtWriteFile reference the file handle *before*
-- probing their pointer arguments, so the sweep needs a real
-- FILE-object handle as scaffolding -- \Device\Null, the cheapest one.
-- The unbridged IO syscalls (NtFsControlFile, the volume/EA query-set
-- pair, locks, NtLoadDriver, ...) are out of scope here; their
-- prologues audited clean too. Cover them when they are bridged.

local t      = require('test')
local fs     = require('nt.dll.fs')       -- registers the IO cdefs
local oa     = require('nt.dll.oa')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')
local ntdll  = require('nt.dll')
local ffi    = require('ffi')
local bit    = require('bit')

-- First byte past MmUserProbeAddress. dword-aligned: the range check,
-- not the alignment check, is what must reject this.
local KERNEL_PTR = ffi.cast('void *', 0x80000000)

t.suite("io: hardening (kernel-range pointer-slot sweep)")

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
local function hslot() return ffi.new('HANDLE[1]')           end
local function iosb()  return ffi.new('IO_STATUS_BLOCK[1]')  end
local function li()    return ffi.new('LARGE_INTEGER[1]')    end
local function ulong() return ffi.new('ULONG[1]')            end
local function bytes(n) return ffi.new('unsigned char[?]', n or 256) end

-- A valid OBJECT_ATTRIBUTES naming \Device\Null. oa.path(...).oa is
-- the proven idiom (see test/fs.lua); the .oa cdata anchors its own
-- backing memory.
local function valid_oa() return oa.path("\\Device\\Null").oa end

-- A real FILE-object handle for the handle-ref-before-probe syscalls.
-- Opened r+w so both NtReadFile and NtWriteFile clear their access
-- check and reach the pointer probe under test.
local null_h
do
    local ok, h = pcall(fs.NtOpenFile,
        bit.bor(fs.FILE_GENERIC_READ, fs.FILE_GENERIC_WRITE),
        oa.path("\\Device\\Null").oa,
        bit.bor(fs.FILE_SHARE_READ, fs.FILE_SHARE_WRITE),
        fs.FILE_SYNCHRONOUS_IO_NONALERT)
    if ok then null_h = h end
end

local function null_raw()
    assert(null_h,
           "test.fuzz.io: could not open \\Device\\Null scratch handle")
    return handle.raw(null_h)
end

local FILE_ATTRS = fs.FILE_ATTRIBUTE_NORMAL
local SHARE      = fs.FILE_SHARE_READ

-- ---- NtCreateFile -- OUT FileHandle, IN ObjectAttributes, OUT IoStatusBlock,
--                      IN AllocationSize, IN EaBuffer ----
-- IoCreateFile probes FileHandle -> IoStatusBlock -> AllocationSize ->
-- EaBuffer, then hands ObjectAttributes to ObOpenObjectByName.

t.test("NtCreateFile rejects kernel-range FileHandle", function()
    local st = err.normalize(ntdll.NtCreateFile(
        KERNEL_PTR, fs.FILE_GENERIC_READ, valid_oa(), iosb(), nil,
        FILE_ATTRS, SHARE, fs.FILE_OPEN, 0, nil, 0))
    rejects(st, "NtCreateFile/FileHandle")
end)

t.test("NtCreateFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtCreateFile(
        hslot(), fs.FILE_GENERIC_READ, valid_oa(), KERNEL_PTR, nil,
        FILE_ATTRS, SHARE, fs.FILE_OPEN, 0, nil, 0))
    rejects(st, "NtCreateFile/IoStatusBlock")
end)

t.test("NtCreateFile rejects kernel-range AllocationSize", function()
    local st = err.normalize(ntdll.NtCreateFile(
        hslot(), fs.FILE_GENERIC_READ, valid_oa(), iosb(), KERNEL_PTR,
        FILE_ATTRS, SHARE, fs.FILE_OPEN, 0, nil, 0))
    rejects(st, "NtCreateFile/AllocationSize")
end)

t.test("NtCreateFile rejects kernel-range EaBuffer", function()
    -- EaLength != 0 so IoCreateFile reaches the EaBuffer probe.
    local st = err.normalize(ntdll.NtCreateFile(
        hslot(), fs.FILE_GENERIC_READ, valid_oa(), iosb(), nil,
        FILE_ATTRS, SHARE, fs.FILE_OPEN, 0, KERNEL_PTR, 64))
    rejects(st, "NtCreateFile/EaBuffer")
end)

t.test("NtCreateFile rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateFile(
        hslot(), fs.FILE_GENERIC_READ, KERNEL_PTR, iosb(), nil,
        FILE_ATTRS, SHARE, fs.FILE_OPEN, 0, nil, 0))
    rejects(st, "NtCreateFile/ObjectAttributes")
end)

-- ---- NtOpenFile -- OUT FileHandle, IN ObjectAttributes, OUT IoStatusBlock ----

t.test("NtOpenFile rejects kernel-range FileHandle", function()
    local st = err.normalize(ntdll.NtOpenFile(
        KERNEL_PTR, fs.FILE_GENERIC_READ, valid_oa(), iosb(), SHARE, 0))
    rejects(st, "NtOpenFile/FileHandle")
end)

t.test("NtOpenFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtOpenFile(
        hslot(), fs.FILE_GENERIC_READ, valid_oa(), KERNEL_PTR, SHARE, 0))
    rejects(st, "NtOpenFile/IoStatusBlock")
end)

t.test("NtOpenFile rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtOpenFile(
        hslot(), fs.FILE_GENERIC_READ, KERNEL_PTR, iosb(), SHARE, 0))
    rejects(st, "NtOpenFile/ObjectAttributes")
end)

-- ---- NtCreateNamedPipeFile -- OUT FileHandle, IN ObjectAttributes,
--                               OUT IoStatusBlock, IN DefaultTimeout ----
-- The wrapper probes DefaultTimeout first; IoCreateFile then probes
-- FileHandle -> IoStatusBlock and hands off ObjectAttributes. The pipe
-- type/mode args are kept valid (0 = byte-stream / queue) so the
-- create-parameter validation does not reject before the probes.

t.test("NtCreateNamedPipeFile rejects kernel-range DefaultTimeout", function()
    local st = err.normalize(ntdll.NtCreateNamedPipeFile(
        hslot(), fs.FILE_GENERIC_READ, valid_oa(), iosb(), SHARE,
        fs.FILE_CREATE, 0, 0, 0, 0, 1, 4096, 4096, KERNEL_PTR))
    rejects(st, "NtCreateNamedPipeFile/DefaultTimeout")
end)

t.test("NtCreateNamedPipeFile rejects kernel-range FileHandle", function()
    local st = err.normalize(ntdll.NtCreateNamedPipeFile(
        KERNEL_PTR, fs.FILE_GENERIC_READ, valid_oa(), iosb(), SHARE,
        fs.FILE_CREATE, 0, 0, 0, 0, 1, 4096, 4096, nil))
    rejects(st, "NtCreateNamedPipeFile/FileHandle")
end)

t.test("NtCreateNamedPipeFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtCreateNamedPipeFile(
        hslot(), fs.FILE_GENERIC_READ, valid_oa(), KERNEL_PTR, SHARE,
        fs.FILE_CREATE, 0, 0, 0, 0, 1, 4096, 4096, nil))
    rejects(st, "NtCreateNamedPipeFile/IoStatusBlock")
end)

t.test("NtCreateNamedPipeFile rejects kernel-range ObjectAttributes", function()
    local st = err.normalize(ntdll.NtCreateNamedPipeFile(
        hslot(), fs.FILE_GENERIC_READ, KERNEL_PTR, iosb(), SHARE,
        fs.FILE_CREATE, 0, 0, 0, 0, 1, 4096, 4096, nil))
    rejects(st, "NtCreateNamedPipeFile/ObjectAttributes")
end)

-- ---- NtReadFile -- OUT IoStatusBlock, OUT Buffer, IN ByteOffset, IN Key ----
-- File handle is referenced before the probes, so a valid one is
-- required to reach the slot under test.

t.test("NtReadFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtReadFile(
        null_raw(), nil, nil, nil, KERNEL_PTR, bytes(64), 64, nil, nil))
    rejects(st, "NtReadFile/IoStatusBlock")
end)

t.test("NtReadFile rejects kernel-range Buffer", function()
    local st = err.normalize(ntdll.NtReadFile(
        null_raw(), nil, nil, nil, iosb(), KERNEL_PTR, 64, nil, nil))
    rejects(st, "NtReadFile/Buffer")
end)

t.test("NtReadFile rejects kernel-range ByteOffset", function()
    local st = err.normalize(ntdll.NtReadFile(
        null_raw(), nil, nil, nil, iosb(), bytes(64), 64, KERNEL_PTR, nil))
    rejects(st, "NtReadFile/ByteOffset")
end)

t.test("NtReadFile rejects kernel-range Key", function()
    local st = err.normalize(ntdll.NtReadFile(
        null_raw(), nil, nil, nil, iosb(), bytes(64), 64, li(), KERNEL_PTR))
    rejects(st, "NtReadFile/Key")
end)

-- ---- NtWriteFile -- OUT IoStatusBlock, IN Buffer, IN ByteOffset, IN Key ----

t.test("NtWriteFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtWriteFile(
        null_raw(), nil, nil, nil, KERNEL_PTR, bytes(64), 64, nil, nil))
    rejects(st, "NtWriteFile/IoStatusBlock")
end)

t.test("NtWriteFile rejects kernel-range Buffer", function()
    local st = err.normalize(ntdll.NtWriteFile(
        null_raw(), nil, nil, nil, iosb(), KERNEL_PTR, 64, nil, nil))
    rejects(st, "NtWriteFile/Buffer")
end)

t.test("NtWriteFile rejects kernel-range ByteOffset", function()
    local st = err.normalize(ntdll.NtWriteFile(
        null_raw(), nil, nil, nil, iosb(), bytes(64), 64, KERNEL_PTR, nil))
    rejects(st, "NtWriteFile/ByteOffset")
end)

t.test("NtWriteFile rejects kernel-range Key", function()
    local st = err.normalize(ntdll.NtWriteFile(
        null_raw(), nil, nil, nil, iosb(), bytes(64), 64, li(), KERNEL_PTR))
    rejects(st, "NtWriteFile/Key")
end)

-- ---- NtDeviceIoControlFile -- OUT IoStatusBlock, IN InputBuffer,
--                               OUT OutputBuffer ----
-- IoControlCode 0 selects METHOD_BUFFERED, so IopXxxControlFile probes
-- IoStatusBlock -> OutputBuffer -> InputBuffer before referencing the
-- handle.

t.test("NtDeviceIoControlFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtDeviceIoControlFile(
        null_raw(), nil, nil, nil, KERNEL_PTR, 0,
        bytes(16), 16, bytes(16), 16))
    rejects(st, "NtDeviceIoControlFile/IoStatusBlock")
end)

t.test("NtDeviceIoControlFile rejects kernel-range OutputBuffer", function()
    local st = err.normalize(ntdll.NtDeviceIoControlFile(
        null_raw(), nil, nil, nil, iosb(), 0,
        bytes(16), 16, KERNEL_PTR, 16))
    rejects(st, "NtDeviceIoControlFile/OutputBuffer")
end)

t.test("NtDeviceIoControlFile rejects kernel-range InputBuffer", function()
    local st = err.normalize(ntdll.NtDeviceIoControlFile(
        null_raw(), nil, nil, nil, iosb(), 0,
        KERNEL_PTR, 16, bytes(16), 16))
    rejects(st, "NtDeviceIoControlFile/InputBuffer")
end)

-- ---- NtQueryInformationFile -- OUT IoStatusBlock, OUT FileInformation ----
-- Both probed before the handle is referenced; class 5
-- (FileStandardInformation) just selects the post-probe branch.

t.test("NtQueryInformationFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtQueryInformationFile(
        null_raw(), KERNEL_PTR, bytes(64), 64, 5))
    rejects(st, "NtQueryInformationFile/IoStatusBlock")
end)

t.test("NtQueryInformationFile rejects kernel-range FileInformation", function()
    local st = err.normalize(ntdll.NtQueryInformationFile(
        null_raw(), iosb(), KERNEL_PTR, 64, 5))
    rejects(st, "NtQueryInformationFile/FileInformation")
end)

-- ---- NtSetInformationFile -- OUT IoStatusBlock, IN FileInformation ----

t.test("NtSetInformationFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtSetInformationFile(
        null_raw(), KERNEL_PTR, bytes(64), 64, 5))
    rejects(st, "NtSetInformationFile/IoStatusBlock")
end)

t.test("NtSetInformationFile rejects kernel-range FileInformation", function()
    local st = err.normalize(ntdll.NtSetInformationFile(
        null_raw(), iosb(), KERNEL_PTR, 64, 5))
    rejects(st, "NtSetInformationFile/FileInformation")
end)

-- ---- NtQueryDirectoryFile -- OUT IoStatusBlock, OUT FileInformation,
--                              IN FileName ----

t.test("NtQueryDirectoryFile rejects kernel-range IoStatusBlock", function()
    local st = err.normalize(ntdll.NtQueryDirectoryFile(
        null_raw(), nil, nil, nil, KERNEL_PTR, bytes(256), 256, 1,
        0, nil, 1))
    rejects(st, "NtQueryDirectoryFile/IoStatusBlock")
end)

t.test("NtQueryDirectoryFile rejects kernel-range FileInformation", function()
    local st = err.normalize(ntdll.NtQueryDirectoryFile(
        null_raw(), nil, nil, nil, iosb(), KERNEL_PTR, 256, 1,
        0, nil, 1))
    rejects(st, "NtQueryDirectoryFile/FileInformation")
end)

t.test("NtQueryDirectoryFile rejects kernel-range FileName", function()
    local st = err.normalize(ntdll.NtQueryDirectoryFile(
        null_raw(), nil, nil, nil, iosb(), bytes(256), 256, 1,
        0, KERNEL_PTR, 1))
    rejects(st, "NtQueryDirectoryFile/FileName")
end)

-- ---- NtQueryAttributesFile -- IN ObjectAttributes, OUT FileInformation ----
-- FileInformation is probed in the prologue; ObjectAttributes flows
-- into ObOpenObjectByName, whose ObpCaptureObjectAttributes probes it.

t.test("NtQueryAttributesFile rejects kernel-range FileInformation", function()
    local st = err.normalize(ntdll.NtQueryAttributesFile(
        valid_oa(), KERNEL_PTR))
    rejects(st, "NtQueryAttributesFile/FileInformation")
end)

t.test("NtQueryAttributesFile rejects kernel-range ObjectAttributes", function()
    local fbi = ffi.new('FILE_BASIC_INFORMATION[1]')
    local st = err.normalize(ntdll.NtQueryAttributesFile(
        KERNEL_PTR, fbi))
    rejects(st, "NtQueryAttributesFile/ObjectAttributes")
end)
