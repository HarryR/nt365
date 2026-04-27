-- nt.dll.fs — file and device I/O. Maps to NTOS/IO on the kernel side.
--
-- Bridging pattern is the same as nt.dll.ke (OUT→return, NTSTATUS<0
-- → raise), with a few fs-specific shapes:
--
--   NtCreateFile / NtOpenFile
--     Two OUT args: the file handle and the IoStatusBlock. Wrapper
--     returns (handle, disposition). Disposition is iosb.Information
--     — FILE_CREATED=2, FILE_OPENED=1, FILE_OVERWRITTEN=3, etc.
--     Callers that only need the handle: `local h = fs.NtOpenFile(...)`.
--
--   NtReadFile
--     Returns (bytes_read, status). status is STATUS_SUCCESS (0) on
--     full read, STATUS_END_OF_FILE (0xC0000011) when the caller has
--     hit EOF (possibly with a partial read — bytes_read may be > 0
--     even on EOF). STATUS_END_OF_FILE is *not* raised since EOF is
--     expected in read loops; any other NTSTATUS<0 is raised.
--
--   NtWriteFile / NtDeviceIoControlFile
--     Return bytes_written (iosb.Information). Raise on NTSTATUS<0.
--
-- File handles from this module close via NtClose in nt.dll.ob:
--     require('nt.dll.ob').NtClose(h)

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')
local str    = require('nt.dll.str')

-- Pack(4) for LARGE_INTEGER-bearing structs; see nt.dll.sys for the
-- NT 3.5 alignment discussion.
ffi.cdef[[
#pragma pack(push, 4)
typedef struct _FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG         FileAttributes;
} FILE_BASIC_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG         NumberOfLinks;
    unsigned char DeletePending;
    unsigned char Directory;
} FILE_STANDARD_INFORMATION;

typedef struct _FILE_DIRECTORY_INFORMATION {
    ULONG         NextEntryOffset;
    ULONG         FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG         FileAttributes;
    ULONG         FileNameLength;
    wchar_t       FileName[1];
} FILE_DIRECTORY_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION {
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION {
    unsigned char DeleteFile;
} FILE_DISPOSITION_INFORMATION;

#pragma pack(pop)

/* FILE_RENAME_INFORMATION is variable-length: header + inline FileName.
 * Trailing wchar_t[?] flexes via ffi.new('FILE_RENAME_INFORMATION', n);
 * the result is a single cdata whose lifetime covers both the header
 * and the name buffer — same fused-allocation pattern as NT_STRING and
 * NT_OA_PATH, no cast-pointer aliasing.
 *
 * Pack is 2 (NOT 4) because NT 3.5's kernel struct layout for this
 * specific type uses pack(2) — empirically verified via DbgPrint of
 * FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) returning 10 (not
 * 12). With pack(2), HANDLE goes at offset 2 (1-byte pad after the
 * BOOLEAN), ULONG at offset 6, FileName at offset 10. With pack(4)
 * we'd put HANDLE at 4 / FileName at 12, and the kernel would
 * misread our FileNameLength field as 0x00360000 = 3,538,944 (the
 * low byte of our actual FileNameLength shifted into the high byte
 * of the kernel's expected position). The validation
 * `FIELD_OFFSET(FileName) + FileNameLength <= Length` blows up with
 * STATUS_INVALID_PARAMETER. Other fs structs in this module are
 * pack(4)-safe because they don't have a small-then-large field
 * pairing that creates pack-dependent padding. */
#pragma pack(push, 2)
typedef struct _FILE_RENAME_INFORMATION {
    unsigned char ReplaceIfExists;
    HANDLE        RootDirectory;
    ULONG         FileNameLength;   /* bytes, not wchars */
    wchar_t       FileName[?];
} FILE_RENAME_INFORMATION;
#pragma pack(pop)

NTSTATUS __stdcall NtCreateFile(HANDLE *FileHandle,
                                ULONG DesiredAccess,
                                OBJECT_ATTRIBUTES *ObjectAttributes,
                                IO_STATUS_BLOCK *IoStatusBlock,
                                LARGE_INTEGER *AllocationSize,
                                ULONG FileAttributes,
                                ULONG ShareAccess,
                                ULONG CreateDisposition,
                                ULONG CreateOptions,
                                void *EaBuffer,
                                ULONG EaLength);

NTSTATUS __stdcall NtQueryInformationFile(HANDLE FileHandle,
                                          IO_STATUS_BLOCK *IoStatusBlock,
                                          void *FileInformation,
                                          ULONG Length,
                                          int FileInformationClass);

NTSTATUS __stdcall NtSetInformationFile(HANDLE FileHandle,
                                        IO_STATUS_BLOCK *IoStatusBlock,
                                        void *FileInformation,
                                        ULONG Length,
                                        int FileInformationClass);

NTSTATUS __stdcall NtQueryDirectoryFile(HANDLE FileHandle,
                                        HANDLE Event,
                                        void *ApcRoutine,
                                        void *ApcContext,
                                        IO_STATUS_BLOCK *IoStatusBlock,
                                        void *FileInformation,
                                        ULONG Length,
                                        int FileInformationClass,
                                        unsigned char ReturnSingleEntry,
                                        UNICODE_STRING *FileName,
                                        unsigned char RestartScan);

NTSTATUS __stdcall NtOpenFile  (HANDLE *FileHandle,
                                ULONG DesiredAccess,
                                OBJECT_ATTRIBUTES *ObjectAttributes,
                                IO_STATUS_BLOCK *IoStatusBlock,
                                ULONG ShareAccess,
                                ULONG OpenOptions);

NTSTATUS __stdcall NtReadFile  (HANDLE FileHandle,
                                HANDLE Event,
                                void *ApcRoutine,
                                void *ApcContext,
                                IO_STATUS_BLOCK *IoStatusBlock,
                                void *Buffer,
                                ULONG Length,
                                LARGE_INTEGER *ByteOffset,
                                ULONG *Key);

NTSTATUS __stdcall NtWriteFile (HANDLE FileHandle,
                                HANDLE Event,
                                void *ApcRoutine,
                                void *ApcContext,
                                IO_STATUS_BLOCK *IoStatusBlock,
                                void *Buffer,
                                ULONG Length,
                                LARGE_INTEGER *ByteOffset,
                                ULONG *Key);

NTSTATUS __stdcall NtDeviceIoControlFile(HANDLE FileHandle,
                                         HANDLE Event,
                                         void *ApcRoutine,
                                         void *ApcContext,
                                         IO_STATUS_BLOCK *IoStatusBlock,
                                         ULONG IoControlCode,
                                         void *InputBuffer,
                                         ULONG InputBufferLength,
                                         void *OutputBuffer,
                                         ULONG OutputBufferLength);
]]

