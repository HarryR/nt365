/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regmisc.c

Abstract:

    MicroNT-owned registry odds and ends: flush, key security, local
    RegConnectRegistry, and honest ERROR_CALL_NOT_IMPLEMENTED stubs for the
    admin-only operations (save / restore / replace / load / unload / change
    notification) that MicroNT does not provide -- no winreg server, no
    remote registry.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "regadvp.h"

LONG
APIENTRY
RegFlushKey(
    IN HKEY hKey
    )
{
    NTSTATUS    Status;
    HANDLE      key;
    BOOL        keyIsTemp;
    LONG        Error;

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    Status = NtFlushKey( key );

    if ( keyIsTemp ) {
        NtClose( key );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegGetKeySecurity(
    IN HKEY hKey,
    IN SECURITY_INFORMATION SecurityInformation,
    OUT PSECURITY_DESCRIPTOR pSecurityDescriptor,
    IN OUT LPDWORD lpcbSecurityDescriptor
    )
{
    NTSTATUS    Status;
    HANDLE      key;
    BOOL        keyIsTemp;
    LONG        Error;

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    Status = NtQuerySecurityObject( key, SecurityInformation,
                pSecurityDescriptor, *lpcbSecurityDescriptor,
                lpcbSecurityDescriptor );

    if ( keyIsTemp ) {
        NtClose( key );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

LONG
APIENTRY
RegSetKeySecurity(
    IN HKEY hKey,
    IN SECURITY_INFORMATION SecurityInformation,
    IN PSECURITY_DESCRIPTOR pSecurityDescriptor
    )
{
    NTSTATUS    Status;
    HANDLE      key;
    BOOL        keyIsTemp;
    LONG        Error;

    Error = RegpResolveKey( hKey, MAXIMUM_ALLOWED, &key, &keyIsTemp );
    if ( Error != ERROR_SUCCESS ) {
        return Error;
    }

    Status = NtSetSecurityObject( key, SecurityInformation, pSecurityDescriptor );

    if ( keyIsTemp ) {
        NtClose( key );
    }

    return (LONG)RtlNtStatusToDosError( Status );
}

//
// RegConnectRegistry: MicroNT is single-machine.  A NULL/empty machine name
// is the local machine -- hand back the predefined root unchanged (closing
// it is a no-op).  A remote machine is unsupported.
//

LONG
APIENTRY
RegConnectRegistryW(
    IN LPWSTR lpMachineName OPTIONAL,
    IN HKEY hKey,
    OUT PHKEY phkResult
    )
{
    if ( !ARGUMENT_PRESENT( lpMachineName ) || lpMachineName[0] == L'\0' ) {
        *phkResult = hKey;
        return ERROR_SUCCESS;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG
APIENTRY
RegConnectRegistryA(
    IN LPSTR lpMachineName OPTIONAL,
    IN HKEY hKey,
    OUT PHKEY phkResult
    )
{
    if ( !ARGUMENT_PRESENT( lpMachineName ) || lpMachineName[0] == '\0' ) {
        *phkResult = hKey;
        return ERROR_SUCCESS;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

//
// Admin-only operations MicroNT does not implement.
//

LONG APIENTRY
RegNotifyChangeKeyValue( IN HKEY h, IN BOOL b, IN DWORD f, IN HANDLE e, IN BOOL a )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( b );
    UNREFERENCED_PARAMETER( f ); UNREFERENCED_PARAMETER( e );
    UNREFERENCED_PARAMETER( a );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegSaveKeyW( IN HKEY h, IN LPCWSTR f, IN LPSECURITY_ATTRIBUTES s )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( f ); UNREFERENCED_PARAMETER( s );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegSaveKeyA( IN HKEY h, IN LPCSTR f, IN LPSECURITY_ATTRIBUTES s )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( f ); UNREFERENCED_PARAMETER( s );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegRestoreKeyW( IN HKEY h, IN LPCWSTR f, IN DWORD fl )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( f ); UNREFERENCED_PARAMETER( fl );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegRestoreKeyA( IN HKEY h, IN LPCSTR f, IN DWORD fl )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( f ); UNREFERENCED_PARAMETER( fl );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegReplaceKeyW( IN HKEY h, IN LPCWSTR sk, IN LPCWSTR nf, IN LPCWSTR of )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( sk );
    UNREFERENCED_PARAMETER( nf ); UNREFERENCED_PARAMETER( of );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegReplaceKeyA( IN HKEY h, IN LPCSTR sk, IN LPCSTR nf, IN LPCSTR of )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( sk );
    UNREFERENCED_PARAMETER( nf ); UNREFERENCED_PARAMETER( of );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegLoadKeyW( IN HKEY h, IN LPCWSTR sk, IN LPCWSTR f )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( sk ); UNREFERENCED_PARAMETER( f );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegLoadKeyA( IN HKEY h, IN LPCSTR sk, IN LPCSTR f )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( sk ); UNREFERENCED_PARAMETER( f );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegUnLoadKeyW( IN HKEY h, IN LPCWSTR sk )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( sk );
    return ERROR_CALL_NOT_IMPLEMENTED;
}

LONG APIENTRY
RegUnLoadKeyA( IN HKEY h, IN LPCSTR sk )
{
    UNREFERENCED_PARAMETER( h ); UNREFERENCED_PARAMETER( sk );
    return ERROR_CALL_NOT_IMPLEMENTED;
}
