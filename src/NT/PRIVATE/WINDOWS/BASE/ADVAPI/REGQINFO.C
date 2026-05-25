/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regqinfo.c

Abstract:

    MicroNT-owned collapse of RegQueryInfoKey -- NtQueryKey
    (KeyFullInformation) directly via ntdll.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

LONG
APIENTRY
RegQueryInfoKeyW(
    IN HKEY hKey,
    OUT LPWSTR lpClass OPTIONAL,
    IN OUT LPDWORD lpcClass OPTIONAL,
    IN LPDWORD lpReserved,
    OUT LPDWORD lpcSubKeys OPTIONAL,
    OUT LPDWORD lpcMaxSubKeyLen OPTIONAL,
    OUT LPDWORD lpcMaxClassLen OPTIONAL,
    OUT LPDWORD lpcValues OPTIONAL,
    OUT LPDWORD lpcMaxValueNameLen OPTIONAL,
    OUT LPDWORD lpcMaxValueLen OPTIONAL,
    OUT LPDWORD lpcbSecurityDescriptor OPTIONAL,
    OUT PFILETIME lpftLastWriteTime OPTIONAL
    )
{
    NTSTATUS                Status;
    HANDLE                  key;
    BOOL                    keyIsTemp;
    LONG                    Error;
    PKEY_FULL_INFORMATION   fi;
    PVOID                   info;
    ULONG                   bufLen;
    ULONG                   resultLen;
    BYTE    stackBuf[ sizeof( KEY_FULL_INFORMATION ) + 256 * sizeof( WCHAR ) ];

    UNREFERENCED_PARAMETER( lpReserved );

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    info   = (PVOID)stackBuf;
    bufLen = sizeof( stackBuf );

    Status = NtQueryKey( key, KeyFullInformation, info, bufLen, &resultLen );
    if ( Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL ) {
        bufLen = resultLen;
        info   = RtlAllocateHeap( RtlProcessHeap(), 0, bufLen );
        if ( info == NULL ) {
            if ( keyIsTemp ) NtClose( key );
            return ERROR_OUTOFMEMORY;
        }
        Status = NtQueryKey( key, KeyFullInformation, info, bufLen, &resultLen );
    }

    if ( NT_SUCCESS( Status ) ) {
        fi = (PKEY_FULL_INFORMATION)info;

        if ( ARGUMENT_PRESENT( lpcSubKeys ) )          *lpcSubKeys = fi->SubKeys;
        if ( ARGUMENT_PRESENT( lpcMaxSubKeyLen ) )     *lpcMaxSubKeyLen = fi->MaxNameLen / sizeof( WCHAR );
        if ( ARGUMENT_PRESENT( lpcMaxClassLen ) )      *lpcMaxClassLen = fi->MaxClassLen / sizeof( WCHAR );
        if ( ARGUMENT_PRESENT( lpcValues ) )           *lpcValues = fi->Values;
        if ( ARGUMENT_PRESENT( lpcMaxValueNameLen ) )  *lpcMaxValueNameLen = fi->MaxValueNameLen / sizeof( WCHAR );
        if ( ARGUMENT_PRESENT( lpcMaxValueLen ) )      *lpcMaxValueLen = fi->MaxValueDataLen;
        if ( ARGUMENT_PRESENT( lpcbSecurityDescriptor ) ) *lpcbSecurityDescriptor = 0;
        if ( ARGUMENT_PRESENT( lpftLastWriteTime ) )   *lpftLastWriteTime = *(PFILETIME)&fi->LastWriteTime;

        if ( ARGUMENT_PRESENT( lpClass ) && ARGUMENT_PRESENT( lpcClass ) ) {
            if ( *lpcClass > fi->ClassLength / sizeof( WCHAR ) ) {
                if ( fi->ClassLength ) {
                    RtlMoveMemory( lpClass, (PBYTE)info + fi->ClassOffset, fi->ClassLength );
                }
                lpClass[ fi->ClassLength / sizeof( WCHAR ) ] = L'\0';
                *lpcClass = fi->ClassLength / sizeof( WCHAR );
            } else {
                Status = STATUS_BUFFER_OVERFLOW;
            }
        } else if ( ARGUMENT_PRESENT( lpcClass ) ) {
            *lpcClass = fi->ClassLength / sizeof( WCHAR );
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

LONG
APIENTRY
RegQueryInfoKeyA(
    IN HKEY hKey,
    OUT LPSTR lpClass OPTIONAL,
    IN OUT LPDWORD lpcClass OPTIONAL,
    IN LPDWORD lpReserved,
    OUT LPDWORD lpcSubKeys OPTIONAL,
    OUT LPDWORD lpcMaxSubKeyLen OPTIONAL,
    OUT LPDWORD lpcMaxClassLen OPTIONAL,
    OUT LPDWORD lpcValues OPTIONAL,
    OUT LPDWORD lpcMaxValueNameLen OPTIONAL,
    OUT LPDWORD lpcMaxValueLen OPTIONAL,
    OUT LPDWORD lpcbSecurityDescriptor OPTIONAL,
    OUT PFILETIME lpftLastWriteTime OPTIONAL
    )
{
    NTSTATUS    Status;
    LONG        Error;
    DWORD       classChars;
    DWORD       cClass;
    PWSTR       classW;
    ULONG       aLen;

    classChars = ARGUMENT_PRESENT( lpcClass ) ? *lpcClass : 0;
    classW = NULL;
    if ( ARGUMENT_PRESENT( lpClass ) ) {
        classW = RtlAllocateHeap( RtlProcessHeap(), 0,
                    ( classChars ? classChars : 1 ) * sizeof( WCHAR ) );
        if ( classW == NULL ) {
            return ERROR_OUTOFMEMORY;
        }
    }

    cClass = classChars;
    Error = RegQueryInfoKeyW( hKey,
                ARGUMENT_PRESENT( lpClass ) ? classW : NULL,
                ARGUMENT_PRESENT( lpClass ) ? &cClass : lpcClass,
                lpReserved, lpcSubKeys, lpcMaxSubKeyLen, lpcMaxClassLen,
                lpcValues, lpcMaxValueNameLen, lpcMaxValueLen,
                lpcbSecurityDescriptor, lpftLastWriteTime );

    if ( Error == ERROR_SUCCESS && ARGUMENT_PRESENT( lpClass ) ) {
        aLen = 0;
        Status = RtlUnicodeToMultiByteN( lpClass, classChars, &aLen,
                                         classW, cClass * sizeof( WCHAR ) );
        if ( NT_SUCCESS( Status ) ) {
            lpClass[ aLen ] = '\0';
            if ( ARGUMENT_PRESENT( lpcClass ) ) {
                *lpcClass = aLen;
            }
        } else {
            Error = (LONG)RtlNtStatusToDosError( Status );
        }
    }

    if ( classW != NULL ) {
        RtlFreeHeap( RtlProcessHeap(), 0, classW );
    }
    return Error;
}
