/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    init.c

Abstract:

    shell32.dll initialization (MicroNT-owned).

    The stock shell32 LibMain connected to the shell hook and the window
    server.  MicroNT's shell32 carries only the headless surface
    (ShellExecute / FindExecutable, CommandLineToArgvW, the Str* and She*
    helpers, environment substitution), so init just records the module
    handle and opts out of thread-library notifications.

--*/

#include <windows.h>
#include "shell.h"

BOOLEAN
DllInitialize(
    IN PVOID hmod,
    IN ULONG Reason,
    IN PCONTEXT Context
    )
{
    UNREFERENCED_PARAMETER( Context );

    if ( Reason == DLL_PROCESS_ATTACH ) {
        hInstance = (HANDLE)hmod;
        DisableThreadLibraryCalls( (HMODULE)hmod );
    }

    return( TRUE );
}
