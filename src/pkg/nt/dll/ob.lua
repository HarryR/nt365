-- nt.dll.ob — Object Manager. Maps to NTOS/OB on the kernel side.
--
-- The NT Object Manager owns the namespace (\Device, \??, \Registry,
-- \BaseNamedObjects, \Driver, \FileSystem, ...) plus the handle table
-- primitives every other subsystem sits on top of. This sub-module
-- wraps:
--   - Handle lifetime:  NtClose, NtDuplicateObject,
--                       NtMakeTemporaryObject
--   - Directories:      Nt{Create,Open,Query}DirectoryObject
--   - Symbolic links:   Nt{Create,Open,Query}SymbolicLinkObject
--
-- Bridging follows the uniform convention (see nt.dll.ke for the rules);
-- NtQueryDirectoryObject returns STATUS_NO_MORE_ENTRIES (0x8000001A) at
-- end-of-enumeration — a WARNING-severity status that passes is_error
-- naturally, so the caller sees it instead of the wrapper raising.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local str    = require('nt.dll.str')
local handle = require('nt.dll.handle')   -- owns the NtClose cdef

ffi.cdef[[
NTSTATUS __stdcall NtDuplicateObject(HANDLE SourceProcess,
                                     HANDLE SourceHandle,
                                     HANDLE TargetProcess,
                                     HANDLE *TargetHandle,
                                     ULONG  DesiredAccess,
                                     ULONG  HandleAttributes,
                                     ULONG  Options);

NTSTATUS __stdcall NtMakeTemporaryObject(HANDLE Handle);

NTSTATUS __stdcall NtCreateDirectoryObject(HANDLE *DirectoryHandle,
                                           ULONG DesiredAccess,
                                           OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS __stdcall NtOpenDirectoryObject(HANDLE *DirectoryHandle,
                                         ULONG DesiredAccess,
                                         OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS __stdcall NtQueryDirectoryObject(HANDLE DirectoryHandle,
                                          void *Buffer,
                                          ULONG Length,
                                          unsigned char ReturnSingleEntry,
                                          unsigned char RestartScan,
                                          ULONG *Context,
                                          ULONG *ReturnLength);

NTSTATUS __stdcall NtCreateSymbolicLinkObject(HANDLE *LinkHandle,
                                              ULONG DesiredAccess,
                                              OBJECT_ATTRIBUTES *ObjectAttributes,
                                              UNICODE_STRING *LinkTarget);

NTSTATUS __stdcall NtOpenSymbolicLinkObject(HANDLE *LinkHandle,
                                            ULONG DesiredAccess,
                                            OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS __stdcall NtQuerySymbolicLinkObject(HANDLE LinkHandle,
                                             UNICODE_STRING *LinkTarget,
                                             ULONG *ReturnedLength);

NTSTATUS __stdcall NtQueryObject(HANDLE Handle,
                                 int ObjectInformationClass,
                                 void *ObjectInformation,
                                 ULONG ObjectInformationLength,
                                 ULONG *ReturnLength);

/* Per-entry record returned by NtQueryDirectoryObject. Array of these
 * plus variable-length wchar data for the Name/TypeName buffers all
 * live in the caller's output buffer (Shape 5). */
typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION;

typedef struct _OBJECT_BASIC_INFORMATION {
    ULONG         Attributes;
    ULONG         GrantedAccess;
    ULONG         HandleCount;
    ULONG         PointerCount;
    ULONG         PagedPoolUsage;
    ULONG         NonPagedPoolUsage;
    ULONG         Reserved[3];
    ULONG         NameInfoSize;
    ULONG         TypeInfoSize;
    ULONG         SecurityDescriptorSize;
    LARGE_INTEGER CreationTime;
} OBJECT_BASIC_INFORMATION;

typedef struct _OBJECT_NAME_INFORMATION {
    UNICODE_STRING Name;
    /* variable-length wchar_t buffer follows */
} OBJECT_NAME_INFORMATION;

typedef struct _OBJECT_TYPE_INFORMATION {
    UNICODE_STRING Name;
    ULONG          TotalNumberOfObjects;
    ULONG          TotalNumberOfHandles;
    ULONG          TotalPagedPoolUsage;
    ULONG          TotalNonPagedPoolUsage;
    ULONG          TotalNamePoolUsage;
    ULONG          TotalHandleTableUsage;
    ULONG          HighWaterNumberOfObjects;
    ULONG          HighWaterNumberOfHandles;
    ULONG          HighWaterPagedPoolUsage;
    ULONG          HighWaterNonPagedPoolUsage;
    ULONG          HighWaterNamePoolUsage;
    ULONG          HighWaterHandleTableUsage;
    ULONG          InvalidAttributes;
    ULONG          GenericMapping[4];
    ULONG          ValidAccessMask;
    unsigned char  SecurityRequired;
    unsigned char  MaintainHandleCount;
    USHORT         Reserved1;
    ULONG          PoolType;
    ULONG          DefaultPagedPoolCharge;
    ULONG          DefaultNonPagedPoolCharge;
} OBJECT_TYPE_INFORMATION;
]]

local M = {}

-- Explicit close of an NT_HANDLE. We detach (clear ownership) before
-- the syscall so the wrapper's __gc skips this handle when collected
-- later — no double-close.
function M.NtClose(h)
    if not ffi.istype('NT_HANDLE', h) then
        error("NtClose: expected NT_HANDLE, got " .. tostring(h), 2)
    end
    local raw = h:detach()
    local st = ntdll.NtClose(raw)
    if err.is_error(st) then err.raise('NtClose', st) end
end

function M.NtDuplicateObject(src_process, src_handle, tgt_process,
                             access, attributes, options)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtDuplicateObject(handle.raw(src_process),
                                       handle.raw(src_handle),
                                       handle.raw(tgt_process), h,
                                       access, attributes, options)
    if err.is_error(st) then err.raise('NtDuplicateObject', st) end
    return handle.wrap(h[0])
end

function M.NtMakeTemporaryObject(h)
    local st = ntdll.NtMakeTemporaryObject(handle.raw(h))
    if err.is_error(st) then err.raise('NtMakeTemporaryObject', st) end
end

function M.NtCreateDirectoryObject(access, oa)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateDirectoryObject(h, access, oa)
    if err.is_error(st) then err.raise('NtCreateDirectoryObject', st) end
    return handle.wrap(h[0])
end

function M.NtOpenDirectoryObject(access, oa)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenDirectoryObject(h, access, oa)
    if err.is_error(st) then err.raise('NtOpenDirectoryObject', st) end
    return handle.wrap(h[0])
end

-- Enumerate a directory object. Caller owns `buffer` (the kernel fills
-- it with OBJECT_DIRECTORY_INFORMATION entries) and `context` (a
-- ULONG* whose kernel-maintained state tracks position across calls —
-- allocate once, pass the same pointer repeatedly, set restart_scan
-- on the first call).
--
-- Returns (bytes_written, status). status is 0 on success, or
-- STATUS_NO_MORE_ENTRIES (0x8000001A, a WARNING severity so it passes
-- the is_error check) when the directory is exhausted.
function M.NtQueryDirectoryObject(dir, buffer, length,
                                  single_entry, restart_scan, context)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryDirectoryObject(handle.raw(dir), buffer, length,
                                            single_entry and 1 or 0,
                                            restart_scan and 1 or 0,
                                            context, ret)
    if err.is_error(st) then err.raise('NtQueryDirectoryObject', st) end
    return ret[0], err.normalize(st)
end

function M.NtCreateSymbolicLinkObject(access, oa, target)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateSymbolicLinkObject(h, access, oa, target)
    if err.is_error(st) then err.raise('NtCreateSymbolicLinkObject', st) end
    return handle.wrap(h[0])
end

function M.NtOpenSymbolicLinkObject(access, oa)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenSymbolicLinkObject(h, access, oa)
    if err.is_error(st) then err.raise('NtOpenSymbolicLinkObject', st) end
    return handle.wrap(h[0])
end

-- Read a symbolic link's target. Returns a UTF-8 Lua string.
-- Uses a fused NT_STRING output buffer so the UNICODE_STRING and its
-- wbuf share one cdata — no Shape 1 aliasing between the syscall and
-- the str.from_utf16 read.
--
-- 1024 wchar capacity comfortably fits every symlink target seen so
-- far under NT 3.5. For the rare long-target case, drop to
-- `require('nt.dll')` and drive NtQuerySymbolicLinkObject manually
-- against a larger caller-owned buffer.
function M.NtQuerySymbolicLinkObject(h)
    local ns = str.new_utf16(1024)
    local st = ntdll.NtQuerySymbolicLinkObject(handle.raw(h), ns.us, nil)
    if err.is_error(st) then err.raise('NtQuerySymbolicLinkObject', st) end
    return str.from_utf16(ns.us)
end

-- Query generic object info. Size-query capable: caller provides buffer
-- and length, we return (bytes_written, status). STATUS_INFO_LENGTH_MISMATCH
-- (0xC0000004) is an error-severity NTSTATUS, so passes through is_error
-- and raises — the caller should size appropriately up front.
--
-- info_class values (OBJECT_INFORMATION_CLASS):
--   0 = ObjectBasicInformation  → OBJECT_BASIC_INFORMATION
--   1 = ObjectNameInformation   → OBJECT_NAME_INFORMATION + wchar_t buffer
--   2 = ObjectTypeInformation   → OBJECT_TYPE_INFORMATION + wchar_t buffer
function M.NtQueryObject(h, info_class, buffer, length)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryObject(handle.raw(h), info_class,
                                   buffer, length, ret)
    if err.is_error(st) then err.raise('NtQueryObject', st) end
    return ret[0], err.normalize(st)
end

return M

-- ----------------------------------------------------------------------
-- TODO — NT Object Manager syscalls not yet bridged. Add when a real
-- caller reaches.
--
-- Size-query pattern (caller provides buffer; kernel fills and reports
-- bytes used. Don't fit the uniform wrapper — hand-roll helpers or
-- leave for raw `ntdll.<Foo>` access):
--   NtSetInformationObject       set handle inherit/protect flags
--
-- Security / audit (NTOS/SE territory, exported by OB for handle-scoped calls):
--   NtCloseObjectAuditAlarm
--
