

/*++ BUILD Version: 0001    // Increment this if a change has global effects

Copyright (c) 1985-91, Microsoft Corporation

Module Name:

    winbasep.h

Abstract:

    Private
    Procedure declarations, constant definitions and macros for the Base
    component.

--*/
#ifndef _WINBASEP_
#define _WINBASEP_
#ifdef __cplusplus
extern "C" {
#endif
#define FILE_FLAG_GLOBAL_HANDLE         0x00800000
#define FILE_FLAG_MM_CACHED_FILE_HANDLE 0x00400000
#define HFINDFILE HANDLE                        //
#define INVALID_HFINDFILE       ((HFINDFILE)-1) //
#if(WINVER < 0x0400)
#define STARTF_USEHOTKEY        0x00000200
#define STARTF_HASSHELLDATA     0x00000400
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)

WINBASEAPI
DWORD
WINAPI
GetPrivateProfileSectionNamesA(
    LPSTR lpszReturnBuffer,
    DWORD nSize,
    LPCSTR lpFileName
    );
WINBASEAPI
DWORD
WINAPI
GetPrivateProfileSectionNamesW(
    LPWSTR lpszReturnBuffer,
    DWORD nSize,
    LPCWSTR lpFileName
    );
#ifdef UNICODE
#define GetPrivateProfileSectionNames  GetPrivateProfileSectionNamesW
#else
#define GetPrivateProfileSectionNames  GetPrivateProfileSectionNamesA
#endif // !UNICODE

WINBASEAPI
BOOL
WINAPI
GetPrivateProfileStructA(
    LPCSTR lpszSection,
    LPCSTR lpszKey,
    LPVOID   lpStruct,
    UINT     uSizeStruct,
    LPCSTR szFile
    );
WINBASEAPI
BOOL
WINAPI
GetPrivateProfileStructW(
    LPCWSTR lpszSection,
    LPCWSTR lpszKey,
    LPVOID   lpStruct,
    UINT     uSizeStruct,
    LPCWSTR szFile
    );
#ifdef UNICODE
#define GetPrivateProfileStruct  GetPrivateProfileStructW
#else
#define GetPrivateProfileStruct  GetPrivateProfileStructA
#endif // !UNICODE

WINBASEAPI
BOOL
WINAPI
WritePrivateProfileStructA(
    LPCSTR lpszSection,
    LPCSTR lpszKey,
    LPVOID   lpStruct,
    UINT     uSizeStruct,
    LPCSTR szFile
    );
WINBASEAPI
BOOL
WINAPI
WritePrivateProfileStructW(
    LPCWSTR lpszSection,
    LPCWSTR lpszKey,
    LPVOID   lpStruct,
    UINT     uSizeStruct,
    LPCWSTR szFile
    );
#ifdef UNICODE
#define WritePrivateProfileStruct  WritePrivateProfileStructW
#else
#define WritePrivateProfileStruct  WritePrivateProfileStructA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
DWORD WINAPI RegisterServiceProcess(DWORD dwProcessId, DWORD dwServiceType);
#define RSP_UNREGISTER_SERVICE	0x00000000
#define RSP_SIMPLE_SERVICE	0x00000001
#if(WINVER < 0x0400)
//
// Power Management APIs
//

#define AC_LINE_OFFLINE                 0x00
#define AC_LINE_ONLINE                  0x01
#define AC_LINE_BACKUP_POWER            0x02
#define AC_LINE_UNKNOWN                 0xFF

#define BATTERY_FLAG_HIGH               0x01
#define BATTERY_FLAG_LOW                0x02
#define BATTERY_FLAG_CRITICAL           0x04
#define BATTERY_FLAG_CHARGING           0x08
#define BATTERY_FLAG_NO_BATTERY         0x80
#define BATTERY_FLAG_UNKNOWN            0xFF

#define BATTERY_PERCENTAGE_UNKNOWN      0xFF

#define BATTERY_LIFE_UNKNOWN        0xFFFFFFFF

typedef struct _SYSTEM_POWER_STATUS {
    BYTE ACLineStatus;
    BYTE BatteryFlag;
    BYTE BatteryLifePercent;
    BYTE Reserved1;
    DWORD BatteryLifeTime;
    DWORD BatteryFullLifeTime;
}   SYSTEM_POWER_STATUS, *LPSYSTEM_POWER_STATUS;

BOOL
WINAPI
GetSystemPowerStatus(
    LPSYSTEM_POWER_STATUS lpSystemPowerStatus
    );

BOOL
WINAPI
SetSystemPowerState(
    BOOL fSuspend,
    BOOL fForce
    );
#endif /* WINVER < 0x0400 */

#if DEVL

#define BASEP_DUMP_REMOTE_CALL      0x80000000
#define BASEP_DUMP_SYSTEM_PROCESS   0x40000000
#define BASEP_DUMP_LOCKS            0x08000000
#define BASEP_DUMP_HEAP_SUMMARY     0x04000000
#define BASEP_DUMP_HEAP_HOGS        0x02000000
#define BASEP_DUMP_HEAP_ENTRIES     0x01000000
#define BASEP_DUMP_MODULE_TABLE     0x00800000
#define BASEP_DUMP_BACKTRACES       0x00400000
#define BASEP_DUMP_OBJECTS          0x00200000

