-- nt.dll.mm — Memory Manager. Maps to NTOS/MM on the kernel side.
-- Virtual-memory ops (alloc/free/protect/query, cross-process
-- read/write) and the create/map/unmap half of sections. Open/query
-- for existing sections lives in nt.dll.ex (Section is shelved there
-- next to the other executive sync-primitive openers).
--
-- ProcessHandle conventions:
--   - Pass nil for "current process" — the wrapper substitutes the
--     (HANDLE)-1 pseudo-handle NT uses for self-reference.
--   - Pass an NT_HANDLE from ps.NtOpenProcess for cross-process.
--
-- BaseAddress conventions:
--   - Allocation: pass nil for "any address"; kernel writes the
--     chosen base into the returned first value.
--   - Free / Unmap / Query: pass the cdata pointer previously returned
--     by the matching allocation / map call.
--
-- LARGE_INTEGER size parameters (section MaximumSize, section offset)
-- are taken as Lua numbers in bytes; the wrapper stamps them into a
-- LARGE_INTEGER whose lifetime is scoped to the syscall.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')

ffi.cdef[[
#pragma pack(push, 4)

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID  BaseAddress;
    PVOID  AllocationBase;
    ULONG  AllocationProtect;
    ULONG  RegionSize;
    ULONG  State;
    ULONG  Protect;
    ULONG  Type;
} MEMORY_BASIC_INFORMATION;

#pragma pack(pop)

NTSTATUS __stdcall NtAllocateVirtualMemory(HANDLE ProcessHandle,
                                           void **BaseAddress,
                                           ULONG ZeroBits,
                                           ULONG *RegionSize,
                                           ULONG AllocationType,
                                           ULONG Protect);

NTSTATUS __stdcall NtFreeVirtualMemory(HANDLE ProcessHandle,
                                       void **BaseAddress,
                                       ULONG *RegionSize,
                                       ULONG FreeType);

NTSTATUS __stdcall NtProtectVirtualMemory(HANDLE ProcessHandle,
                                          void **BaseAddress,
                                          ULONG *NumberOfBytesToProtect,
                                          ULONG NewAccessProtection,
                                          ULONG *OldAccessProtection);

NTSTATUS __stdcall NtQueryVirtualMemory(HANDLE ProcessHandle,
                                        void *BaseAddress,
                                        int MemoryInformationClass,
                                        void *MemoryInformation,
                                        ULONG MemoryInformationLength,
                                        ULONG *ReturnLength);

NTSTATUS __stdcall NtReadVirtualMemory(HANDLE ProcessHandle,
                                       void *BaseAddress,
                                       void *Buffer,
                                       ULONG NumberOfBytesToRead,
                                       ULONG *NumberOfBytesRead);

NTSTATUS __stdcall NtWriteVirtualMemory(HANDLE ProcessHandle,
                                        void *BaseAddress,
                                        void *Buffer,
                                        ULONG NumberOfBytesToWrite,
                                        ULONG *NumberOfBytesWritten);

NTSTATUS __stdcall NtFlushVirtualMemory(HANDLE ProcessHandle,
                                        void **BaseAddress,
                                        ULONG *NumberOfBytesToFlush,
                                        IO_STATUS_BLOCK *IoStatusBlock);

NTSTATUS __stdcall NtLockVirtualMemory(HANDLE ProcessHandle,
                                       void **BaseAddress,
                                       ULONG *NumberOfBytesToLock,
                                       ULONG LockType);

NTSTATUS __stdcall NtUnlockVirtualMemory(HANDLE ProcessHandle,
                                         void **BaseAddress,
                                         ULONG *NumberOfBytesToUnlock,
                                         ULONG LockType);

NTSTATUS __stdcall NtCreateSection(HANDLE *SectionHandle,
                                   ULONG DesiredAccess,
                                   OBJECT_ATTRIBUTES *ObjectAttributes,
                                   LARGE_INTEGER *MaximumSize,
                                   ULONG SectionPageProtection,
                                   ULONG AllocationAttributes,
                                   HANDLE FileHandle);

NTSTATUS __stdcall NtMapViewOfSection(HANDLE SectionHandle,
                                      HANDLE ProcessHandle,
                                      void **BaseAddress,
                                      ULONG ZeroBits,
                                      ULONG CommitSize,
                                      LARGE_INTEGER *SectionOffset,
                                      ULONG *ViewSize,
                                      int InheritDisposition,
                                      ULONG AllocationType,
                                      ULONG Win32Protect);

NTSTATUS __stdcall NtUnmapViewOfSection(HANDLE ProcessHandle,
                                        void *BaseAddress);

NTSTATUS __stdcall NtExtendSection(HANDLE SectionHandle,
                                   LARGE_INTEGER *NewSectionSize);
]]

