-- nt.dll.cm — Configuration Manager (the registry). Maps to NTOS/CONFIG
-- on the kernel side; symbols live in ntdll with the NtOpenKey /
-- NtQueryValueKey / NtEnumerate* family.
--
-- Bridging follows the uniform convention from nt.dll.ke:
--   - OUT pointer arguments surface as return values.
--   - Error-severity NTSTATUS (0xC0*) raises nt.errors; warnings
--     (0x8*) and informational (0x4*) pass through.
--
-- Two common warnings callers will see (neither of them errors):
--   STATUS_NO_MORE_ENTRIES  0x8000001A — NtEnumerate{Key,ValueKey}
--                                        end-of-enumeration.
--   STATUS_BUFFER_OVERFLOW  0x80000005 — NtQueryValueKey / NtQueryKey /
--                                        NtEnumerate*Key callee wants
--                                        a larger buffer; ReturnedLength
--                                        tells you how much.
--
-- Every query/enumerate wrapper returns (bytes_written, status) so
-- callers can distinguish "ok, here's the data" from "need more room".

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local str    = require('nt.dll.str')
local handle = require('nt.dll.handle')

ffi.cdef[[
/* KEY_BASIC_INFORMATION / KEY_VALUE_FULL_INFORMATION are the two
 * variable-length info classes the enumerators return. Name /
 * wchar_t buffer follow in the caller's buffer at offset 0 after the
 * fixed header (Shape 5). */
typedef struct _KEY_BASIC_INFORMATION {
    LARGE_INTEGER LastWriteTime;
    ULONG         TitleIndex;
    ULONG         NameLength;
    wchar_t       Name[1];
} KEY_BASIC_INFORMATION;

typedef struct _KEY_VALUE_FULL_INFORMATION {
    ULONG   TitleIndex;
    ULONG   Type;
    ULONG   DataOffset;
    ULONG   DataLength;
    ULONG   NameLength;
    wchar_t Name[1];
} KEY_VALUE_FULL_INFORMATION;

NTSTATUS __stdcall NtOpenKey(HANDLE *KeyHandle,
                             ULONG DesiredAccess,
                             OBJECT_ATTRIBUTES *ObjectAttributes);

NTSTATUS __stdcall NtCreateKey(HANDLE *KeyHandle,
                               ULONG DesiredAccess,
                               OBJECT_ATTRIBUTES *ObjectAttributes,
                               ULONG TitleIndex,
                               UNICODE_STRING *Class,
                               ULONG CreateOptions,
                               ULONG *Disposition);

NTSTATUS __stdcall NtDeleteKey(HANDLE KeyHandle);
NTSTATUS __stdcall NtFlushKey (HANDLE KeyHandle);

NTSTATUS __stdcall NtQueryKey(HANDLE KeyHandle,
                              int KeyInformationClass,
                              void *KeyInformation,
                              ULONG Length,
                              ULONG *ResultLength);

NTSTATUS __stdcall NtEnumerateKey(HANDLE KeyHandle,
                                  ULONG Index,
                                  int KeyInformationClass,
                                  void *KeyInformation,
                                  ULONG Length,
                                  ULONG *ResultLength);

NTSTATUS __stdcall NtEnumerateValueKey(HANDLE KeyHandle,
                                       ULONG Index,
                                       int KeyValueInformationClass,
                                       void *KeyValueInformation,
                                       ULONG Length,
                                       ULONG *ResultLength);

NTSTATUS __stdcall NtQueryValueKey(HANDLE KeyHandle,
                                   UNICODE_STRING *ValueName,
                                   int KeyValueInformationClass,
                                   void *KeyValueInformation,
                                   ULONG Length,
                                   ULONG *ResultLength);

NTSTATUS __stdcall NtSetValueKey(HANDLE KeyHandle,
                                 UNICODE_STRING *ValueName,
                                 ULONG TitleIndex,
                                 ULONG Type,
                                 void *Data,
                                 ULONG DataSize);

NTSTATUS __stdcall NtDeleteValueKey(HANDLE KeyHandle,
                                    UNICODE_STRING *ValueName);
]]

local M = {}

-- KEY_INFORMATION_CLASS values (for NtQueryKey / NtEnumerateKey).
M.KeyBasicInformation     = 0
M.KeyNodeInformation      = 1
M.KeyFullInformation      = 2
M.KeyNameInformation      = 3
M.KeyCachedInformation    = 4
M.KeyFlagsInformation     = 5
M.KeyVirtualizationInformation = 6
M.KeyHandleTagsInformation = 7

-- KEY_VALUE_INFORMATION_CLASS values (for NtQueryValueKey / NtEnumerateValueKey).
M.KeyValueBasicInformation         = 0
M.KeyValueFullInformation          = 1
M.KeyValuePartialInformation       = 2
M.KeyValueFullInformationAlign64   = 3
M.KeyValuePartialInformationAlign64 = 4

-- REG_* data-type codes (for KEY_VALUE_FULL_INFORMATION.Type).
M.REG_NONE                       = 0
M.REG_SZ                         = 1
M.REG_EXPAND_SZ                  = 2
M.REG_BINARY                     = 3
M.REG_DWORD                      = 4
M.REG_DWORD_LITTLE_ENDIAN        = 4
M.REG_DWORD_BIG_ENDIAN           = 5
M.REG_LINK                       = 6
M.REG_MULTI_SZ                   = 7
M.REG_RESOURCE_LIST              = 8
M.REG_FULL_RESOURCE_DESCRIPTOR   = 9
M.REG_RESOURCE_REQUIREMENTS_LIST = 10

