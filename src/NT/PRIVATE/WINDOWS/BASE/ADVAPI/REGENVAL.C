/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regenval.c

Abstract:

    MicroNT-owned collapse of RegEnumValue -- NtEnumerateValueKey
    (KeyValueFullInformation) directly via ntdll.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

#define ENUMVAL_STACK    ( 256 * sizeof( WCHAR ) + 256 )

LONG
APIENTRY
RegEnumValueW(
    IN HKEY hKey,
    IN DWORD dwIndex,
    OUT LPWSTR lpValueName,
    IN OUT LPDWORD lpcchValueName,
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
    KEY_VALUE_INFORMATION_CLASS     infoClass;
    PVOID                           info;
    ULONG                           bufLen;
    ULONG                           resultLen;
    ULONG                           nameBytes;
    PWSTR                           namePtr;
    ULONG                           dataBytes;
    PBYTE                           dataPtr;
    DWORD                           type;
    BYTE    stackBuf[ sizeof( KEY_VALUE_FULL_INFORMATION ) + ENUMVAL_STACK ];

    UNREFERENCED_PARAMETER( lpReserved );

    if ( ARGUMENT_PRESENT( lpcbData ) && !ARGUMENT_PRESENT( lpData ) ) {
        *lpcbData = 0;
    }

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    infoClass = ARGUMENT_PRESENT( lpcbData )
              ? KeyValueFullInformation
              : KeyValueBasicInformation;
    info   = (PVOID)stackBuf;
    bufLen = sizeof( stackBuf );

    Status = NtEnumerateValueKey( key, dwIndex, infoClass, info, bufLen, &resultLen );
    if ( Status == STATUS_BUFFER_OVERFLOW ) {
        bufLen = resultLen;
        info   = RtlAllocateHeap( RtlProcessHeap(), 0, bufLen );
        if ( info == NULL ) {
            if ( keyIsTemp ) NtClose( key );
            return ERROR_OUTOFMEMORY;
        }
        Status = NtEnumerateValueKey( key, dwIndex, infoClass, info, bufLen, &resultLen );
    }

    if ( NT_SUCCESS( Status ) ) {

        if ( infoClass == KeyValueBasicInformation ) {
            nameBytes = ((PKEY_VALUE_BASIC_INFORMATION)info)->NameLength;
            namePtr   = ((PKEY_VALUE_BASIC_INFORMATION)info)->Name;
            dataBytes = 0;
            dataPtr   = NULL;
            type      = ((PKEY_VALUE_BASIC_INFORMATION)info)->Type;
        } else {
            nameBytes = ((PKEY_VALUE_FULL_INFORMATION)info)->NameLength;
            namePtr   = ((PKEY_VALUE_FULL_INFORMATION)info)->Name;
            dataBytes = ((PKEY_VALUE_FULL_INFORMATION)info)->DataLength;
            dataPtr   = (PBYTE)info + ((PKEY_VALUE_FULL_INFORMATION)info)->DataOffset;
            type      = ((PKEY_VALUE_FULL_INFORMATION)info)->Type;
        }

        if ( *lpcchValueName > nameBytes / sizeof( WCHAR ) ) {

            RtlMoveMemory( lpValueName, namePtr, nameBytes );
            lpValueName[ nameBytes / sizeof( WCHAR ) ] = L'\0';
            *lpcchValueName = nameBytes / sizeof( WCHAR );

            if ( ARGUMENT_PRESENT( lpType ) ) {
                *lpType = type;
            }

            if ( ARGUMENT_PRESENT( lpData ) && ARGUMENT_PRESENT( lpcbData ) ) {
                if ( *lpcbData >= dataBytes ) {
                    if ( dataBytes ) {
                        RtlMoveMemory( lpData, dataPtr, dataBytes );
                    }
                    *lpcbData = dataBytes;
                } else {
                    *lpcbData = dataBytes;
                    Status = STATUS_BUFFER_OVERFLOW;
                }
            } else if ( ARGUMENT_PRESENT( lpcbData ) ) {
                *lpcbData = dataBytes;
            }

        } else {
            Status = STATUS_BUFFER_OVERFLOW;
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
RegEnumValueA(
    IN HKEY hKey,
    IN DWORD dwIndex,
    OUT LPSTR lpValueName,
    IN OUT LPDWORD lpcchValueName,
    IN LPDWORD lpReserved,
    OUT LPDWORD lpType OPTIONAL,
    OUT LPBYTE lpData OPTIONAL,
    IN OUT LPDWORD lpcbData OPTIONAL
    )
{
    NTSTATUS    Status;
    LONG        Error;
    DWORD       nameChars;
    DWORD       cName;
    DWORD       dataBytes;
    DWORD       cbW;
    PWSTR       nameW;
    PBYTE       dataW;
    DWORD       type;
    ULONG       aLen;
    BOOL        isStr;

    UNREFERENCED_PARAMETER( lpReserved );

    nameChars = ARGUMENT_PRESENT( lpcchValueName ) ? *lpcchValueName : 0;
    dataBytes = ( ARGUMENT_PRESENT( lpData ) && ARGUMENT_PRESENT( lpcbData ) ) ? *lpcbData : 0;
    nameW = NULL;
    dataW = NULL;
    type  = REG_NONE;

    nameW = RtlAllocateHeap( RtlProcessHeap(), 0,
                ( nameChars ? nameChars : 1 ) * sizeof( WCHAR ) );
    if ( nameW == NULL ) {
        return ERROR_OUTOFMEMORY;
    }

    //
    // String data is stored as Unicode; give the W call a buffer twice the
    // caller's ANSI size so it round-trips.
    //
    if ( ARGUMENT_PRESENT( lpData ) && ARGUMENT_PRESENT( lpcbData ) ) {
        dataW = RtlAllocateHeap( RtlProcessHeap(), 0,
                    ( dataBytes ? dataBytes : 1 ) * sizeof( WCHAR ) );
        if ( dataW == NULL ) {
            RtlFreeHeap( RtlProcessHeap(), 0, nameW );
            return ERROR_OUTOFMEMORY;
        }
    }

    cName = nameChars;
    cbW   = dataBytes * sizeof( WCHAR );
    Error = RegEnumValueW( hKey, dwIndex, nameW, &cName, NULL, &type,
                dataW, dataW ? &cbW : NULL );

    if ( Error == ERROR_SUCCESS ) {

        aLen = 0;
        Status = RtlUnicodeToMultiByteN( lpValueName, nameChars, &aLen,
                                         nameW, cName * sizeof( WCHAR ) );
        if ( NT_SUCCESS( Status ) ) {
            lpValueName[ aLen ] = '\0';
            if ( ARGUMENT_PRESENT( lpcchValueName ) ) {
                *lpcchValueName = aLen;
            }
        } else {
            Error = (LONG)RtlNtStatusToDosError( Status );
        }

        if ( ARGUMENT_PRESENT( lpType ) ) {
            *lpType = type;
        }

        if ( Error == ERROR_SUCCESS && dataW != NULL ) {
            isStr = ( type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ );
            if ( isStr ) {
                aLen = 0;
                Status = RtlUnicodeToMultiByteN( (PCHAR)lpData, dataBytes, &aLen,
                                                 (PWSTR)dataW, cbW );
                if ( NT_SUCCESS( Status ) ) {
                    *lpcbData = aLen;
                } else {
                    Error = (LONG)RtlNtStatusToDosError( Status );
                }
            } else {
                if ( dataBytes >= cbW ) {
                    if ( cbW ) {
                        RtlMoveMemory( lpData, dataW, cbW );
                    }
                    *lpcbData = cbW;
                } else {
                    *lpcbData = cbW;
                    Error = ERROR_MORE_DATA;
                }
            }
        }
    }

    if ( dataW != NULL ) {
        RtlFreeHeap( RtlProcessHeap(), 0, dataW );
    }
    RtlFreeHeap( RtlProcessHeap(), 0, nameW );
    return Error;
}
