/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regset.c

Abstract:

    MicroNT-owned collapse of RegSetValueEx -- NtSetValueKey directly via
    ntdll.  String values (REG_SZ / EXPAND_SZ / MULTI_SZ) are stored as
    Unicode, so the ANSI wrapper converts the data up before storing.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

LONG
APIENTRY
RegSetValueExW(
    IN HKEY hKey,
    IN LPCWSTR lpValueName,
    IN DWORD Reserved,
    IN DWORD dwType,
    IN CONST BYTE* lpData,
    IN DWORD cbData
    )
{
    NTSTATUS        Status;
    HANDLE          key;
    BOOL            keyIsTemp;
    LONG            Error;
    UNICODE_STRING  ValueName;

    UNREFERENCED_PARAMETER( Reserved );

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    RtlInitUnicodeString( &ValueName,
        ARGUMENT_PRESENT( lpValueName ) ? lpValueName : L"" );

    Status = NtSetValueKey( key, &ValueName, 0, dwType, (PVOID)lpData, cbData );

    if ( keyIsTemp ) {
        NtClose( key );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegSetValueExA(
    IN HKEY hKey,
    IN LPCSTR lpValueName,
    IN DWORD Reserved,
    IN DWORD dwType,
    IN CONST BYTE* lpData,
    IN DWORD cbData
    )
{
    NTSTATUS        Status;
    ANSI_STRING     NameA;
    UNICODE_STRING  NameU;
    LONG            Error;
    BOOL            haveName;
    BOOL            isStr;
    PWSTR           dataW;
    ULONG           uLen;

    haveName = FALSE;
    dataW    = NULL;

    if ( ARGUMENT_PRESENT( lpValueName ) ) {
        RtlInitAnsiString( &NameA, lpValueName );
        Status = RtlAnsiStringToUnicodeString( &NameU, &NameA, TRUE );
        if ( !NT_SUCCESS( Status ) ) {
            return (LONG)RtlNtStatusToDosError( Status );
        }
        haveName = TRUE;
    }

    isStr = ( dwType == REG_SZ || dwType == REG_EXPAND_SZ || dwType == REG_MULTI_SZ );

    if ( isStr && lpData != NULL && cbData != 0 ) {
        uLen = 0;
        dataW = RtlAllocateHeap( RtlProcessHeap(), 0, cbData * sizeof( WCHAR ) );
        if ( dataW == NULL ) {
            if ( haveName ) RtlFreeUnicodeString( &NameU );
            return ERROR_OUTOFMEMORY;
        }
        Status = RtlMultiByteToUnicodeN( dataW, cbData * sizeof( WCHAR ), &uLen,
                                         (PCHAR)lpData, cbData );
        if ( !NT_SUCCESS( Status ) ) {
            RtlFreeHeap( RtlProcessHeap(), 0, dataW );
            if ( haveName ) RtlFreeUnicodeString( &NameU );
            return (LONG)RtlNtStatusToDosError( Status );
        }
        Error = RegSetValueExW( hKey, haveName ? NameU.Buffer : NULL,
                    Reserved, dwType, (CONST BYTE*)dataW, uLen );
        RtlFreeHeap( RtlProcessHeap(), 0, dataW );
    } else {
        Error = RegSetValueExW( hKey, haveName ? NameU.Buffer : NULL,
                    Reserved, dwType, lpData, cbData );
    }

    if ( haveName ) {
        RtlFreeUnicodeString( &NameU );
    }
    return Error;
}
