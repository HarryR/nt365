/*++

Copyright (c) 2026 MicroNT

Module Name:

    stdio.c

Abstract:

    csrss-free conlib replacement.

    Stock NT 3.5 kernel32 links against conlib (WINCON/CLIENT) which
    LPCs every console-API call into consrv inside csrss.  MicroNT
    has no csrss; we drop conlib entirely and provide kernel32's
    console export surface here.

    Three policy tiers:

      Tier A — PEB-backed real impls.  GetStdHandle / SetStdHandle
        read and write Peb->ProcessParameters->StandardInput/Output/
        Error.  WriteConsoleA / ReadConsoleA route to WriteFile /
        ReadFile (the std handle is a pipe or file underneath, not a
        console).  GetConsoleCP / GetConsoleOutputCP return GetACP /
        GetOEMCP.  GetConsoleMode / GetConsoleScreenBufferInfo /
        FlushConsoleInputBuffer fail with ERROR_INVALID_HANDLE — the
        toolchain treats that as "stdout isn't a tty", which is the
        correct answer for the pipe-backed handles MicroNT hands
        every process.  ConDllInitialize (gutted from BaseInit) is
        a no-op TRUE for binary compat.

      Tier B — fail-loud binary-compat stubs.  Each KERNEL32.SRC
        console export not internally referenced has a stub body
        that SetLastError(ERROR_INVALID_HANDLE) / return FALSE.  A
        third-party EXE GetProcAddress'ing kernel32!Foo finds a
        non-NULL pointer that fails predictably.

      Tier C — silent-OK.  SetConsoleCtrlHandler returns TRUE
        without storing the handler — common Ctrl+C-registration
        pattern that legitimate EXEs do at startup; failing breaks
        them.

    The KERNEL32.SRC export list is the source of truth.  Any new
    console-tier export added there needs a matching stub here.

Author:

    MicroNT (csrss-free port from NT 3.5 conlib)

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include "basedll.h"
#include <conapi.h>     // private console types: CONSOLE_FONT_INFO, GetConsoleHardwareState, ConsoleSubst, ...
#include <conroute.h>   // OpenConsoleW / DuplicateConsoleHandle / VerifyConsoleIoHandle / etc.

/* --------------------------------------------------------------------- */
/*  Globals BASEINIT.C still initialises (BaseConsoleInput etc.)         */
/*  Kept as zero-init UNICODE_STRINGs so RtlInitUnicodeString() in       */
/*  BaseDllInitialize doesn't reference an undefined symbol.             */
/* --------------------------------------------------------------------- */

UNICODE_STRING BaseConsoleInput;
UNICODE_STRING BaseConsoleOutput;
UNICODE_STRING BaseConsoleGeneric;

/* --------------------------------------------------------------------- */
/*  Tier A — real implementations                                         */
/* --------------------------------------------------------------------- */

/*
 * GetStdHandle / SetStdHandle live in filehops.c (already PEB-backed in
 * the original NT 3.5 source — they were stream-handle wrappers over
 * Peb->ProcessParameters->Standard{Input,Output,Error}, no csrss
 * involvement).  We don't redefine them here.
 */

BOOL
WINAPI
WriteConsoleA(
    HANDLE      hConsoleOutput,
    CONST VOID *lpBuffer,
    DWORD       nNumberOfCharsToWrite,
    LPDWORD     lpNumberOfCharsWritten,
    LPVOID      lpReserved
    )
{
    UNREFERENCED_PARAMETER(lpReserved);
    return WriteFile(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite,
                     lpNumberOfCharsWritten, NULL);
}

BOOL
WINAPI
WriteConsoleW(
    HANDLE      hConsoleOutput,
    CONST VOID *lpBuffer,
    DWORD       nNumberOfCharsToWrite,
    LPDWORD     lpNumberOfCharsWritten,
    LPVOID      lpReserved
    )
{
    /* PEB std handle is a byte pipe; W variant writes raw wide bytes. */
    UNREFERENCED_PARAMETER(lpReserved);
    return WriteFile(hConsoleOutput, lpBuffer,
                     nNumberOfCharsToWrite * sizeof(WCHAR),
                     lpNumberOfCharsWritten, NULL);
}

