/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regenum.c

Abstract:

    MicroNT-owned collapse of the Win32 registry key-enumeration APIs --
    NtEnumerateKey directly via ntdll.  (RegEnumValue lives in regenval.c.)

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

#define ENUM_STACK_CHARS    512

LONG
APIENTRY
RegEnumKeyExW(
    IN HKEY hKey,
    IN DWORD dwIndex,
    OUT LPWSTR lpName,
    IN OUT LPDWORD lpcName,
    IN LPDWORD lpReserved,
    OUT LPWSTR lpClass OPTIONAL,
    IN OUT LPDWORD lpcClass OPTIONAL,
    OUT PFILETIME lpftLastWriteTime OPTIONAL
    )
{
    NTSTATUS                Status;
    HANDLE                  key;
    BOOL                    keyIsTemp;
    LONG                    Error;
    KEY_INFORMATION_CLASS   infoClass;
    PVOID                   info;
    ULONG                   bufLen;
    ULONG                   resultLen;
    ULONG                   nameBytes;
    PWSTR                   namePtr;
    ULONG                   classBytes;
    PWSTR                   classPtr;
    PFILETIME               pLastWrite;
    BYTE    stackBuf[ sizeof( KEY_NODE_INFORMATION ) + ENUM_STACK_CHARS * sizeof( WCHAR ) ];

    UNREFERENCED_PARAMETER( lpReserved );

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    infoClass = ARGUMENT_PRESENT( lpClass ) ? KeyNodeInformation : KeyBasicInformation;
    info   = (PVOID)stackBuf;
    bufLen = sizeof( stackBuf );

    Status = NtEnumerateKey( key, dwIndex, infoClass, info, bufLen, &resultLen );
    if ( Status == STATUS_BUFFER_OVERFLOW ) {
        bufLen = resultLen;
        info   = RtlAllocateHeap( RtlProcessHeap(), 0, bufLen );
        if ( info == NULL ) {
            if ( keyIsTemp ) NtClose( key );
            return ERROR_OUTOFMEMORY;
        }
        Status = NtEnumerateKey( key, dwIndex, infoClass, info, bufLen, &resultLen );
    }

    if ( NT_SUCCESS( Status ) ) {

        if ( infoClass == KeyBasicInformation ) {
            nameBytes  = ((PKEY_BASIC_INFORMATION)info)->NameLength;
            namePtr    = ((PKEY_BASIC_INFORMATION)info)->Name;
            classBytes = 0;
            classPtr   = NULL;
            pLastWrite = (PFILETIME)&((PKEY_BASIC_INFORMATION)info)->LastWriteTime;
        } else {
            nameBytes  = ((PKEY_NODE_INFORMATION)info)->NameLength;
            namePtr    = ((PKEY_NODE_INFORMATION)info)->Name;
            classBytes = ((PKEY_NODE_INFORMATION)info)->ClassLength;
            classPtr   = (PWSTR)((PBYTE)info + ((PKEY_NODE_INFORMATION)info)->ClassOffset);
            pLastWrite = (PFILETIME)&((PKEY_NODE_INFORMATION)info)->LastWriteTime;
        }

        //
        // *lpcName is the buffer size in chars including the terminating null.
        //
        if ( *lpcName > nameBytes / sizeof( WCHAR ) ) {

            RtlMoveMemory( lpName, namePtr, nameBytes );
            lpName[ nameBytes / sizeof( WCHAR ) ] = L'\0';
            *lpcName = nameBytes / sizeof( WCHAR );

            if ( ARGUMENT_PRESENT( lpClass ) ) {
                if ( lpcClass != NULL && *lpcClass > classBytes / sizeof( WCHAR ) ) {
                    if ( classBytes ) {
                        RtlMoveMemory( lpClass, classPtr, classBytes );
                    }
                    lpClass[ classBytes / sizeof( WCHAR ) ] = L'\0';
                    *lpcClass = classBytes / sizeof( WCHAR );
                } else {
                    Status = STATUS_BUFFER_OVERFLOW;
                }
            }

            if ( ARGUMENT_PRESENT( lpftLastWriteTime ) ) {
                *lpftLastWriteTime = *pLastWrite;
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
RegEnumKeyExA(
    IN HKEY hKey,
    IN DWORD dwIndex,
    OUT LPSTR lpName,
    IN OUT LPDWORD lpcName,
    IN LPDWORD lpReserved,
    OUT LPSTR lpClass OPTIONAL,
    IN OUT LPDWORD lpcClass OPTIONAL,
    OUT PFILETIME lpftLastWriteTime OPTIONAL
    )
{
    NTSTATUS    Status;
    LONG        Error;
    DWORD       nameChars;
    DWORD       classChars;
    DWORD       cName;
    DWORD       cClass;
    PWSTR       nameW;
    PWSTR       classW;
    ULONG       aLen;

    UNREFERENCED_PARAMETER( lpReserved );

    nameChars  = ARGUMENT_PRESENT( lpcName )  ? *lpcName  : 0;
    classChars = ARGUMENT_PRESENT( lpcClass ) ? *lpcClass : 0;

    nameW = RtlAllocateHeap( RtlProcessHeap(), 0,
                ( nameChars ? nameChars : 1 ) * sizeof( WCHAR ) );
    if ( nameW == NULL ) {
        return ERROR_OUTOFMEMORY;
    }
    classW = NULL;
    if ( ARGUMENT_PRESENT( lpClass ) ) {
        classW = RtlAllocateHeap( RtlProcessHeap(), 0,
                    ( classChars ? classChars : 1 ) * sizeof( WCHAR ) );
        if ( classW == NULL ) {
            RtlFreeHeap( RtlProcessHeap(), 0, nameW );
            return ERROR_OUTOFMEMORY;
        }
    }

    cName  = nameChars;
    cClass = classChars;
    Error = RegEnumKeyExW( hKey, dwIndex, nameW, &cName, NULL,
                ARGUMENT_PRESENT( lpClass ) ? classW : NULL,
                ARGUMENT_PRESENT( lpClass ) ? &cClass : NULL,
                lpftLastWriteTime );

    if ( Error == ERROR_SUCCESS ) {
        aLen = 0;
        Status = RtlUnicodeToMultiByteN( lpName, nameChars, &aLen,
                                         nameW, cName * sizeof( WCHAR ) );
        if ( NT_SUCCESS( Status ) ) {
            lpName[ aLen ] = '\0';
            if ( ARGUMENT_PRESENT( lpcName ) ) {
                *lpcName = aLen;
            }
        } else {
            Error = (LONG)RtlNtStatusToDosError( Status );
        }

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
    }

    if ( classW != NULL ) {
        RtlFreeHeap( RtlProcessHeap(), 0, classW );
    }
    RtlFreeHeap( RtlProcessHeap(), 0, nameW );
    return Error;
}

//
// Win 3.1 compatible RegEnumKey (name only; cchName is the buffer char count).
//

LONG
APIENTRY
RegEnumKeyW(
    IN HKEY hKey,
    IN DWORD dwIndex,
    OUT LPWSTR lpName,
    IN DWORD cchName
    )
{
    DWORD c = cchName;
    return RegEnumKeyExW( hKey, dwIndex, lpName, &c, NULL, NULL, NULL, NULL );
}

LONG
APIENTRY
RegEnumKeyA(
    IN HKEY hKey,
    IN DWORD dwIndex,
    OUT LPSTR lpName,
    IN DWORD cchName
    )
{
    DWORD c = cchName;
    return RegEnumKeyExA( hKey, dwIndex, lpName, &c, NULL, NULL, NULL, NULL );
}