-- STATUS_END_OF_FILE has severity ERROR, so the uniform is_error
-- check would raise on it. EOF is expected in read loops though, so
-- NtReadFile special-cases it through. Constant lives in M (publicly
-- exposed so callers can match against the status we return).

local M = {}

-- ------------------------------------------------------------------
-- File-API constants — re-exported so callers (io.lua, os.lua, tests)
-- don't have to re-derive them from MSDN-style hex literals. Values
-- are stable across NT versions; sourced from NT/PUBLIC/SDK/INC/NTIOAPI.H
-- and WINNT.H.
-- ------------------------------------------------------------------

-- Access masks — composite "GENERIC" forms include SYNCHRONIZE, so
-- callers don't need to OR it in separately for synchronous handles.
M.FILE_GENERIC_READ            = 0x00120089
M.FILE_GENERIC_WRITE           = 0x00120116

-- Standard rights commonly OR'd with the per-class bits above.
M.DELETE                       = 0x00010000
M.SYNCHRONIZE                  = 0x00100000

-- ShareAccess.
M.FILE_SHARE_READ              = 0x00000001
M.FILE_SHARE_WRITE             = 0x00000002
M.FILE_SHARE_DELETE            = 0x00000004

-- CreateDisposition.
M.FILE_SUPERSEDE               = 0
M.FILE_OPEN                    = 1
M.FILE_CREATE                  = 2
M.FILE_OPEN_IF                 = 3
M.FILE_OVERWRITE               = 4
M.FILE_OVERWRITE_IF            = 5

-- CreateOptions.
M.FILE_DIRECTORY_FILE          = 0x00000001
M.FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
M.FILE_NON_DIRECTORY_FILE      = 0x00000040
M.FILE_DELETE_ON_CLOSE         = 0x00001000

-- FileAttributes (NtCreateFile FileAttributes argument).
M.FILE_ATTRIBUTE_NORMAL        = 0x00000080
M.FILE_ATTRIBUTE_DIRECTORY     = 0x00000010

-- Pseudo-status codes used at the read boundary. STATUS_END_OF_FILE
-- (severity ERROR) is special-cased through NtReadFile rather than
-- raised, so callers that want to detect EOF need this constant.
M.STATUS_END_OF_FILE           = 0xC0000011
M.STATUS_NO_MORE_FILES         = 0x80000006

function M.NtCreateFile(access, oa, alloc_size, file_attrs, share,
                        disposition, options, ea_buffer, ea_length)
    local h    = ffi.new('HANDLE[1]')
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st   = ntdll.NtCreateFile(h, access, oa, iosb,
                                    alloc_size, file_attrs, share,
                                    disposition, options,
                                    ea_buffer, ea_length or 0)
    if err.is_error(st) then err.raise('NtCreateFile', st) end
    return handle.wrap(h[0]), iosb.Information
