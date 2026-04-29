/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    baseinit.c

Abstract:

    This module implements Win32 base initialization

Author:

    Mark Lucovsky (markl) 26-Sep-1990

Revision History:

--*/

#include "basedll.h"
#include <ntverp.h>     // VER_PRODUCT{MAJOR,MINOR}VERSION + VER_PRODUCTBUILD{,_QFE}

//
// Divides by 10000
//

ULONG BaseGetTickMagicMultiplier = 10000;
LARGE_INTEGER BaseGetTickMagicDivisor = { 0xd1b71758, 0xe219652c };
CCHAR BaseGetTickMagicShiftCount = 13;
BOOLEAN BaseRunningInServerProcess;

WCHAR BaseDefaultPathBuffer[ 2048 ];

BOOLEAN BasepFileApisAreOem = FALSE;

VOID
WINAPI
SetFileApisToOEM(
    VOID
    )
{
    BasepFileApisAreOem = TRUE;
}

VOID
WINAPI
SetFileApisToANSI(
    VOID
    )
{
    BasepFileApisAreOem = FALSE;
}

BOOL
WINAPI
AreFileApisANSI(
    VOID
    )
{
    return !BasepFileApisAreOem;
}


//
// MicroNT: csrss-free.  ConDllInitialize lives in conlib (WINCON/CLIENT)
// and does the LPC connect to consrv inside csrss.  We drop conlib
// entirely; std-handle wiring goes straight to PEB->ProcessParameters.
// Forward decl elided to avoid a stale prototype.
//

BOOLEAN
NlsDllInitialize(
    IN PVOID DllHandle,
    IN ULONG Reason,
    IN PCONTEXT Context OPTIONAL
    );

//
// MicroNT: csrss-free.  IconHackORama (called from BaseDllInitialize)
// loaded user32 to make icon-bearing console apps display correctly.
// We have no user32 yet and no console subsystem — drop the helper
// and its call site.  QuickThreadCreateRoutine + SETQUICKROUTINE
// existed only to inject CreateThread into csrss's quick-thread path
// (BaseRunningInServerProcess case); also dropped.
//


BOOLEAN
BaseDllInitialize(
    IN PVOID DllHandle,
    IN ULONG Reason,
    IN PCONTEXT Context OPTIONAL
    )

/*++

Routine Description:

    This function implements Win32 base dll initialization.
    It's primary purpose is to create the Base heap.

Arguments:

    DllHandle - Saved in BaseDllHandle global variable

    Context - Not Used

Return Value:

    STATUS_SUCCESS

--*/

