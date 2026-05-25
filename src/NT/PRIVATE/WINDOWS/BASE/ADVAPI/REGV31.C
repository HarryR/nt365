/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regv31.c

Abstract:

    MicroNT-owned Win 3.1-style default-value registry wrappers:
    RegQueryValue / RegSetValue (A + W).

    These predate the named-value Reg*ValueEx APIs.  Each key has a single
    unnamed REG_SZ "default" value, addressed by an optional relative subkey;
    the wrappers open/create that subkey and read/write its default value.
    Superseded by RegQueryValueEx / RegSetValueEx for new code, but software
    of the NT 3.x / Win 3.1 era (e.g. Python 2.5's startup path computation)
    still imports them, so they are implemented here over the collapsed Ex
    surface rather than stubbed.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <string.h>
#include "regadvp.h"

LONG
APIENTRY
RegQueryValueW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey,
    OUT LPWSTR lpValue,
    IN OUT PLONG lpcbValue
    )
{
    HKEY    hSub = hKey;
    BOOL    opened = FALSE;
    LONG    Error;
    DWORD   type;
    DWORD   cb;

    if ( ARGUMENT_PRESENT( lpSubKey ) && lpSubKey[0] != L'\0' ) {
        Error = RegOpenKeyExW( hKey, lpSubKey, 0, KEY_QUERY_VALUE, &hSub );
        if ( Error != ERROR_SUCCESS ) {
            return Error;
        }
        opened = TRUE;
    }

    cb = ( ARGUMENT_PRESENT( lpcbValue ) && ARGUMENT_PRESENT( lpValue ) )
       ? (DWORD)*lpcbValue : 0;

    //
    // The default value is the unnamed value -- pass NULL so the Ex layer
    // resolves it to the empty value name.
    //
    Error = RegQueryValueExW( hSub, NULL, NULL, &type, (LPBYTE)lpValue, &cb );

    if ( ARGUMENT_PRESENT( lpcbValue ) ) {
        *lpcbValue = (LONG)cb;
    }

    if ( opened ) {
        RegCloseKey( hSub );
    }

    return Error;
}

LONG
APIENTRY
RegQueryValueA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey,
    OUT LPSTR lpValue,
    IN OUT PLONG lpcbValue
    )
{
    HKEY    hSub = hKey;
    BOOL    opened = FALSE;
    LONG    Error;
    DWORD   type;
    DWORD   cb;

    if ( ARGUMENT_PRESENT( lpSubKey ) && lpSubKey[0] != '\0' ) {
        Error = RegOpenKeyExA( hKey, lpSubKey, 0, KEY_QUERY_VALUE, &hSub );
        if ( Error != ERROR_SUCCESS ) {
            return Error;
        }
        opened = TRUE;
    }

    cb = ( ARGUMENT_PRESENT( lpcbValue ) && ARGUMENT_PRESENT( lpValue ) )
       ? (DWORD)*lpcbValue : 0;

    Error = RegQueryValueExA( hSub, NULL, NULL, &type, (LPBYTE)lpValue, &cb );

    if ( ARGUMENT_PRESENT( lpcbValue ) ) {
        *lpcbValue = (LONG)cb;
    }

    if ( opened ) {
        RegCloseKey( hSub );
    }

    return Error;
}

LONG
APIENTRY
RegSetValueW(
    IN HKEY hKey,
    IN LPCWSTR lpSubKey,
    IN DWORD dwType,
    IN LPCWSTR lpData,
    IN DWORD cbData
    )
{
    HKEY    hSub = hKey;
    BOOL    opened = FALSE;
    LONG    Error;
    DWORD   cb;

    UNREFERENCED_PARAMETER( cbData );

    //
    // The Win 3.1 form only ever stored REG_SZ default values.
    //
    if ( dwType != REG_SZ ) {
        return ERROR_INVALID_PARAMETER;
    }

    if ( !ARGUMENT_PRESENT( lpData ) ) {
        lpData = L"";
    }

    if ( ARGUMENT_PRESENT( lpSubKey ) && lpSubKey[0] != L'\0' ) {
        Error = RegCreateKeyW( hKey, lpSubKey, &hSub );
        if ( Error != ERROR_SUCCESS ) {
            return Error;
        }
        opened = TRUE;
    }

    cb = ( (DWORD)wcslen( lpData ) + 1 ) * sizeof( WCHAR );
    Error = RegSetValueExW( hSub, NULL, 0, REG_SZ, (CONST BYTE *)lpData, cb );

    if ( opened ) {
        RegCloseKey( hSub );
    }

    return Error;
}

LONG
APIENTRY
RegSetValueA(
    IN HKEY hKey,
    IN LPCSTR lpSubKey,
    IN DWORD dwType,
    IN LPCSTR lpData,
    IN DWORD cbData
    )
{
    HKEY    hSub = hKey;
    BOOL    opened = FALSE;
    LONG    Error;
    DWORD   cb;

    UNREFERENCED_PARAMETER( cbData );

    if ( dwType != REG_SZ ) {
        return ERROR_INVALID_PARAMETER;
    }

    if ( !ARGUMENT_PRESENT( lpData ) ) {
        lpData = "";
    }

    if ( ARGUMENT_PRESENT( lpSubKey ) && lpSubKey[0] != '\0' ) {
        Error = RegCreateKeyA( hKey, lpSubKey, &hSub );
        if ( Error != ERROR_SUCCESS ) {
            return Error;
        }
        opened = TRUE;
    }

    cb = (DWORD)strlen( lpData ) + 1;
    Error = RegSetValueExA( hSub, NULL, 0, REG_SZ, (CONST BYTE *)lpData, cb );

    if ( opened ) {
        RegCloseKey( hSub );
    }

    return Error;
}
