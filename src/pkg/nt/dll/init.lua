-- nt.dll — bindings to ntdll.dll.
--
-- This module is a *leaf*: it declares the common NT types that appear
-- in more than one syscall signature, then returns the `ffi.load` handle.
-- Sub-modules (nt.dll.ke, nt.dll.fs, nt.dll.mm, ...) add their own
-- prototypes via ffi.cdef and wrap the specific functions they own.
--
-- Callers typically don't require this module directly — they require
-- a sub-module and get the wrapped surface. Requiring nt.dll is an
-- escape hatch for NT symbols we haven't wrapped: the returned handle
-- resolves any cdef'd ntdll export via ffi __index.

local ffi = require('ffi')

-- Types shared across sub-modules. A type cdef'd twice is a LuaJIT
-- error, so anything used by two or more sub-modules has to live here.
-- Single-user types stay in the sub-module that owns them.
ffi.cdef[[
typedef long                    NTSTATUS;
typedef unsigned long           ULONG;
typedef unsigned short          USHORT;
typedef unsigned char           UCHAR;
typedef void *                  HANDLE;
typedef void *                  PVOID;

typedef struct _UNICODE_STRING {
    USHORT    Length;
    USHORT    MaximumLength;
    wchar_t * Buffer;
} UNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG            Length;
    HANDLE           RootDirectory;
    UNICODE_STRING * ObjectName;
    ULONG            Attributes;
    void *           SecurityDescriptor;
    void *           SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG    Information;
} IO_STATUS_BLOCK;

typedef union _LARGE_INTEGER {
    struct { ULONG LowPart; long HighPart; };
    long long QuadPart;
} LARGE_INTEGER;

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID;

/* Used by both LPC (NtConnectPort) and SE (NtDuplicateToken via OA's
 * SecurityQualityOfService field). */
typedef struct _SECURITY_QUALITY_OF_SERVICE {
    ULONG         Length;
    int           ImpersonationLevel;
    unsigned char ContextTrackingMode;
    unsigned char EffectiveOnly;
} SECURITY_QUALITY_OF_SERVICE;
]]

return ffi.load('ntdll')
