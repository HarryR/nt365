/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    regadvp.h

Abstract:

    Private shared declarations for MicroNT's collapsed Win32 registry
    (advapi32-owned, direct-ntdll, no winreg.dll/RPC/LOCAL).

--*/

#ifndef _REGADVP_H_
#define _REGADVP_H_

//
// Predefined registry roots are 0x8000000x pseudo-handles; a real Nt key
// handle never has the high bit set.
//
#define IS_PREDEFINED_HKEY( h )   ( ( (ULONG)(h) & 0x80000000 ) != 0 )

//
// Resolve a Win32 HKEY (a predefined root or an already-open key) to a real
// Nt key handle.  For a predefined root a fresh handle is opened and *pIsTemp
// is set TRUE -- the caller must NtClose( *pReal ) when done.  For a real key
// *pReal == hKey and *pIsTemp is FALSE.  Returns a Win32 error code;
// unsupported predefined roots (perf / current-config / dyn-data) yield
// ERROR_INVALID_HANDLE.
//
LONG
RegpResolveKey(
    IN HKEY hKey,
    IN REGSAM samDesired,
    OUT PHANDLE pReal,
    OUT PBOOL pIsTemp
    );

#endif // _REGADVP_H_
