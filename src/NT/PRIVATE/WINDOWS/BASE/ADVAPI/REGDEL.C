/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regdel.c

Abstract:

    MicroNT-owned collapse of RegDeleteKey / RegDeleteValue -- NtDeleteKey
    / NtDeleteValueKey directly via ntdll.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

LONG
APIENTRY
RegDeleteKeyW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey
    )
{
    NTSTATUS            Status;
    HANDLE              root;
    BOOL                rootIsTemp;
    LONG                Error;
    HANDLE              target;
    OBJECT_ATTRIBUTES   Obja;
    UNICODE_STRING      SubKey;

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &root, &rootIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    RtlInitUnicodeString( &SubKey, ARGUMENT_PRESENT( lpSubKey ) ? lpSubKey : L"" );
    InitializeObjectAttributes( &Obja, &SubKey, OBJ_CASE_INSENSITIVE, root, NULL );

    Status = NtOpenKey( &target, DELETE, &Obja );
    if ( NT_SUCCESS( Status ) ) {
        Status = NtDeleteKey( target );
        NtClose( target );
    }

    if ( rootIsTemp ) {
        NtClose( root );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegDeleteKeyA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey
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
        Error = RegDeleteKeyW( hKey, SubKeyU.Buffer );
        RtlFreeUnicodeString( &SubKeyU );
    } else {
        Error = RegDeleteKeyW( hKey, NULL );
    }

    return Error;
}

LONG
APIENTRY
RegDeleteValueW(
    IN HKEY hKey,
    IN LPWSTR lpValueName
    )
{
    NTSTATUS        Status;
    HANDLE          key;
    BOOL            keyIsTemp;
    LONG            Error;
    UNICODE_STRING  ValueName;

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    RtlInitUnicodeString( &ValueName,
        ARGUMENT_PRESENT( lpValueName ) ? lpValueName : L"" );

    Status = NtDeleteValueKey( key, &ValueName );

    if ( keyIsTemp ) {
        NtClose( key );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegDeleteValueA(
    IN HKEY hKey,
    IN LPSTR lpValueName
    )
{
    NTSTATUS        Status;
    ANSI_STRING     NameA;
    UNICODE_STRING  NameU;
    LONG            Error;

    if ( ARGUMENT_PRESENT( lpValueName ) ) {
        RtlInitAnsiString( &NameA, lpValueName );
        Status = RtlAnsiStringToUnicodeString( &NameU, &NameA, TRUE );
        if ( !NT_SUCCESS( Status ) ) {
            return (LONG)RtlNtStatusToDosError( Status );
        }
        Error = RegDeleteValueW( hKey, NameU.Buffer );
        RtlFreeUnicodeString( &NameU );
    } else {
        Error = RegDeleteValueW( hKey, NULL );
    }

    return Error;
}