end

function M.NtOpenFile(access, oa, share, options)
    local h    = ffi.new('HANDLE[1]')
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st   = ntdll.NtOpenFile(h, access, oa, iosb, share, options)
    if err.is_error(st) then err.raise('NtOpenFile', st) end
    return handle.wrap(h[0]), iosb.Information
end

function M.NtReadFile(h, buffer, length, byte_offset)
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtReadFile(handle.raw(h), nil, nil, nil, iosb,
                                buffer, length, byte_offset, nil)
    local stu = err.normalize(st)
    if err.is_error(st) and stu ~= M.STATUS_END_OF_FILE then
        err.raise('NtReadFile', st)
    end
    return iosb.Information, stu
end

function M.NtWriteFile(h, buffer, length, byte_offset)
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtWriteFile(handle.raw(h), nil, nil, nil, iosb,
                                 buffer, length, byte_offset, nil)
    if err.is_error(st) then err.raise('NtWriteFile', st) end
    return iosb.Information
end

function M.NtDeviceIoControlFile(h, ioctl,
                                 in_buf, in_len, out_buf, out_len)
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtDeviceIoControlFile(handle.raw(h), nil, nil, nil, iosb,
                                           ioctl,
                                           in_buf, in_len or 0,
                                           out_buf, out_len or 0)
    if err.is_error(st) then err.raise('NtDeviceIoControlFile', st) end
    return iosb.Information
end

-- FILE_INFORMATION_CLASS subset used below. Values from
-- NT/PUBLIC/SDK/INC/NTIOAPI.H — stable across NT versions.
local FileBasicInformation       = 4
local FileStandardInformation    = 5
local FileRenameInformation      = 10
local FileDispositionInformation = 13
local FilePositionInformation    = 14
local FileEndOfFileInformation   = 20

M.FileBasicInformation       = FileBasicInformation
M.FileStandardInformation    = FileStandardInformation
M.FileRenameInformation      = FileRenameInformation
M.FileDispositionInformation = FileDispositionInformation
M.FilePositionInformation    = FilePositionInformation
M.FileEndOfFileInformation   = FileEndOfFileInformation

-- Query fixed-size file info. Returns a freshly-allocated cdata of the
-- requested class. NT 3.5 strict-checks Length == sizeof for fixed
-- classes (same rule as NtQueryObject), so size is taken from the cdef.
local function make_file_query(struct_name, info_class)
    local size = ffi.sizeof(struct_name)
    return function(h)
        local info = ffi.new(struct_name)
        local iosb = ffi.new('IO_STATUS_BLOCK')
        local st = ntdll.NtQueryInformationFile(handle.raw(h), iosb,
                                                info, size, info_class)
        if err.is_error(st) then err.raise('NtQueryInformationFile', st) end
        return info
    end
end

M.query_basic    = make_file_query('FILE_BASIC_INFORMATION',    FileBasicInformation)
M.query_standard = make_file_query('FILE_STANDARD_INFORMATION', FileStandardInformation)

-- Generic NtSetInformationFile bridge. info is a cdata of any size; the
-- caller supplies the matching FILE_*_INFORMATION class.
function M.NtSetInformationFile(h, info, length, info_class)
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtSetInformationFile(handle.raw(h), iosb,
                                          info, length, info_class)
    if err.is_error(st) then err.raise('NtSetInformationFile', st) end
    return iosb.Information
end

-- file:seek-style helpers. Set (and read) the kernel-side file pointer
-- on a synchronous handle. NtRead/WriteFile with ByteOffset=nil consults
-- this pointer; the wrappers in this module already pass `byte_offset`
-- explicitly, but for io.lua's `file:seek` we want NT to track position.
function M.set_position(h, offset)
    local info = ffi.new('FILE_POSITION_INFORMATION')
    info.CurrentByteOffset.QuadPart = offset
    M.NtSetInformationFile(h, info,
                           ffi.sizeof('FILE_POSITION_INFORMATION'),
                           FilePositionInformation)
end

function M.get_position(h)
    local info = ffi.new('FILE_POSITION_INFORMATION')
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtQueryInformationFile(handle.raw(h), iosb,
        info, ffi.sizeof('FILE_POSITION_INFORMATION'), FilePositionInformation)
    if err.is_error(st) then err.raise('NtQueryInformationFile', st) end
    return tonumber(info.CurrentByteOffset.QuadPart)
end

-- Mark file for deletion on last close. Handle must have been opened
-- with DELETE access. NT 3.5 deletes when the last handle to the file
-- (anywhere in the system) is closed; subsequent opens fail with
-- STATUS_DELETE_PENDING until that happens.
function M.set_disposition(h, delete)
    local info = ffi.new('FILE_DISPOSITION_INFORMATION')
    info.DeleteFile = delete and 1 or 0
    M.NtSetInformationFile(h, info,
                           ffi.sizeof('FILE_DISPOSITION_INFORMATION'),
                           FileDispositionInformation)
