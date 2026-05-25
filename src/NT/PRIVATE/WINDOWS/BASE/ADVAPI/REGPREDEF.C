/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regpredef.c

Abstract:

    MicroNT-owned predefined-handle resolution for the collapsed Win32
    registry.  Maps the 0x8000000x pseudo-handles into the Nt registry
    namespace and opens a real key handle, directly via ntdll (no winreg
    server / RPC).

        HKEY_LOCAL_MACHINE  -> \REGISTRY\MACHINE
        HKEY_USERS          -> \REGISTRY\USER
        HKEY_CLASSES_ROOT   -> \REGISTRY\MACHINE\SOFTWARE\CLASSES
        HKEY_CURRENT_USER   -> RtlOpenCurrentUser (token SID -> \REGISTRY\USER\<SID>,
                               LSA-free; falls back to .DEFAULT)

    HKEY_CURRENT_CONFIG / HKEY_PERFORMANCE_* / HKEY_DYN_DATA are unsupported
    (no perf counters / hardware profiles) and return ERROR_INVALID_HANDLE.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

#define LENGTH( str )   ( sizeof( str ) - sizeof( UNICODE_NULL ) )

#define MACHINE         L"\\REGISTRY\\MACHINE"
#define USERS           L"\\REGISTRY\\USER"
#define CLASSES         L"\\REGISTRY\\MACHINE\\SOFTWARE\\CLASSES"

static UNICODE_STRING MachineRoot = { LENGTH( MACHINE ), LENGTH( MACHINE ), MACHINE };
static UNICODE_STRING UsersRoot   = { LENGTH( USERS ),   LENGTH( USERS ),   USERS };
static UNICODE_STRING ClassesRoot = { LENGTH( CLASSES ), LENGTH( CLASSES ), CLASSES };

static LONG
OpenPredefinedRoot(
    IN HKEY hKey,
    IN REGSAM samDesired,
    OUT PHANDLE pRoot
    )
{
    OBJECT_ATTRIBUTES   Obja;
    PUNICODE_STRING     root;
    NTSTATUS            Status;

    switch ( (ULONG)hKey ) {

    case (ULONG)HKEY_LOCAL_MACHINE: root = &MachineRoot; break;
    case (ULONG)HKEY_USERS:         root = &UsersRoot;   break;
    case (ULONG)HKEY_CLASSES_ROOT:  root = &ClassesRoot; break;

    case (ULONG)HKEY_CURRENT_USER:
        Status = RtlOpenCurrentUser( samDesired, pRoot );
        return (LONG)RtlNtStatusToDosError( Status );

    default:
        return ERROR_INVALID_HANDLE;
    }

    InitializeObjectAttributes( &Obja, root, OBJ_CASE_INSENSITIVE, NULL, NULL );
    Status = NtOpenKey( pRoot, samDesired, &Obja );
    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
RegpResolveKey(
    IN HKEY hKey,
    IN REGSAM samDesired,
    OUT PHANDLE pReal,
    OUT PBOOL pIsTemp
    )
{
    if ( IS_PREDEFINED_HKEY( hKey ) ) {
        *pIsTemp = TRUE;
        return OpenPredefinedRoot( hKey, samDesired, pReal );
    }

    *pReal = (HANDLE)hKey;
    *pIsTemp = FALSE;
    return ERROR_SUCCESS;
}
