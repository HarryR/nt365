/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regcreate.c

Abstract:

    MicroNT-owned collapse of the Win32 registry RegCreateKey* APIs --
    predefined-root resolution + NtCreateKey, directly via ntdll.

    NOTE: NtCreateKey creates only the leaf key; intermediate path
    components must already exist.  Creating missing intermediates (the
    wcstok loop in NT 3.5's server-side BaseRegCreateKey) is a follow-up
    if a caller ever needs it -- Python's startup path doesn't.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

LONG
APIENTRY
RegCreateKeyExW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey,
    IN DWORD Reserved,
    IN LPWSTR lpClass OPTIONAL,
    IN DWORD dwOptions,
    IN REGSAM samDesired,
    IN LPSECURITY_ATTRIBUTES lpSecurityAttributes OPTIONAL,
    OUT PHKEY phkResult,
    OUT LPDWORD lpdwDisposition OPTIONAL
    )
{
    OBJECT_ATTRIBUTES   Obja;
    UNICODE_STRING      SubKey;
    UNICODE_STRING      Class;
    PUNICODE_STRING     pClass;
    NTSTATUS            Status;
    HANDLE              root;
    BOOL                rootIsTemp;
    LONG                Error;

    UNREFERENCED_PARAMETER( Reserved );

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &root, &rootIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    RtlInitUnicodeString( &SubKey, ARGUMENT_PRESENT( lpSubKey ) ? lpSubKey : L"" );

    pClass = NULL;
    if ( ARGUMENT_PRESENT( lpClass ) ) {
        RtlInitUnicodeString( &Class, lpClass );
        pClass = &Class;
    }

    InitializeObjectAttributes(
        &Obja,
        &SubKey,
        OBJ_CASE_INSENSITIVE,
        root,
        ARGUMENT_PRESENT( lpSecurityAttributes )
            ? lpSecurityAttributes->lpSecurityDescriptor
            : NULL
        );

    Status = NtCreateKey(
                (PHANDLE)phkResult,
                samDesired,
                &Obja,
                0,
                pClass,
                dwOptions,
                lpdwDisposition
                );

    if ( rootIsTemp ) {
        NtClose( root );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegCreateKeyExA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey,
    IN DWORD Reserved,
    IN LPSTR lpClass OPTIONAL,
    IN DWORD dwOptions,
    IN REGSAM samDesired,
    IN LPSECURITY_ATTRIBUTES lpSecurityAttributes OPTIONAL,
    OUT PHKEY phkResult,
    OUT LPDWORD lpdwDisposition OPTIONAL
    )
{
    NTSTATUS        Status;
    ANSI_STRING     aStr;
    UNICODE_STRING  SubKeyU;
    UNICODE_STRING  ClassU;
    LONG            Error;
    BOOL            haveSub = FALSE;
    BOOL            haveClass = FALSE;

    if ( ARGUMENT_PRESENT( lpSubKey ) ) {
        RtlInitAnsiString( &aStr, lpSubKey );
        Status = RtlAnsiStringToUnicodeString( &SubKeyU, &aStr, TRUE );
        if ( !NT_SUCCESS( Status ) ) {
            return (LONG)RtlNtStatusToDosError( Status );
        }
        haveSub = TRUE;
    }

    if ( ARGUMENT_PRESENT( lpClass ) ) {
        RtlInitAnsiString( &aStr, lpClass );
        Status = RtlAnsiStringToUnicodeString( &ClassU, &aStr, TRUE );
        if ( !NT_SUCCESS( Status ) ) {
            if ( haveSub ) {
                RtlFreeUnicodeString( &SubKeyU );
            }
            return (LONG)RtlNtStatusToDosError( Status );
        }
        haveClass = TRUE;
    }

    Error = RegCreateKeyExW(
                hKey,
                haveSub ? SubKeyU.Buffer : NULL,
                Reserved,
                haveClass ? ClassU.Buffer : NULL,
                dwOptions,
                samDesired,
                lpSecurityAttributes,
                phkResult,
                lpdwDisposition
                );

    if ( haveClass ) {
        RtlFreeUnicodeString( &ClassU );
    }
    if ( haveSub ) {
        RtlFreeUnicodeString( &SubKeyU );
    }

    return Error;
}

//
// Win 3.1 compatible RegCreateKey: non-volatile, MAXIMUM_ALLOWED.
//

LONG
APIENTRY
RegCreateKeyW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey,
    OUT PHKEY phkResult
    )
{
    return RegCreateKeyExW( hKey, lpSubKey, 0, NULL,
               REG_OPTION_NON_VOLATILE, MAXIMUM_ALLOWED, NULL, phkResult, NULL );
}

LONG
APIENTRY
RegCreateKeyA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey,
    OUT PHKEY phkResult
    )
{
    return RegCreateKeyExA( hKey, lpSubKey, 0, NULL,
               REG_OPTION_NON_VOLATILE, MAXIMUM_ALLOWED, NULL, phkResult, NULL );
}
