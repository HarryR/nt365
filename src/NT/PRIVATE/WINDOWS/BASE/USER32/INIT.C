/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    init.c

Abstract:

    user32.dll initialization (MicroNT-owned).

    The stock USER client DLL entry (UserClientDllInitialize) connects the
    process to the window-server (csrss) and wires up the shared client/
    server state.  MicroNT's user32 is a leaf utility DLL -- strings,
    character classification, rectangles, wsprintf, resource strings -- with
    no server attachment, so initialization is just the thread-library-call
    opt-out.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>

BOOLEAN
DllInitialize(
    IN PVOID hmod,
    IN ULONG Reason,
    IN PCONTEXT Context
    )
{
    UNREFERENCED_PARAMETER( Context );

    if ( Reason == DLL_PROCESS_ATTACH ) {
        DisableThreadLibraryCalls( hmod );
    }

    return( TRUE );
}