local M = {}

-- AllocationType / FreeType (MEM_*)
M.MEM_COMMIT       = 0x00001000
M.MEM_RESERVE      = 0x00002000
M.MEM_DECOMMIT     = 0x00004000
M.MEM_RELEASE      = 0x00008000
M.MEM_TOP_DOWN     = 0x00100000
M.MEM_RESET        = 0x00080000

-- Page protection (PAGE_*)
M.PAGE_NOACCESS          = 0x01
M.PAGE_READONLY          = 0x02
M.PAGE_READWRITE         = 0x04
M.PAGE_WRITECOPY         = 0x08
M.PAGE_EXECUTE           = 0x10
M.PAGE_EXECUTE_READ      = 0x20
M.PAGE_EXECUTE_READWRITE = 0x40
M.PAGE_EXECUTE_WRITECOPY = 0x80
M.PAGE_GUARD             = 0x100
M.PAGE_NOCACHE           = 0x200

-- Section allocation attributes (SEC_*)
M.SEC_BASED          = 0x00200000
M.SEC_NO_CHANGE      = 0x00400000
M.SEC_FILE           = 0x00800000
M.SEC_IMAGE          = 0x01000000
M.SEC_RESERVE        = 0x04000000
M.SEC_COMMIT         = 0x08000000
M.SEC_NOCACHE        = 0x10000000

-- Section access rights (on top of STANDARD_RIGHTS_*)
M.SECTION_QUERY       = 0x0001
M.SECTION_MAP_WRITE   = 0x0002
M.SECTION_MAP_READ    = 0x0004
M.SECTION_MAP_EXECUTE = 0x0008
M.SECTION_EXTEND_SIZE = 0x0010
M.SECTION_ALL_ACCESS  = 0x000F001F

-- SECTION_INHERIT enum for NtMapViewOfSection
M.ViewShare = 1
M.ViewUnmap = 2

-- MEMORY_INFORMATION_CLASS
M.MemoryBasicInformation = 0

-- Region State (returned in MEMORY_BASIC_INFORMATION.State)
M.MEM_FREE      = 0x10000
-- MEM_COMMIT / MEM_RESERVE share values with AllocationType

-- Region Type (MEMORY_BASIC_INFORMATION.Type)
M.MEM_PRIVATE   = 0x20000
M.MEM_MAPPED    = 0x40000
M.MEM_IMAGE     = 0x1000000

-- Pseudo-handle for the current process. NT's NtCurrentProcess() is
-- the kernel-internal (HANDLE)-1; ntdll re-exports this as an inline
-- wherever needed. Keep a precomputed cdata so every helper doesn't
-- re-cast.
local CURRENT_PROCESS = ffi.cast('HANDLE', ffi.cast('intptr_t', -1))

-- Resolve a process-handle argument to a raw HANDLE. nil → current
-- process pseudo-handle; NT_HANDLE → its kernel handle value.
local function proc_raw(h)
    if h == nil then return CURRENT_PROCESS end
    return handle.raw(h)
end

M.NtCurrentProcess = function() return CURRENT_PROCESS end

-- ------------------------------------------------------------------
-- Virtual memory
-- ------------------------------------------------------------------