BOOL
WINAPI
ReadConsoleA(
    HANDLE  hConsoleInput,
    LPVOID  lpBuffer,
    DWORD   nNumberOfCharsToRead,
    LPDWORD lpNumberOfCharsRead,
    LPVOID  lpReserved
    )
{
    UNREFERENCED_PARAMETER(lpReserved);
    return ReadFile(hConsoleInput, lpBuffer, nNumberOfCharsToRead,
                    lpNumberOfCharsRead, NULL);
}

BOOL
WINAPI
ReadConsoleW(
    HANDLE  hConsoleInput,
    LPVOID  lpBuffer,
    DWORD   nNumberOfCharsToRead,
    LPDWORD lpNumberOfCharsRead,
    LPVOID  lpReserved
    )
{
    UNREFERENCED_PARAMETER(lpReserved);
    return ReadFile(hConsoleInput, lpBuffer,
                    nNumberOfCharsToRead * sizeof(WCHAR),
                    lpNumberOfCharsRead, NULL);
}

UINT WINAPI GetConsoleCP(VOID)       { return GetACP();  }
UINT WINAPI GetConsoleOutputCP(VOID) { return GetOEMCP(); }

BOOLEAN
ConDllInitialize(
    IN PVOID    DllHandle,
    IN ULONG    Reason,
    IN PCONTEXT Context OPTIONAL
    )
{
    /* MicroNT: csrss-free.  BaseDllInitialize no longer calls this
     * (the call site was removed); we keep it exported for any
     * out-of-tree binary-compat consumer.  Always succeeds. */
    UNREFERENCED_PARAMETER(DllHandle);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Context);
    return TRUE;
}

/* --------------------------------------------------------------------- */
/*  Tier C — silent-OK                                                    */
/* --------------------------------------------------------------------- */

BOOL
WINAPI
SetConsoleCtrlHandler(
    PHANDLER_ROUTINE HandlerRoutine,
    BOOL             Add
    )
{
    /* Common pattern: well-behaved Win32 EXEs register a Ctrl+C
     * handler at startup.  Silently accepting + ignoring keeps them
     * loadable; we have no console-event source to dispatch to
     * anyway. */
    UNREFERENCED_PARAMETER(HandlerRoutine);
    UNREFERENCED_PARAMETER(Add);
    return TRUE;
}

/* --------------------------------------------------------------------- */
/*  Tier B — fail-loud binary-compat stubs                                */
/*                                                                        */
/*  Macro takes a parenthesized arg list as a single token so older       */
/*  MSVC (no variadic macros) can splice it.  One macro per return        */
/*  type because each fail value is type-specific.                        */
/* --------------------------------------------------------------------- */