end

-- Truncate / extend the file to the given byte length. Handle needs
-- FILE_WRITE_DATA. After this returns the file pointer is unchanged.
function M.set_end_of_file(h, length)
    local info = ffi.new('FILE_END_OF_FILE_INFORMATION')
    info.EndOfFile.QuadPart = length
    M.NtSetInformationFile(h, info,
                           ffi.sizeof('FILE_END_OF_FILE_INFORMATION'),
                           FileEndOfFileInformation)
end

-- Rename via FILE_RENAME_INFORMATION. `new_name` is a UTF-8 Lua string;
-- decoded internally to UTF-16. `replace` defaults to false; if true
-- and the target exists it gets clobbered. `root_handle` is an optional
-- NT_HANDLE — when provided, the name is interpreted relative to that
-- directory (e.g. for renaming inside an already-opened parent dir
-- handle). Raw HANDLEs are not accepted; handle.raw enforces NT_HANDLE.
local WCHAR_BYTES = ffi.sizeof('wchar_t')

function M.set_rename(h, new_name, replace, root_handle)
    local wchars = str.decode_utf8(new_name)
    local n      = #wchars
    -- Single fused cdata: header + n wchars in one allocation. ffi.new
    -- with the trailing-VLA cdef gives us one ref to keep alive across
    -- the syscall; no cast-pointer aliasing.
    local info = ffi.new('FILE_RENAME_INFORMATION', n)
    info.ReplaceIfExists = replace and 1 or 0
    info.RootDirectory   = root_handle and handle.raw(root_handle) or nil
    info.FileNameLength  = n * WCHAR_BYTES        -- bytes, per NT contract
    for i = 1, n do
        info.FileName[i-1] = wchars[i]
    end
    -- `ffi.sizeof('TYPE', nelem)` is the documented LuaJIT idiom for
    -- VLA-struct sizing; should give header + nelem*element_size.
    local total_length = ffi.sizeof('FILE_RENAME_INFORMATION', n)
    M.NtSetInformationFile(h, info, total_length, FileRenameInformation)
end

-- Directory enumeration. Caller provides buffer; each call returns one
-- or more FILE_DIRECTORY_INFORMATION records (linked via NextEntryOffset).
-- Returns (bytes_written, status). STATUS_NO_MORE_FILES (0x80000006) is
-- WARNING-severity so it passes through is_error; end-of-dir signal.
function M.NtQueryDirectoryFile(h, buffer, length, restart_scan)
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtQueryDirectoryFile(handle.raw(h), nil, nil, nil, iosb,
                                          buffer, length,
                                          1 --[[ FileDirectoryInformation ]],
                                          0 --[[ ReturnSingleEntry=false ]],
                                          nil --[[ no filter ]],
                                          restart_scan and 1 or 0)
    if err.is_error(st) then err.raise('NtQueryDirectoryFile', st) end
    return iosb.Information, err.normalize(st)
end

return M

-- ----------------------------------------------------------------------
-- TODO — NT file/IO syscalls not yet bridged here. Add when a real
-- caller reaches for one. Grouped by shape of the wrapper they'd need.
--
-- Straightforward (same pattern as the above):
--   NtFsControlFile          filesystem-level IOCTL (FSCTL_* codes)
--   NtFlushBuffersFile       flush cached writes to disk
--   NtLockFile               byte-range lock
--   NtUnlockFile             byte-range unlock
--   NtCancelIoFile           cancel pending async I/O on a handle
--   NtDeleteFile             delete-by-path (no open needed)
--
-- Size-query pattern (caller typically invokes twice: probe length,
-- alloc buffer, call for real). Don't fit the uniform wrapper — add
-- hand-rolled helpers, or leave for raw `ntdll.<Foo>` access:
--   NtSetInformationFile         set FILE_*_INFORMATION
--   NtQueryVolumeInformationFile FILE_FS_*_INFORMATION classes
--   NtSetVolumeInformationFile   "
--   NtQueryAttributesFile        cheap stat(2) without opening the file
--   NtQueryFullAttributesFile    "  with more fields
--
-- Async / callback-driven:
--   NtNotifyChangeDirectoryFile  dir watch — completes via APC/IOCP
--
-- Named object creation (distinct from NtCreateFile):
--   NtCreateNamedPipeFile        pipe server endpoint
--   NtCreateMailslotFile         mailslot server endpoint
--
-- Scatter/gather (verify NT 3.5 ntdll actually exports — may be NT4+):
--   NtReadFileScatter
--   NtWriteFileGather