function M.NtOpenKey(access, oa)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenKey(h, access, oa)
    if err.is_error(st) then err.raise('NtOpenKey', st) end
    return handle.wrap(h[0])
end

-- Returns (handle, disposition). Disposition:
--   1 = REG_CREATED_NEW_KEY
--   2 = REG_OPENED_EXISTING_KEY
function M.NtCreateKey(access, oa, title_index, class, options)
    local h    = ffi.new('HANDLE[1]')
    local disp = ffi.new('ULONG[1]')
    local st = ntdll.NtCreateKey(h, access, oa, title_index or 0,
                                 class, options or 0, disp)
    if err.is_error(st) then err.raise('NtCreateKey', st) end
    return handle.wrap(h[0]), disp[0]
end

function M.NtDeleteKey(h)
    local st = ntdll.NtDeleteKey(handle.raw(h))
    if err.is_error(st) then err.raise('NtDeleteKey', st) end
end

function M.NtFlushKey(h)
    local st = ntdll.NtFlushKey(handle.raw(h))
    if err.is_error(st) then err.raise('NtFlushKey', st) end
end

-- Caller provides buffer + length. Returns (bytes_written, status).
-- STATUS_BUFFER_OVERFLOW (0x80000005) is passed through so callers can
-- read ResultLength, realloc, and retry.
function M.NtQueryKey(h, info_class, buffer, length)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryKey(handle.raw(h), info_class, buffer, length, ret)
    if err.is_error(st) then err.raise('NtQueryKey', st) end
    return ret[0], err.normalize(st)
end

-- Enumerate subkeys. Returns (bytes_written, status).
-- STATUS_NO_MORE_ENTRIES at end; STATUS_BUFFER_OVERFLOW on short buffer.
function M.NtEnumerateKey(h, index, info_class, buffer, length)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtEnumerateKey(handle.raw(h), index, info_class,
                                    buffer, length, ret)
    if err.is_error(st) then err.raise('NtEnumerateKey', st) end
    return ret[0], err.normalize(st)
end

-- Enumerate values on a key. Same return shape as NtEnumerateKey.
function M.NtEnumerateValueKey(h, index, info_class, buffer, length)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtEnumerateValueKey(handle.raw(h), index, info_class,
                                         buffer, length, ret)
    if err.is_error(st) then err.raise('NtEnumerateValueKey', st) end
    return ret[0], err.normalize(st)
end

-- Read a value's data. `value_name` is a Lua UTF-8 string — we marshal
-- to UNICODE_STRING inside this function, binding the NT_STRING to a
-- local that's guaranteed alive across the syscall. Caller supplies
-- the info_class + output buffer (buffer is caller-owned; bind it to
-- a local yourself). Returns (bytes_written, status);
-- STATUS_BUFFER_OVERFLOW tells you to retry with a larger buffer.
function M.NtQueryValueKey(h, value_name, info_class, buffer, length)
    local ns  = str.to_utf16(value_name)
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryValueKey(handle.raw(h), ns.us, info_class,
                                     buffer, length, ret)
    if err.is_error(st) then err.raise('NtQueryValueKey', st) end
    return ret[0], err.normalize(st)
end

-- `value_name` is a Lua UTF-8 string (marshalled here). `data` is a
-- caller-owned buffer/cdata; the caller binds it to a local so its
-- backing storage is alive for the syscall.
function M.NtSetValueKey(h, value_name, value_type, data, data_size)
    local ns = str.to_utf16(value_name)
    local st = ntdll.NtSetValueKey(handle.raw(h), ns.us, 0, value_type,
                                   data, data_size)
    if err.is_error(st) then err.raise('NtSetValueKey', st) end
end

function M.NtDeleteValueKey(h, value_name)
    local ns = str.to_utf16(value_name)
    local st = ntdll.NtDeleteValueKey(handle.raw(h), ns.us)
    if err.is_error(st) then err.raise('NtDeleteValueKey', st) end
end

return M

-- ----------------------------------------------------------------------
-- TODO — NT registry syscalls not yet bridged:
--
-- Hive management (mostly used by smss-era setup and backup tools):
--   NtLoadKey            mount a hive file as a subkey
--   NtUnloadKey          unmount
--   NtReplaceKey         atomic replace of a subkey's hive
--   NtRestoreKey         apply a saved hive into a key
--   NtSaveKey            write a key subtree to a hive file
--
-- Notification (async):
--   NtNotifyChangeKey            watch a key for changes, APC/IOCP
--                                completion.
--
-- Lesser-used:
--   NtInitializeRegistry         called by smss during boot; normal
--                                userland has no reason.
--   NtQueryOpenSubKeys           count open subkeys
--   NtQueryMultipleValueKey      batch variant of NtQueryValueKey
--
-- Well-known KEY_INFORMATION_CLASS / KEY_VALUE_INFORMATION_CLASS values
-- would live here once callers need them; for now just pass ints.
