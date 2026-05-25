/*++

Copyright (c) 1989  Microsoft Corporation

Module Name:

    init.c

Abstract:

    AdvApi32.dll initialization

Author:

    Robert Reichel (RobertRe) 8-12-92

Revision History:

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
    if ( Reason == DLL_PROCESS_ATTACH ) {
        DisableThreadLibraryCalls(hmod);
        }

    //
    // MicroNT: Sys003Initialize (Win 3.1 IO compat, win31io.c) is not
    // ported.  RegInitialize is wired once the registry collapse lands
    // under REG/.  Until then init is a no-op past the thread-library-
    // calls disable.
    //
    return( TRUE );
}