#define STUB_BOOL(NAME, ARGS) \
    BOOL WINAPI NAME ARGS \
    { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

#define STUB_DWORD(NAME, ARGS) \
    DWORD WINAPI NAME ARGS \
    { SetLastError(ERROR_INVALID_HANDLE); return 0; }

#define STUB_HANDLE(NAME, ARGS) \
    HANDLE WINAPI NAME ARGS \
    { SetLastError(ERROR_INVALID_HANDLE); return INVALID_HANDLE_VALUE; }

#define STUB_HMENU(NAME, ARGS) \
    HMENU WINAPI NAME ARGS \
    { SetLastError(ERROR_INVALID_HANDLE); return NULL; }

#define STUB_VOID(NAME, ARGS) \
    VOID WINAPI NAME ARGS { SetLastError(ERROR_INVALID_HANDLE); }

#define STUB_COORD(NAME, ARGS) \
    COORD WINAPI NAME ARGS \
    { COORD z = {0,0}; SetLastError(ERROR_INVALID_HANDLE); return z; }

/* --- Console session lifecycle --- */
STUB_BOOL  (AllocConsole, (VOID))
STUB_BOOL  (FreeConsole,  (VOID))

/* --- Screen buffer lifecycle --- */
STUB_HANDLE(CreateConsoleScreenBuffer,
    (DWORD a, DWORD b, CONST SECURITY_ATTRIBUTES *c, DWORD d, LPVOID e))
STUB_BOOL  (SetConsoleActiveScreenBuffer,    (HANDLE h))
STUB_BOOL  (SetConsoleScreenBufferSize,      (HANDLE h, COORD s))

/* --- Mode + screen-buffer info --- */
STUB_BOOL  (GetConsoleMode,                  (HANDLE h, LPDWORD m))
STUB_BOOL  (SetConsoleMode,                  (HANDLE h, DWORD  m))
STUB_BOOL  (GetConsoleScreenBufferInfo,
    (HANDLE h, PCONSOLE_SCREEN_BUFFER_INFO p))
STUB_BOOL  (FlushConsoleInputBuffer,         (HANDLE h))
STUB_BOOL  (GetNumberOfConsoleInputEvents,   (HANDLE h, LPDWORD n))
STUB_COORD (GetLargestConsoleWindowSize,     (HANDLE h))

/* --- Cursor --- */
STUB_BOOL  (GetConsoleCursorInfo,            (HANDLE h, PCONSOLE_CURSOR_INFO i))
STUB_BOOL  (SetConsoleCursorInfo,
    (HANDLE h, CONST CONSOLE_CURSOR_INFO *i))
STUB_BOOL  (SetConsoleCursorPosition,        (HANDLE h, COORD c))
STUB_BOOL  (SetConsoleCursor,                (HANDLE h, HCURSOR c))
STUB_DWORD (ShowConsoleCursor,               (HANDLE h, BOOL b))

/* --- Window + display --- */
STUB_BOOL  (SetConsoleWindowInfo,
    (HANDLE h, BOOL absolute, CONST SMALL_RECT *r))
STUB_BOOL  (SetConsoleTextAttribute,         (HANDLE h, WORD a))
STUB_BOOL  (GetConsoleDisplayMode,           (LPDWORD m))
STUB_BOOL  (SetConsoleDisplayMode,           (HANDLE h, DWORD m, PCOORD c))
STUB_BOOL  (SetConsoleMaximumWindowSize,     (HANDLE h, COORD c))

/* --- Title ---
 * MicroNT: stub-but-NUL-terminate.  Real NT's GetConsoleTitle writes
 * an empty string + NUL when there's no title; callers (cmd.exe's
 * Init() at CINIT.C:358) then mystrcpy() the result without checking
 * the return value.  A bare-stub that doesn't touch the buffer
 * leaves uninitialised heap content there and the wcscpy walks off
 * into uncommitted memory.  Cheap fix: write a single NUL. */
DWORD WINAPI GetConsoleTitleA (LPSTR  buf, DWORD n) {
    if (buf && n > 0) buf[0] = 0;
    return 0;
}
DWORD WINAPI GetConsoleTitleW (LPWSTR buf, DWORD n) {
    if (buf && n > 0) buf[0] = 0;
    return 0;
}
STUB_BOOL  (SetConsoleTitleA,                (LPCSTR  s))
STUB_BOOL  (SetConsoleTitleW,                (LPCWSTR s))

/* --- Mouse --- */
STUB_BOOL  (GetNumberOfConsoleMouseButtons,  (LPDWORD n))

/* --- Codepages (other than the Tier-A getters) --- */
STUB_BOOL  (SetConsoleCP,                    (UINT cp))
STUB_BOOL  (SetConsoleOutputCP,              (UINT cp))

/* --- Input record I/O --- */
STUB_BOOL  (PeekConsoleInputA,
    (HANDLE h, PINPUT_RECORD r, DWORD n, LPDWORD nr))
STUB_BOOL  (PeekConsoleInputW,
    (HANDLE h, PINPUT_RECORD r, DWORD n, LPDWORD nr))
STUB_BOOL  (ReadConsoleInputA,
    (HANDLE h, PINPUT_RECORD r, DWORD n, LPDWORD nr))
STUB_BOOL  (ReadConsoleInputW,
    (HANDLE h, PINPUT_RECORD r, DWORD n, LPDWORD nr))
STUB_BOOL  (WriteConsoleInputA,
    (HANDLE h, CONST INPUT_RECORD *r, DWORD n, LPDWORD nw))
STUB_BOOL  (WriteConsoleInputW,
    (HANDLE h, CONST INPUT_RECORD *r, DWORD n, LPDWORD nw))

/* --- 2D character/attribute output --- */
STUB_BOOL  (ReadConsoleOutputA,
    (HANDLE h, PCHAR_INFO buf, COORD bs, COORD bc, PSMALL_RECT region))
STUB_BOOL  (ReadConsoleOutputW,
    (HANDLE h, PCHAR_INFO buf, COORD bs, COORD bc, PSMALL_RECT region))
STUB_BOOL  (WriteConsoleOutputA,
    (HANDLE h, CONST CHAR_INFO *buf, COORD bs, COORD bc, PSMALL_RECT region))
STUB_BOOL  (WriteConsoleOutputW,
    (HANDLE h, CONST CHAR_INFO *buf, COORD bs, COORD bc, PSMALL_RECT region))
STUB_BOOL  (ReadConsoleOutputCharacterA,
    (HANDLE h, LPSTR  buf, DWORD len, COORD c, LPDWORD nr))
STUB_BOOL  (ReadConsoleOutputCharacterW,
    (HANDLE h, LPWSTR buf, DWORD len, COORD c, LPDWORD nr))
STUB_BOOL  (WriteConsoleOutputCharacterA,
    (HANDLE h, LPCSTR  buf, DWORD len, COORD c, LPDWORD nw))
STUB_BOOL  (WriteConsoleOutputCharacterW,
    (HANDLE h, LPCWSTR buf, DWORD len, COORD c, LPDWORD nw))
STUB_BOOL  (ReadConsoleOutputAttribute,
    (HANDLE h, LPWORD a, DWORD n, COORD c, LPDWORD nr))
STUB_BOOL  (WriteConsoleOutputAttribute,
    (HANDLE h, CONST WORD *a, DWORD n, COORD c, LPDWORD nw))
STUB_BOOL  (FillConsoleOutputCharacterA,
    (HANDLE h, CHAR  ch, DWORD n, COORD c, LPDWORD nw))
STUB_BOOL  (FillConsoleOutputCharacterW,
    (HANDLE h, WCHAR ch, DWORD n, COORD c, LPDWORD nw))
STUB_BOOL  (FillConsoleOutputAttribute,
    (HANDLE h, WORD a, DWORD n, COORD c, LPDWORD nw))
STUB_BOOL  (ScrollConsoleScreenBufferA,
    (HANDLE h, CONST SMALL_RECT *src, CONST SMALL_RECT *clip,
     COORD dest, CONST CHAR_INFO *fill))
STUB_BOOL  (ScrollConsoleScreenBufferW,
    (HANDLE h, CONST SMALL_RECT *src, CONST SMALL_RECT *clip,
     COORD dest, CONST CHAR_INFO *fill))

/* --- Ctrl events (dispatch source) --- */
STUB_BOOL  (GenerateConsoleCtrlEvent,        (DWORD evt, DWORD pgrp))

/* --- Aliases (exe-name → string substitution).  CONAPI.H takes
 *     non-const LPSTR/LPWSTR throughout — match the declarations even
 *     though we only fail-stub. --- */
STUB_BOOL  (AddConsoleAliasA,
    (LPSTR  src, LPSTR  tgt, LPSTR  exe))
STUB_BOOL  (AddConsoleAliasW,
    (LPWSTR src, LPWSTR tgt, LPWSTR exe))
STUB_DWORD (GetConsoleAliasA,
    (LPSTR  src, LPSTR  buf, DWORD n, LPSTR  exe))
STUB_DWORD (GetConsoleAliasW,
    (LPWSTR src, LPWSTR buf, DWORD n, LPWSTR exe))
STUB_DWORD (GetConsoleAliasesA,        (LPSTR  buf, DWORD n, LPSTR  exe))
STUB_DWORD (GetConsoleAliasesW,        (LPWSTR buf, DWORD n, LPWSTR exe))
STUB_DWORD (GetConsoleAliasesLengthA,  (LPSTR  exe))
STUB_DWORD (GetConsoleAliasesLengthW,  (LPWSTR exe))
STUB_DWORD (GetConsoleAliasExesA,      (LPSTR  buf, DWORD n))
STUB_DWORD (GetConsoleAliasExesW,      (LPWSTR buf, DWORD n))
STUB_DWORD (GetConsoleAliasExesLengthA,(VOID))
STUB_DWORD (GetConsoleAliasExesLengthW,(VOID))

/* --- Command history --- */
STUB_VOID  (ExpungeConsoleCommandHistoryA, (LPSTR  exe))
STUB_VOID  (ExpungeConsoleCommandHistoryW, (LPWSTR exe))
STUB_BOOL  (SetConsoleNumberOfCommandsA,   (DWORD n, LPSTR  exe))
STUB_BOOL  (SetConsoleNumberOfCommandsW,   (DWORD n, LPWSTR exe))
STUB_DWORD (GetConsoleCommandHistoryLengthA, (LPSTR  exe))
STUB_DWORD (GetConsoleCommandHistoryLengthW, (LPWSTR exe))
STUB_DWORD (GetConsoleCommandHistoryA,
    (LPSTR  buf, DWORD n, LPSTR  exe))
STUB_DWORD (GetConsoleCommandHistoryW,
    (LPWSTR buf, DWORD n, LPWSTR exe))
STUB_BOOL  (SetConsoleCommandHistoryMode,    (DWORD mode))

/* --- Fonts --- */
STUB_BOOL  (SetConsoleFont,                  (HANDLE h, DWORD font))
STUB_BOOL  (GetCurrentConsoleFont,
    (HANDLE h, BOOL maxWindow, PCONSOLE_FONT_INFO pInfo))
STUB_COORD (GetConsoleFontSize,              (HANDLE h, DWORD font))
STUB_BOOL  (GetConsoleFontInfo,
    (HANDLE h, BOOL maxWindow, DWORD num, PCONSOLE_FONT_INFO pInfo))
STUB_DWORD (GetNumberOfConsoleFonts,         (VOID))

/* --- Hardware state / palette / shortcuts / menu --- */
STUB_BOOL  (GetConsoleHardwareState,
    (HANDLE h, PCOORD resolution, PCOORD fontSize))
STUB_BOOL  (SetConsoleHardwareState,
    (HANDLE h, COORD  resolution, COORD  fontSize))
STUB_BOOL  (SetConsolePalette,
    (HANDLE h, HPALETTE pal, UINT usage))
STUB_BOOL  (SetConsoleKeyShortcuts,
    (BOOL set, BYTE keys, LPAPPKEY appKeys, DWORD n))
STUB_BOOL  (SetConsoleMenuClose,             (BOOL b))
STUB_HMENU (ConsoleMenuControl,              (HANDLE h, DWORD a, DWORD b))

/* --- private handle ops --- */
STUB_BOOL  (CloseConsoleHandle,              (HANDLE h))
STUB_HANDLE(DuplicateConsoleHandle,
    (HANDLE h, DWORD access, BOOL inherit, DWORD options))
STUB_HANDLE(OpenConsoleW,
    (LPWSTR name, DWORD access, BOOL inherit, DWORD share))
STUB_HANDLE(GetConsoleInputWaitHandle,       (VOID))
STUB_BOOL  (VerifyConsoleIoHandle,           (HANDLE h))
STUB_BOOL  (ConsoleSubst,
    (DWORD drive, DWORD flag, LPWSTR buf, DWORD bufLen))

/* --- DIB / event-active hooks --- */
STUB_BOOL  (InvalidateConsoleDIBits,         (HANDLE h, PSMALL_RECT r))
STUB_VOID  (SetLastConsoleEventActive,       (VOID))

/* --------------------------------------------------------------------- */
/*  Backup* exports.  KERNEL32.SRC exports BackupRead/Seek/Write; the     */
/*  original NT 3.5 BACKUP.C is dropped in MicroNT (no incremental-      */
/*  backup consumers, no toolchain need).  Fail-loud stubs satisfy the   */
/*  link.                                                                 */
/* --------------------------------------------------------------------- */

STUB_BOOL  (BackupRead,
    (HANDLE h, LPBYTE buf, DWORD n, LPDWORD readOut, BOOL abort, BOOL secInfo,
     LPVOID *ctx))
STUB_BOOL  (BackupWrite,
    (HANDLE h, LPBYTE buf, DWORD n, LPDWORD wroteOut, BOOL abort, BOOL secInfo,
     LPVOID *ctx))
STUB_BOOL  (BackupSeek,
    (HANDLE h, DWORD seekLow, DWORD seekHigh, LPDWORD lowOut, LPDWORD highOut,
     LPVOID *ctx))