#define BASEP_DUMP_FLAG_MASK        0xFFF00000
#define BASEP_DUMP_HANDLE_MASK      (~BASEP_DUMP_FLAG_MASK)

DWORD
BasepDebugDump(
    DWORD dwFlags
    );

#endif  // DEVL

BOOL
WINAPI
CloseProfileUserMapping( VOID );

BOOL
WINAPI
OpenProfileUserMapping( VOID );


BOOL
QueryWin31IniFilesMappedToRegistry(
    IN DWORD Flags,
    OUT PWSTR Buffer,
    IN DWORD cchBuffer,
    OUT LPDWORD cchUsed
    );

#define WIN31_INIFILES_MAPPED_TO_SYSTEM 0x00000001
#define WIN31_INIFILES_MAPPED_TO_USER   0x00000002

typedef BOOL (WINAPI *PWIN31IO_STATUS_CALLBACK)(
    IN PWSTR Status,
    IN PVOID CallbackParameter
    );

typedef enum _WIN31IO_EVENT {
    Win31SystemStartEvent,
    Win31LogonEvent,
    Win31LogoffEvent
} WIN31IO_EVENT;

#define WIN31_MIGRATE_INIFILES  0x00000001
#define WIN31_MIGRATE_GROUPS    0x00000002
#define WIN31_MIGRATE_REGDAT    0x00000004
#define WIN31_MIGRATE_ALL      (WIN31_MIGRATE_INIFILES | WIN31_MIGRATE_GROUPS | WIN31_MIGRATE_REGDAT)

DWORD
WINAPI
QueryWindows31FilesMigration(
    IN WIN31IO_EVENT EventType
    );

BOOL
WINAPI
SynchronizeWindows31FilesAndWindowsNTRegistry(
    IN WIN31IO_EVENT EventType,
    IN DWORD Flags,
    IN PWIN31IO_STATUS_CALLBACK StatusCallBack,
    IN PVOID CallbackParameter
    );

typedef struct _VIRTUAL_BUFFER {
    PVOID Base;
    PVOID CommitLimit;
    PVOID ReserveLimit;
} VIRTUAL_BUFFER, *PVIRTUAL_BUFFER;

BOOLEAN
CreateVirtualBuffer(
    OUT PVIRTUAL_BUFFER Buffer,
    IN ULONG CommitSize OPTIONAL,
    IN ULONG ReserveSize OPTIONAL
    );

int
VirtualBufferExceptionHandler(
    IN ULONG ExceptionCode,
    IN PEXCEPTION_POINTERS ExceptionInfo,
    IN OUT PVIRTUAL_BUFFER Buffer
    );

BOOLEAN
ExtendVirtualBuffer(
    IN PVIRTUAL_BUFFER Buffer,
    IN PVOID Address
    );

BOOLEAN
TrimVirtualBuffer(
    IN PVIRTUAL_BUFFER Buffer
    );

BOOLEAN
FreeVirtualBuffer(
    IN PVIRTUAL_BUFFER Buffer
    );


//
// filefind stucture shared with ntvdm, jonle
// see mvdm\dos\dem\demsrch.c
//
typedef struct _FINDFILE_HANDLE {
    HANDLE DirectoryHandle;
    PVOID FindBufferBase;
    PVOID FindBufferNext;
    ULONG FindBufferLength;
    ULONG FindBufferValidLength;
    RTL_CRITICAL_SECTION FindBufferLock;
} FINDFILE_HANDLE, *PFINDFILE_HANDLE;

#define BASE_FIND_FIRST_DEVICE_HANDLE (HANDLE)1

WINBASEAPI
BOOL
WINAPI
GetDaylightFlag(VOID);

WINBASEAPI
BOOL
WINAPI
SetDaylightFlag(
    BOOL fDaylight
    );

WINBASEAPI
BOOL
WINAPI
FreeLibrary16(
    HINSTANCE hLibModule
    );

WINBASEAPI
FARPROC
WINAPI
GetProcAddress16(
    HINSTANCE hModule,
    LPCSTR lpProcName
    );

WINBASEAPI
HINSTANCE
LoadLibrary16(
    LPCSTR lpLibFileName
    );

WINBASEAPI
BOOL
APIENTRY
NukeProcess(
    DWORD ppdb,
    UINT uExitCode,
    DWORD ulFlags);

WINBASEAPI
HGLOBAL
WINAPI
GlobalAlloc16(
    UINT uFlags,
    DWORD dwBytes
    );

WINBASEAPI
LPVOID
WINAPI
GlobalLock16(
    HGLOBAL hMem
    );

WINBASEAPI
BOOL
WINAPI
GlobalUnlock16(
    HGLOBAL hMem
    );

WINBASEAPI
HGLOBAL
WINAPI
GlobalFree16(
    HGLOBAL hMem
    );

WINBASEAPI
DWORD
WINAPI
GlobalSize16(
    HGLOBAL hMem
    );


#ifdef __cplusplus
}
#endif


#endif  // ndef _WINBASEP_