-- Allocate virtual memory in the target process. Returns (base, size)
-- — both rounded up to the allocation granularity by the kernel.
-- `base_hint` may be nil (any address) or a cdata pointer for a fixed
-- placement. `alloc_type` is MEM_COMMIT | MEM_RESERVE (common) or just
-- MEM_COMMIT to bring a prior reservation into memory. `protect` is
-- one of the PAGE_* constants.
function M.NtAllocateVirtualMemory(proc, base_hint, size, alloc_type, protect)
    local ba = ffi.new('void *[1]')
    ba[0]    = base_hint
    local sz = ffi.new('ULONG[1]')
    sz[0]    = size
    local st = ntdll.NtAllocateVirtualMemory(proc_raw(proc), ba, 0,
                                             sz, alloc_type, protect)
    if err.is_error(st) then err.raise('NtAllocateVirtualMemory', st) end
    return ba[0], sz[0]
end

-- Free a region. For MEM_RELEASE the size must be 0 and base must be
-- the exact value from a prior allocate. For MEM_DECOMMIT you may
-- decommit a sub-region.
function M.NtFreeVirtualMemory(proc, base, size, free_type)
    local ba = ffi.new('void *[1]')
    ba[0]    = base
    local sz = ffi.new('ULONG[1]')
    sz[0]    = size or 0
    local st = ntdll.NtFreeVirtualMemory(proc_raw(proc), ba, sz, free_type)
    if err.is_error(st) then err.raise('NtFreeVirtualMemory', st) end
end

-- Change page protection on a region. Returns the previous protection.
function M.NtProtectVirtualMemory(proc, base, size, new_protect)
    local ba = ffi.new('void *[1]')
    ba[0]    = base
    local sz = ffi.new('ULONG[1]')
    sz[0]    = size
    local old = ffi.new('ULONG[1]')
    local st = ntdll.NtProtectVirtualMemory(proc_raw(proc), ba, sz,
                                            new_protect, old)
    if err.is_error(st) then err.raise('NtProtectVirtualMemory', st) end
    return old[0]
end

-- Query memory info at `base`. Returns a filled MEMORY_BASIC_INFORMATION
-- cdata (class 0). Other info classes can be driven via raw ntdll.
function M.NtQueryVirtualMemory_Basic(proc, base)
    local info = ffi.new('MEMORY_BASIC_INFORMATION')
    local ret  = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryVirtualMemory(proc_raw(proc), base,
                                          M.MemoryBasicInformation,
                                          info,
                                          ffi.sizeof('MEMORY_BASIC_INFORMATION'),
                                          ret)
    if err.is_error(st) then err.raise('NtQueryVirtualMemory', st) end
    return info
end

-- Generic query. caller-owned buffer + length, receives (bytes_written).
function M.NtQueryVirtualMemory(proc, base, info_class, buffer, length)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryVirtualMemory(proc_raw(proc), base,
                                          info_class, buffer, length, ret)
    if err.is_error(st) then err.raise('NtQueryVirtualMemory', st) end
    return ret[0]
end

-- Read from another process's address space. `buffer` is caller-owned
-- in this process. Returns the number of bytes actually read.
function M.NtReadVirtualMemory(proc, remote_base, buffer, size)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtReadVirtualMemory(proc_raw(proc), remote_base,
                                         buffer, size, ret)
    if err.is_error(st) then err.raise('NtReadVirtualMemory', st) end
    return ret[0]
end

-- Write into another process's address space.
function M.NtWriteVirtualMemory(proc, remote_base, buffer, size)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtWriteVirtualMemory(proc_raw(proc), remote_base,
                                          buffer, size, ret)
    if err.is_error(st) then err.raise('NtWriteVirtualMemory', st) end
    return ret[0]
end

function M.NtFlushVirtualMemory(proc, base, size)
    local ba = ffi.new('void *[1]')
    ba[0]    = base
    local sz = ffi.new('ULONG[1]')
    sz[0]    = size
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtFlushVirtualMemory(proc_raw(proc), ba, sz, iosb)
    if err.is_error(st) then err.raise('NtFlushVirtualMemory', st) end
    return iosb.Information
end