{
    BOOLEAN Success;
    NTSTATUS Status;
    PPEB Peb;
    LPWSTR p, p1;

    BaseDllHandle = (HANDLE)DllHandle;

    (VOID)Context;

    Success = TRUE;

    Peb = NtCurrentPeb();

    switch ( Reason ) {

    case DLL_PROCESS_ATTACH:

        DisableThreadLibraryCalls(DllHandle);

        BaseIniFileUpdateCount = 0;

        Status = BaseDllInitializeMemoryManager();
        if (!NT_SUCCESS( Status )) {
            return( FALSE );
            }


        BaseAtomTable = NULL;

        RtlInitUnicodeString( &BaseDefaultPath, NULL );

        //
        // MicroNT: csrss-free.  Original kernel32 here:
        //   - IconHackORama() to load user32 for console apps
        //   - ConDllInitialize() to connect to consrv (csrss)
        //   - CsrClientConnectToServer(BASESRV_SERVERDLL_INDEX) to bind
        //     basesrv and read BaseStaticServerData
        //   - CsrNewThread() to register the initial thread with csrss
        // None of those exist in MicroNT.  Version comes from the build
        // (no PEB version fields in NT 3.5; basesrv was the canonical
        // owner).  System directories are hardcoded: \SystemRoot is a
        // kernel-installed symbolic link, resolved by the OB path-walker
        // on every NtOpenFile, so callers get correct files without us
        // doing the symlink walk at init.
        //

        // Stamped by src/tools/stamp-version.py (libversion.py owns the
        // single source of truth — see NTVERP.H comment block).
        BaseWindowsMajorVersion = VER_PRODUCTMAJORVERSION;
        BaseWindowsMinorVersion = VER_PRODUCTMINORVERSION;
        BaseBuildNumber         = VER_PRODUCTBUILD;
        BaseCSDVersion          = VER_PRODUCTBUILD_QFE;

        RtlInitUnicodeString( &BaseWindowsDirectory,
                              L"\\SystemRoot" );
        RtlInitUnicodeString( &BaseWindowsSystemDirectory,
                              L"\\SystemRoot\\System32" );

        //
        // MicroNT: populate a process-local BASE_STATIC_SERVER_DATA so
        // every BASE/CLIENT consumer that reads BaseStaticServerData->
        // SysInfo.{PageSize,AllocationGranularity,NumberOfPhysicalPages,
        // MaximumUserModeAddress,MinimumUserModeAddress,
        // ActiveProcessorsAffinityMask,...} or
        // BaseStaticServerData->{NamedObjectDirectory,WindowsDirectory,
        // WindowsMajorVersion,...} doesn't NULL-deref.
        //
        // In stock NT 3.5 this struct lived in basesrv-published shared
        // memory mapped read-only into every process's PEB; we own a
        // private copy here.  SysInfo comes from
        // NtQuerySystemInformation; the directory + version fields
        // mirror the globals we already initialised above.  Macros
        // ROUND_UP_TO_PAGES / ROUND_DOWN_TO_PAGES (BASEDLL.H) and the
        // global memory / process / file APIs use this struct.
        //
        {
            static BASE_STATIC_SERVER_DATA BaseLocalServerData;
            // Stack-local SysInfo so ProbeForWrite's 4-byte alignment
            // check passes.  &BaseLocalServerData.SysInfo lands at
            // offset 0x11E (24 bytes of UNICODE_STRINGs + 6 bytes of
            // USHORTs + 256 bytes of WCHAR[128]) which is 2 mod 4 —
            // BASE_STATIC_SERVER_DATA was designed assuming pack(2)
            // and never to be the target of ProbeForWrite directly.
            // We query into the stack copy then memcpy across.
            SYSTEM_BASIC_INFORMATION TempSysInfo;
            ULONG ReturnLength = 0;
            NTSTATUS QueryStatus;

            QueryStatus = NtQuerySystemInformation(
                SystemBasicInformation,
                &TempSysInfo,
                sizeof(TempSysInfo),
                &ReturnLength);
            if (NT_SUCCESS(QueryStatus)) {
                RtlMoveMemory(&BaseLocalServerData.SysInfo,
                              &TempSysInfo,
                              sizeof(SYSTEM_BASIC_INFORMATION));
            } else {
                // Fallback to i386-canonical defaults if the query
                // failed (shouldn't with a stack-local buffer; kept
                // as a belt-and-braces guard).
                DbgPrint("BASEDLL: NtQuerySystemInformation(SystemBasic) "
                         "failed %lx — using hardcoded SysInfo defaults\n",
                         QueryStatus);
                BaseLocalServerData.SysInfo.OemMachineId               = 0;
                BaseLocalServerData.SysInfo.TimerResolution            = 100000;       // 10 ms in 100ns units
                BaseLocalServerData.SysInfo.PageSize                   = 0x1000;       // i386 4 KB
                BaseLocalServerData.SysInfo.NumberOfPhysicalPages      = 0x4000;       // 64 MB / 4 KB
                BaseLocalServerData.SysInfo.LowestPhysicalPageNumber   = 1;
                BaseLocalServerData.SysInfo.HighestPhysicalPageNumber  = 0x4000;
                BaseLocalServerData.SysInfo.AllocationGranularity      = 0x10000;      // 64 KB (i386 NT)
                BaseLocalServerData.SysInfo.MinimumUserModeAddress     = 0x10000;
                BaseLocalServerData.SysInfo.MaximumUserModeAddress     = 0x7FFEFFFF;
                BaseLocalServerData.SysInfo.ActiveProcessorsAffinityMask = 1;
                BaseLocalServerData.SysInfo.NumberOfProcessors         = 1;
            }

            BaseLocalServerData.WindowsDirectory       = BaseWindowsDirectory;
            BaseLocalServerData.WindowsSystemDirectory = BaseWindowsSystemDirectory;
            RtlInitUnicodeString(&BaseLocalServerData.NamedObjectDirectory,
                                 L"\\BaseNamedObjects");
            BaseLocalServerData.WindowsMajorVersion = VER_PRODUCTMAJORVERSION;
            BaseLocalServerData.WindowsMinorVersion = VER_PRODUCTMINORVERSION;
            BaseLocalServerData.BuildNumber         = VER_PRODUCTBUILD;
            BaseLocalServerData.CSDVersion[0]       = UNICODE_NULL;
            BaseLocalServerData.IniFileMapping      = NULL;

            BaseStaticServerData = &BaseLocalServerData;
        }

        RtlInitUnicodeString(&BaseConsoleInput,L"CONIN$");
        RtlInitUnicodeString(&BaseConsoleOutput,L"CONOUT$");
        RtlInitUnicodeString(&BaseConsoleGeneric,L"CON");

        BaseUnicodeCommandLine = *(PUNICODE_STRING)&(NtCurrentPeb()->ProcessParameters->CommandLine);
        Status = RtlUnicodeStringToAnsiString(
                    &BaseAnsiCommandLine,
                    &BaseUnicodeCommandLine,
                    TRUE
                    );
        if ( !NT_SUCCESS(Status) ){
            BaseAnsiCommandLine.Buffer = NULL;
            BaseAnsiCommandLine.Length = 0;
            BaseAnsiCommandLine.MaximumLength = 0;
            }

        p = BaseDefaultPathBuffer;
        *p++ = L'.';
        *p++ = L';';

        p1 = BaseWindowsSystemDirectory.Buffer;
        while( *p = *p1++) {
            p++;
            }
        *p++ = L';';

        //
        // 16bit system directory follows 32bit system directory
        //
        p1 = BaseWindowsDirectory.Buffer;
        while( *p = *p1++) {
            p++;
            }
        p1 = L"\\system";
        while( *p = *p1++) {
            p++;
            }
        *p++ = L';';

        p1 = BaseWindowsDirectory.Buffer;
        while( *p = *p1++) {
            p++;
            }
        *p++ = L';';
        *p = UNICODE_NULL;

        BaseDefaultPath.Buffer = BaseDefaultPathBuffer;
        BaseDefaultPath.Length = (USHORT)((ULONG)p - (ULONG)BaseDefaultPathBuffer);
        BaseDefaultPath.MaximumLength = sizeof( BaseDefaultPathBuffer );

        BaseDefaultPathAppend.Buffer = p;
        BaseDefaultPathAppend.Length = 0;
        BaseDefaultPathAppend.MaximumLength = (USHORT)
            (BaseDefaultPath.MaximumLength - BaseDefaultPath.Length);

        RtlInitUnicodeString(&BasePathVariableName,L"PATH");
        RtlInitUnicodeString(&BaseTmpVariableName,L"TMP");
        RtlInitUnicodeString(&BaseTempVariableName,L"TEMP");
        RtlInitUnicodeString(&BaseDotVariableName,L".");
        RtlInitUnicodeString(&BaseDotTmpSuffixName,L".tmp");
        RtlInitUnicodeString(&BaseDotComSuffixName,L".com");
	RtlInitUnicodeString(&BaseDotPifSuffixName,L".pif");
        RtlInitUnicodeString(&BaseDotExeSuffixName,L".exe");

        //
        // MicroNT: csrss-free.  Original BaseDllInitializeIniFileMappings
        // walked BaseStaticServerData->IniFileMapping[] (basesrv-published)
        // to set up registry-redirected win.ini / system.ini access.
        // Toolchain doesn't read .ini; we have no basesrv data; skip.
        //

        if ( Peb->ProcessParameters ) {
            if ( Peb->ProcessParameters->Flags & RTL_USER_PROC_PROFILE_USER ) {

                LoadLibrary("psapi.dll");

                }

            if (Peb->ProcessParameters->DebugFlags) {
                DbgBreakPoint();
                }
            }

        //
        // call the NLS API initialization routine
        //
        if ( !NlsDllInitialize(DllHandle,Reason,Context) ) {
            return FALSE;
            }

        break;

    case DLL_PROCESS_DETACH:

        //
        // call the NLS API termination routine
        //
        if ( !NlsDllInitialize(DllHandle,Reason,Context) ) {
            return FALSE;
            }

        //
        // If app wrote to any profile files, then flush them to disk.
        //

        if (BaseIniFileUpdateCount != 0) {
            WriteProfileStringW( NULL, NULL, NULL );
            }

        break;
    default:
        break;
    }

    return Success;
}


