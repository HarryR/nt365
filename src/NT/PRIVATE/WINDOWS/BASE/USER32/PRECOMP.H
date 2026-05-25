/*++

Copyright (c) 1985-91  Microsoft Corporation

Module Name:

    precomp.h

Abstract:

    Build glue for MicroNT's owned user32.dll.  The original USER/CLIENT
    precomp.h dragged in the whole window-server protocol (USERCLI.H,
    gpsi, the csrss connection).  MicroNT's user32 carries only the
    self-contained client-side surface -- string / character / rectangle
    / wsprintf / resource-string helpers -- so this header supplies just
    what those modules need: the NT + Win32 base headers, the ANSI<->wide
    translation wrappers the string code calls by macro, and no-op debug
    RIP macros.  Nothing here talks to csrss, lsass, or gdi.

--*/

#ifndef _USER32_PRECOMP_H_
#define _USER32_PRECOMP_H_

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>

#include <stdarg.h>
#include <string.h>

//
// Debug RIP reporting.  In the full USER build these expand to Shred() /
// UserSetError() against the shared server info.  MicroNT's user32 has no
// server attachment, so they are no-ops (the callers do not depend on the
// side effect -- every RIP site already falls through to a best-effort
// result).
//
#define SRIP0( idErr, szFmt )                       ((void)0)
#define SRIP1( idErr, szFmt, p1 )                   ((void)0)
#define SRIP2( idErr, szFmt, p1, p2 )               ((void)0)
#define SRIP3( idErr, szFmt, p1, p2, p3 )           ((void)0)

//
// UserAssert is a retail no-op (in DBG builds it RIPs).  MicroNT builds
// retail, so match the retail definition.
//
#define UserAssert( exp )                           ((void)0)

//
// ANSI<->Unicode translation helpers (chartran.c).  The string modules use
// the ACP-defaulting MBToWCS / WCSToMB shorthands; in the full build these
// are defined in the clipboard headers.  Mirror them here against the
// currently installed code page (CP_ACP).
//
int
WCSToMBEx(
    WORD wCodePage,
    LPCWSTR pUnicodeString,
    int cchUnicodeString,
    LPSTR *ppAnsiString,
    int nAnsiChar,
    BOOL bAllocateMem);

int
MBToWCSEx(
    WORD wCodePage,
    LPCSTR pAnsiString,
    int nAnsiChar,
    LPWSTR *ppUnicodeString,
    int cchUnicodeString,
    BOOL bAllocateMem);

#define MBToWCS( a, b, c, d, e )   MBToWCSEx( CP_ACP, (a), (b), (c), (d), (e) )
#define WCSToMB( a, b, c, d, e )   WCSToMBEx( CP_ACP, (a), (b), (c), (d), (e) )

//
// Message wParam character translation (chartran.c).  Exported to the rest
// of the would-be USER client; declared here so the definitions match.
//
BOOL RtlWCSMessageWParamCharToMB( DWORD msg, PDWORD pWParam );
BOOL RtlMBMessageWParamCharToWCS( DWORD msg, PDWORD pWParam );

#endif // _USER32_PRECOMP_H_
