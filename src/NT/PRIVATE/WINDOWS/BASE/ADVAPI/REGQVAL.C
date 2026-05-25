/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regqval.c

Abstract:

    MicroNT-owned collapse of the Win32 registry RegQueryValueEx API --
    NtQueryValueKey directly via ntdll (the body of NT 3.5's server-side
    BaseRegQueryValue, with the RPC transmit-count + perf-data + RPC
    UNICODE_NULL-length hacks removed).

    RegQueryValueExA (ANSI, with Unicode->ANSI data conversion for string
    types) is a follow-up.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

#define DEFAULT_VALUE_SIZE  128

LONG
APIENTRY
RegQueryValueExW(
    IN HKEY hKey,
    IN LPCWSTR lpValueName,
    IN LPDWORD lpReserved,
    OUT LPDWORD lpType OPTIONAL,
    OUT LPBYTE lpData OPTIONAL,
    IN OUT LPDWORD lpcbData OPTIONAL
    )
{
    NTSTATUS                        Status;
    HANDLE                          key;
    BOOL                            keyIsTemp;
    LONG                            Error;
    UNICODE_STRING                  ValueName;
    KEY_VALUE_INFORMATION_CLASS     infoClass;
    PVOID                           info;
    ULONG                           bufLen;
    ULONG                           resultLen;
    BYTE    stackBuf[ sizeof( KEY_VALUE_PARTIAL_INFORMATION ) + DEFAULT_VALUE_SIZE ];

    UNREFERENCED_PARAMETER( lpReserved );

    if ( ARGUMENT_PRESENT( lpcbData ) && !ARGUMENT_PRESENT( lpData ) ) {
        *lpcbData = 0;
    }

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    RtlInitUnicodeString( &ValueName,
        ARGUMENT_PRESENT( lpValueName ) ? lpValueName : L"" );

    infoClass = ARGUMENT_PRESENT( lpcbData )
              ? KeyValuePartialInformation
              : KeyValueBasicInformation;
    info   = (PVOID)stackBuf;
    bufLen = sizeof( stackBuf );

    Status = NtQueryValueKey( key, &ValueName, infoClass, info, bufLen, &resultLen );

    //
    // No caller data buffer: the fixed portion is all we need, so an
    // overflow here is success.
    //
    if ( Status == STATUS_BUFFER_OVERFLOW && !ARGUMENT_PRESENT( lpData ) ) {
        Status = STATUS_SUCCESS;
    }

    //
    // The stack buffer wasn't big enough and the caller wants the data and
    // has room for it -- allocate exactly and re-query.
    //
    if ( Status == STATUS_BUFFER_OVERFLOW &&
         infoClass == KeyValuePartialInformation &&
         ARGUMENT_PRESENT( lpData ) &&
         *lpcbData >= ((PKEY_VALUE_PARTIAL_INFORMATION)info)->DataLength ) {

        bufLen = resultLen;
        info   = RtlAllocateHeap( RtlProcessHeap(), 0, bufLen );
        if ( info == NULL ) {
            if ( keyIsTemp ) {
                NtClose( key );
            }
            return ERROR_OUTOFMEMORY;
        }
        Status = NtQueryValueKey( key, &ValueName, infoClass, info, bufLen, &resultLen );
    }

    if ( NT_SUCCESS( Status ) && ARGUMENT_PRESENT( lpData ) ) {
        if ( *lpcbData >= ((PKEY_VALUE_PARTIAL_INFORMATION)info)->DataLength ) {
            RtlMoveMemory(
                lpData,
                ((PKEY_VALUE_PARTIAL_INFORMATION)info)->Data,
                ((PKEY_VALUE_PARTIAL_INFORMATION)info)->DataLength
                );
        } else {
            Status = STATUS_BUFFER_OVERFLOW;
        }
    }

    if ( NT_SUCCESS( Status ) || Status == STATUS_BUFFER_OVERFLOW ) {
        if ( infoClass == KeyValueBasicInformation ) {
            if ( ARGUMENT_PRESENT( lpType ) ) {
                *lpType = ((PKEY_VALUE_BASIC_INFORMATION)info)->Type;
            }
        } else {
            if ( ARGUMENT_PRESENT( lpType ) ) {
                *lpType = ((PKEY_VALUE_PARTIAL_INFORMATION)info)->Type;
            }
            *lpcbData = ((PKEY_VALUE_PARTIAL_INFORMATION)info)->DataLength;
        }
    }

    if ( info != (PVOID)stackBuf ) {
        RtlFreeHeap( RtlProcessHeap(), 0, info );
    }
    if ( keyIsTemp ) {
        NtClose( key );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

//
// ANSI wrapper.  Non-string data is byte-identical and passes straight
// through the W path; string types (REG_SZ / EXPAND_SZ / MULTI_SZ) are
// stored as Unicode in the registry and converted to ANSI here.
//
LONG
APIENTRY
RegQueryValueExA(
    IN HKEY hKey,
    IN LPCSTR lpValueName,
    IN LPDWORD lpReserved,
    OUT LPDWORD lpType OPTIONAL,
    OUT LPBYTE lpData OPTIONAL,
    IN OUT LPDWORD lpcbData OPTIONAL
    )
{
    NTSTATUS        Status;
    ANSI_STRING     NameA;
    UNICODE_STRING  NameU;
    LONG            Error;
    DWORD           type;
    DWORD           uSize;
    DWORD           got;
    PWSTR           uTemp;
    ULONG           ansiLen;
    BOOL            isStr;
    BOOL            haveName;

    UNREFERENCED_PARAMETER( lpReserved );

    haveName = FALSE;
    uTemp    = NULL;
    type     = REG_NONE;

    if ( ARGUMENT_PRESENT( lpValueName ) ) {
        RtlInitAnsiString( &NameA, lpValueName );
        Status = RtlAnsiStringToUnicodeString( &NameU, &NameA, TRUE );
        if ( !NT_SUCCESS( Status ) ) {
            return (LONG)RtlNtStatusToDosError( Status );
        }
        haveName = TRUE;
    }

    //
    // Discover the value type (and Unicode data size) first.
    //
    uSize = 0;
    Error = RegQueryValueExW( hKey, haveName ? NameU.Buffer : NULL,
                              NULL, &type, NULL, &uSize );
    if ( Error != ERROR_SUCCESS && Error != ERROR_MORE_DATA ) {
        goto cleanup;
    }

    isStr = ( type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ );

    if ( !isStr ) {
        //
        // Non-string data is byte-identical -- query straight into the
        // caller's buffer.
        //
        Error = RegQueryValueExW( hKey, haveName ? NameU.Buffer : NULL,
                                  NULL, lpType, lpData, lpcbData );
        goto cleanup;
    }

    if ( ARGUMENT_PRESENT( lpType ) ) {
        *lpType = type;
    }

    if ( !ARGUMENT_PRESENT( lpData ) ) {
        //
        // Size query: one ANSI byte per Unicode character.
        //
        if ( ARGUMENT_PRESENT( lpcbData ) ) {
            *lpcbData = uSize / sizeof( WCHAR );
        }
        Error = ERROR_SUCCESS;
        goto cleanup;
    }

    //
    // Fetch the Unicode data, then convert it to ANSI in the caller's buffer.
    //
    uTemp = RtlAllocateHeap( RtlProcessHeap(), 0, uSize ? uSize : sizeof( WCHAR ) );
    if ( uTemp == NULL ) {
        Error = ERROR_OUTOFMEMORY;
        goto cleanup;
    }

    got = uSize;
    Error = RegQueryValueExW( hKey, haveName ? NameU.Buffer : NULL,
                              NULL, NULL, (LPBYTE)uTemp, &got );
    if ( Error != ERROR_SUCCESS ) {
        goto cleanup;
    }

    ansiLen = 0;
    Status = RtlUnicodeToMultiByteN( (PCHAR)lpData, *lpcbData, &ansiLen, uTemp, got );
    if ( !NT_SUCCESS( Status ) ) {
        Error = (LONG)RtlNtStatusToDosError( Status );
        goto cleanup;
    }
    *lpcbData = ansiLen;
    Error = ERROR_SUCCESS;

cleanup:
    if ( uTemp != NULL ) {
        RtlFreeHeap( RtlProcessHeap(), 0, uTemp );
    }
    if ( haveName ) {
        RtlFreeUnicodeString( &NameU );
    }
    return Error;
}