//
// MicroNT: csrss-free.  QuickThreadCreateRoutine was registered with
// CsrSetQuickThreadCreateRoutine when kernel32 ran inside csrss
// itself, so server-internal CsrCreateRemoteThread calls bypassed
// the client-side LPC roundtrip.  No csrss → no caller; deleted.
//

//
// MicroNT: csrss-free.  Original kernel32 read the directory path
// (UNICODE_STRING containing L"\BaseNamedObjects") from
// BaseStaticServerData — basesrv published it during csrss init so
// every process picked up the same canonical path.  The directory
// itself lives in the NT object namespace, not inside csrss; we just
// hardcode the well-known name.  smss / our boot publisher creates
// the \BaseNamedObjects directory with OBJ_PERMANENT exactly the same
// way it does \NLS, so the NtOpenDirectoryObject call below works
// without csrss running.
//
HANDLE
BaseGetNamedObjectDirectory(
    VOID
    )
{
    OBJECT_ATTRIBUTES Obja;
    UNICODE_STRING    NameStr;
    NTSTATUS          Status;

    RtlAcquirePebLock();

    if ( !BaseNamedObjectDirectory ) {
        RtlInitUnicodeString( &NameStr, L"\\BaseNamedObjects" );
        InitializeObjectAttributes( &Obja,
                                    &NameStr,
                                    OBJ_CASE_INSENSITIVE,
                                    NULL,
                                    NULL
                                    );
        Status = NtOpenDirectoryObject( &BaseNamedObjectDirectory,
                                        DIRECTORY_ALL_ACCESS,
                                        &Obja
                                      );
        if ( !NT_SUCCESS(Status) ) {
            BaseNamedObjectDirectory = NULL;
            }
        }
    RtlReleasePebLock();
    return BaseNamedObjectDirectory;
}
