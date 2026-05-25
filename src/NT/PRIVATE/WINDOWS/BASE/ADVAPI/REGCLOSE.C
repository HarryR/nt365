/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regclose.c

Abstract:

    MicroNT-owned collapse of the Win32 registry RegCloseKey API.

    The NT 3.5 winreg client/server/RPC split is gone — this calls ntdll
    directly (BaseRegCloseKey's body was just NtClose).  Predefined roots
    (HKEY_CLASSES_ROOT .. HKEY_PERFORMANCE_NLSTEXT) are 0x8000000x pseudo-
    handles, not real Nt keys, so closing one is a no-op.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>

//
// Predefined registry roots are 0x8000000x pseudo-handles; a real Nt key
// handle never has the high bit set.
//
#define IS_PREDEFINED_HKEY( h )   ( ( (ULONG)(h) & 0x80000000 ) != 0 )

LONG
APIENTRY
RegCloseKey(
    IN HKEY hKey
    )
{
    NTSTATUS Status;

    if ( IS_PREDEFINED_HKEY( hKey ) ) {
        return ERROR_SUCCESS;
    }

    Status = NtClose( (HANDLE)hKey );
    return (LONG)RtlNtStatusToDosError( Status );
}