function M.NtLockVirtualMemory(proc, base, size, lock_type)
    local ba = ffi.new('void *[1]')
    ba[0]    = base
    local sz = ffi.new('ULONG[1]')
    sz[0]    = size
    local st = ntdll.NtLockVirtualMemory(proc_raw(proc), ba, sz, lock_type or 1)
    if err.is_error(st) then err.raise('NtLockVirtualMemory', st) end
end

function M.NtUnlockVirtualMemory(proc, base, size, lock_type)
    local ba = ffi.new('void *[1]')
    ba[0]    = base
    local sz = ffi.new('ULONG[1]')
    sz[0]    = size
    local st = ntdll.NtUnlockVirtualMemory(proc_raw(proc), ba, sz, lock_type or 1)
    if err.is_error(st) then err.raise('NtUnlockVirtualMemory', st) end
end

-- ------------------------------------------------------------------
-- Sections (create / map / unmap / extend)
-- ------------------------------------------------------------------

-- Create a new section. `oa` is optional (named vs anonymous).
-- `max_size` may be nil when backing a file (the file's size is used);
-- required as a Lua number of bytes for anonymous sections. `page_prot`
-- is one of PAGE_*. `alloc_attrs` is SEC_* (SEC_COMMIT | PAGE_READWRITE
-- is the usual "anonymous shared RAM" recipe). `file_handle` is nil
-- for pagefile-backed, or an NT_HANDLE from fs.NtOpenFile/CreateFile
-- to back the section by a file.
function M.NtCreateSection(access, oa, max_size, page_prot, alloc_attrs,
                           file_handle)
    local h    = ffi.new('HANDLE[1]')
    local size = ffi.new('LARGE_INTEGER')
    if max_size then size.QuadPart = max_size end
    local st = ntdll.NtCreateSection(h, access, oa,
                                     max_size and size or nil,
                                     page_prot, alloc_attrs,
                                     file_handle and handle.raw(file_handle) or nil)
    if err.is_error(st) then err.raise('NtCreateSection', st) end
    return handle.wrap(h[0])
end

-- Map a view of a section into a process's address space. Returns
-- (base, view_size). `base_hint`/`commit_size`/`section_offset` may
-- each be nil for "kernel picks". `view_size` may be 0 to map from
-- offset through the end of the section. `inherit` defaults to
-- ViewUnmap (don't inherit); ViewShare if you want child processes
-- to get the same mapping. `protect` is PAGE_*.
function M.NtMapViewOfSection(section, proc, base_hint, commit_size,
                              section_offset, view_size,
                              inherit, alloc_type, protect)
    local ba = ffi.new('void *[1]')
    ba[0]    = base_hint
    local off = ffi.new('LARGE_INTEGER')
    if section_offset then off.QuadPart = section_offset end
    local vs = ffi.new('ULONG[1]')
    vs[0]    = view_size or 0
    local st = ntdll.NtMapViewOfSection(handle.raw(section),
                                        proc_raw(proc), ba, 0,
                                        commit_size or 0,
                                        section_offset and off or nil,
                                        vs,
                                        inherit or M.ViewUnmap,
                                        alloc_type or 0,
                                        protect)
    if err.is_error(st) then err.raise('NtMapViewOfSection', st) end
    return ba[0], vs[0]
end

-- Unmap a previously-mapped view. `base` is exactly what
-- NtMapViewOfSection returned.
function M.NtUnmapViewOfSection(proc, base)
    local st = ntdll.NtUnmapViewOfSection(proc_raw(proc), base)
    if err.is_error(st) then err.raise('NtUnmapViewOfSection', st) end
end

-- Grow a section (file-backed only in general). `new_size` in bytes.
-- Returns the size the kernel settled on (may be larger than requested
-- due to granularity).
function M.NtExtendSection(section, new_size)
    local size = ffi.new('LARGE_INTEGER')
    size.QuadPart = new_size
    local st = ntdll.NtExtendSection(handle.raw(section), size)
    if err.is_error(st) then err.raise('NtExtendSection', st) end
    return size.QuadPart
end

return M
