/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regopen.c

Abstract:

    MicroNT-owned collapse of the Win32 registry RegOpenKey* APIs.

    The NT 3.5 winreg client/server/RPC split is gone: the public Reg*
    entry points resolve a predefined root (see regpredef.c) to a real Nt
    key and call NtOpenKey directly (the body of the old server-side
    BaseRegOpenKey).

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

LONG
APIENTRY
RegOpenKeyExW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey OPTIONAL,
    IN DWORD ulOptions,
    IN REGSAM samDesired,
    OUT PHKEY phkResult
    )
{
    OBJECT_ATTRIBUTES   Obja;
    UNICODE_STRING      SubKey;
    NTSTATUS            Status;
    HANDLE              root;
    BOOL                rootIsTemp;
    LONG                Error;

    UNREFERENCED_PARAMETER( ulOptions );

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &root, &rootIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    RtlInitUnicodeString( &SubKey, ARGUMENT_PRESENT( lpSubKey ) ? lpSubKey : L"" );

    InitializeObjectAttributes(
        &Obja,
        &SubKey,
        OBJ_CASE_INSENSITIVE,
        root,
        NULL
        );

    Status = NtOpenKey( (PHANDLE)phkResult, samDesired, &Obja );

    if ( rootIsTemp ) {
        NtClose( root );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegOpenKeyExA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey OPTIONAL,
    IN DWORD ulOptions,
    IN REGSAM samDesired,
    OUT PHKEY phkResult
    )
{
    NTSTATUS        Status;
    ANSI_STRING     SubKeyA;
    UNICODE_STRING  SubKeyU;
    LONG            Error;

    if ( ARGUMENT_PRESENT( lpSubKey ) ) {
        RtlInitAnsiString( &SubKeyA, lpSubKey );
        Status = RtlAnsiStringToUnicodeString( &SubKeyU, &SubKeyA, TRUE );
        if ( !NT_SUCCESS( Status ) ) {
            return (LONG)RtlNtStatusToDosError( Status );
        }
        Error = RegOpenKeyExW( hKey, SubKeyU.Buffer, ulOptions, samDesired, phkResult );
        RtlFreeUnicodeString( &SubKeyU );
    } else {
        Error = RegOpenKeyExW( hKey, NULL, ulOptions, samDesired, phkResult );
    }

    return Error;
}

//
// Win 3.1 compatible RegOpenKey: opens with MAXIMUM_ALLOWED.
//

LONG
APIENTRY
RegOpenKeyW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey OPTIONAL,
    OUT PHKEY phkResult
    )
{
    return RegOpenKeyExW( hKey, lpSubKey, 0, MAXIMUM_ALLOWED, phkResult );
}

LONG
APIENTRY
RegOpenKeyA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey OPTIONAL,
    OUT PHKEY phkResult
    )
{
    return RegOpenKeyExA( hKey, lpSubKey, 0, MAXIMUM_ALLOWED, phkResult );
}
