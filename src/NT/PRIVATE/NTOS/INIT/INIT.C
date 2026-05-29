/*++

Copyright (c) 1989  Microsoft Corporation

Module Name:

    init.c

Abstract:

    Main source file the NTOS system initialization subcomponent.

Author:

    Steve Wood (stevewo) 31-Mar-1989

Revision History:

--*/


#include "ntos.h"
#include <zwapi.h>
#include <ntdddisk.h>
#include <fsrtl.h>
#include <ntverp.h>

#include "stdlib.h"
#include "stdio.h"
#include <string.h>

VOID
ExpInitializeExecutive(
    IN ULONG Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    );

NTSTATUS
CreateSystemRootLink(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    );

static USHORT
NameToOrdinal (
    IN PSZ NameOfEntryPoint,
    IN ULONG DllBase,
    IN ULONG NumberOfNames,
    IN PULONG NameTableBase,
    IN PUSHORT NameOrdinalTableBase
    );

NTSTATUS
LookupEntryPoint (
    IN PVOID DllBase,
    IN PSZ NameOfEntryPoint,
    OUT PVOID *AddressOfEntryPoint
    );

#if i386
VOID
KiRestoreInterrupts (
    IN BOOLEAN  Restore
    );
#endif

VOID
ExBurnMemory(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,ExpInitializeExecutive)
#pragma alloc_text(INIT,Phase1Initialization)
#pragma alloc_text(INIT,CreateSystemRootLink)
#pragma alloc_text(INIT,NameToOrdinal)
#pragma alloc_text(INIT,LookupEntryPoint)
#pragma alloc_text(INIT,ExBurnMemory)
#endif

//
// Define global static data used during initialization.
//


#if DEVL
ULONG NtGlobalFlag;
extern PMESSAGE_RESOURCE_BLOCK KiBugCheckMessages;

#if DBG
ULONG NtBuildNumber = VER_PRODUCTBUILD | 0xC0000000;
#else
ULONG NtBuildNumber = VER_PRODUCTBUILD | 0xF0000000;
#endif

#endif

STRING NtSystemPathString;
PUCHAR NtSystemPath;
UCHAR NtSystemPathBuffer[ DOS_MAX_PATH_LENGTH ];

ULONG InitializationPhase;  // bss 0

extern LIST_ENTRY PsLoadedModuleList;
extern KiServiceLimit;
#if DEVL
extern PMESSAGE_RESOURCE_DATA  KiBugCodeMessages;
#endif

/* From NTOS/IO/IOINIT.C — prints BaseDllName + FileVersion + CheckSum of a
 * loaded LDR entry. Used for the boot-module inventory after HAL init. */
VOID IopDumpModuleVersion(IN PLDR_DATA_TABLE_ENTRY LdrEntry);

extern CM_SYSTEM_CONTROL_VECTOR CmControlVector[];
ULONG CmNtGlobalFlag;
ULONG CmNtCSDVersion;
UNICODE_STRING CmVersionString;
UNICODE_STRING CmCSDVersionString;

//
// Define working set watch enabled.
//
// The value of this variable is controlled by the register variable ...
//

#if DBG
BOOLEAN PsWatchEnabled = TRUE;
#else
BOOLEAN PsWatchEnabled = FALSE;
#endif // DBG

#if DEVL
#if i386

typedef struct _EXLOCK {
    KSPIN_LOCK SpinLock;
    KIRQL Irql;
} EXLOCK, *PEXLOCK;

BOOLEAN
ExpOkayToLockRoutine(
    IN PEXLOCK Lock
    )
{
    return TRUE;
}

NTSTATUS
ExpInitializeLockRoutine(
    PEXLOCK Lock
    )
{
    KeInitializeSpinLock(&Lock->SpinLock);
    return STATUS_SUCCESS;
}

NTSTATUS
ExpAcquireLockRoutine(
    PEXLOCK Lock
    )
{
    ExAcquireSpinLock(&Lock->SpinLock,&Lock->Irql);
    return STATUS_SUCCESS;
}

NTSTATUS
ExpReleaseLockRoutine(
    PEXLOCK Lock
    )
{
    ExReleaseSpinLock(&Lock->SpinLock,Lock->Irql);
    return STATUS_SUCCESS;
}

NTSTATUS
ExpDeleteLockRoutine(
    PEXLOCK Lock
    )
{
    return STATUS_SUCCESS;
}


#endif
#endif



NLSTABLEINFO InitTableInfo;
ULONG InitNlsTableSize;
PVOID InitNlsTableBase;
ULONG InitAnsiCodePageDataOffset;
ULONG InitOemCodePageDataOffset;
ULONG InitUnicodeCaseTableDataOffset;
PVOID InitNlsSectionPointer;

VOID
ExBurnMemory(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    PLIST_ENTRY ListHead;
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
    PLIST_ENTRY NextEntry;
    PCHAR TypeOfMemory;
    PCHAR Options;
    PCHAR BurnMemoryOption;
    PCHAR NumProcOption;
    ULONG BurnMemoryAmount;
    ULONG PagesToBurn;
    ULONG PagesBurned;
    ULONG NewRegisteredProcessors;
#if !defined(NT_UP)
    extern ULONG KeRegisteredProcessors;
#endif

    if (LoaderBlock->LoadOptions == NULL) {
        return;
        }

    Options = LoaderBlock->LoadOptions;
    strupr(Options);

#if !defined(NT_UP)
    NumProcOption = strstr(Options, "NUMPROC");
    if (NumProcOption != NULL) {
        NumProcOption = strstr(NumProcOption,"=");
    }
    if (NumProcOption != NULL) {
        NewRegisteredProcessors = atol(NumProcOption+1);
        if (NewRegisteredProcessors < KeRegisteredProcessors) {
            KeRegisteredProcessors = NewRegisteredProcessors;
            DbgPrint("INIT: NumProcessors = %d\n",KeRegisteredProcessors);
        }
    }
#endif

    BurnMemoryOption = strstr(Options, "BURNMEMORY");
    if (BurnMemoryOption == NULL ) {
        return;
        }

    BurnMemoryOption = strstr(BurnMemoryOption,"=");
    if (BurnMemoryOption == NULL ) {
        return;
        }
    BurnMemoryAmount = atol(BurnMemoryOption+1);
    PagesToBurn = (BurnMemoryAmount*(1024*1024))/PAGE_SIZE;

    DbgPrint("INIT: BurnAmount %dmb -> %d pages\n",BurnMemoryAmount,PagesToBurn);

    ListHead = &LoaderBlock->MemoryDescriptorListHead;
    NextEntry = ListHead->Flink;
    PagesBurned = 0;
    do {
        MemoryDescriptor = CONTAINING_RECORD(NextEntry,
                                             MEMORY_ALLOCATION_DESCRIPTOR,
                                             ListEntry);

        if (MemoryDescriptor->MemoryType == LoaderFree ||
            MemoryDescriptor->MemoryType == LoaderFirmwareTemporary ) {

            if ( PagesBurned < PagesToBurn ) {

                //
                // We still need to chew up some memory
                //

                if ( MemoryDescriptor->PageCount > (PagesToBurn - PagesBurned) ) {

                    //
                    // This block has more than enough pages to satisfy us...
                    // simply change its page count
                    //

                    DbgPrint("INIT: BasePage %5lx PageCount %5d ReducedBy %5d to %5d\n",
                        MemoryDescriptor->BasePage,
                        MemoryDescriptor->PageCount,
                        (PagesToBurn - PagesBurned),
                        MemoryDescriptor->PageCount - (PagesToBurn - PagesBurned)
                        );

                    MemoryDescriptor->PageCount = MemoryDescriptor->PageCount - (PagesToBurn - PagesBurned);
                    PagesBurned = PagesToBurn;
                    }
                else {

                    //
                    // This block is not big enough. Take all of its pages and convert
                    // it to LoaderBad
                    //

                    DbgPrint("INIT: BasePage %5lx PageCount %5d Turned to LoaderBad\n",
                        MemoryDescriptor->BasePage,
                        MemoryDescriptor->PageCount
                        );

                    PagesBurned += MemoryDescriptor->PageCount;
                    MemoryDescriptor->MemoryType = LoaderBad;
                    }
                }
            else {
                return;
                }
            }

        NextEntry = NextEntry->Flink;

        } while (NextEntry != ListHead);

}

VOID
ExpInitializeExecutive(
    IN ULONG Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

/*++

Routine Description:

    This routine is called from the kernel initialization routine during
    bootstrap to initialize the executive and all of its subcomponents.
    Each subcomponent is potentially called twice to perform phase 0, and
    then phase 1 initialization. During phase 0 initialization, the only
    activity that may be performed is the initialization of subcomponent
    specific data. Phase 0 initilaization is performed in the context of
    the kernel start up routine with initerrupts disabled. During phase 1
    initialization, the system is fully operational and subcomponents may
    do any initialization that is necessary.

Arguments:

    LoaderBlock - Supplies a pointer to a loader parameter block.

Return Value:

    None.

--*/

{
    NTSTATUS Status;
    PLDR_DATA_TABLE_ENTRY DataTableEntry;
    PMESSAGE_RESOURCE_ENTRY MessageEntry;
    PLIST_ENTRY NextEntry;
    ANSI_STRING AnsiString;
    STRING NameString;
    CHAR Buffer[ 256 ];
    PCHAR s;
    ULONG ImageCount, i;
    BOOLEAN IncludeType[LoaderMaximum];
    ULONG MemoryAlloc[(sizeof(PHYSICAL_MEMORY_DESCRIPTOR) +
            sizeof(PHYSICAL_MEMORY_RUN)*MAX_PHYSICAL_MEMORY_FRAGMENTS) /
              sizeof(ULONG)];
    PPHYSICAL_MEMORY_DESCRIPTOR Memory;

#if DEVL
    ULONG   ResourceIdPath[3];
    PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry;
    PMESSAGE_RESOURCE_DATA  MessageData;
#endif

    if (Number == 0) {
        InitializationPhase = 0L;

        //
        // Compute PhysicalMemoryBlock
        //

        Memory = (PPHYSICAL_MEMORY_DESCRIPTOR)&MemoryAlloc;
        Memory->NumberOfRuns = MAX_PHYSICAL_MEMORY_FRAGMENTS;

        // include all memory types ...
        for (i=0; i < LoaderMaximum; i++) {
            IncludeType[i] = TRUE;
        }

        // ... expect these..
        IncludeType[LoaderBad] = FALSE;
        IncludeType[LoaderFirmwarePermanent] = FALSE;
        IncludeType[LoaderSpecialMemory] = FALSE;

        MmInitializeMemoryLimits(LoaderBlock, IncludeType, Memory);

        //
        // Initialize the translation tables using the loader
        // loaded tables
        //

        InitNlsTableBase = LoaderBlock->NlsData->AnsiCodePageData;
        InitAnsiCodePageDataOffset = 0;
        InitOemCodePageDataOffset = ((PUCHAR)LoaderBlock->NlsData->OemCodePageData - (PUCHAR)LoaderBlock->NlsData->AnsiCodePageData);
        InitUnicodeCaseTableDataOffset = ((PUCHAR)LoaderBlock->NlsData->UnicodeCaseTableData - (PUCHAR)LoaderBlock->NlsData->AnsiCodePageData);

        RtlInitNlsTables(
            (PVOID)((PUCHAR)InitNlsTableBase+InitAnsiCodePageDataOffset),
            (PVOID)((PUCHAR)InitNlsTableBase+InitOemCodePageDataOffset),
            (PVOID)((PUCHAR)InitNlsTableBase+InitUnicodeCaseTableDataOffset),
            &InitTableInfo
            );

        RtlResetRtlTranslations(&InitTableInfo);

        //
        // Initialize the RNG subsystem (the in-kernel CSPRNG) before the HAL,
        // so the HAL's boot-time entropy gathering has a live pool to feed.
        // RngInitSystem runs the Xoodoo/Cyclist power-on self-test and
        // bugchecks on failure rather than returning.
        //

        RngInitSystem(InitializationPhase, LoaderBlock);

        //
        // Initialize the Hardware Architecture Layer (HAL).
        //

        if (HalInitSystem(InitializationPhase, LoaderBlock) == FALSE) {
            KeBugCheck(HAL_INITIALIZATION_FAILED);
        }

        /*
         * MicroNT: normally KdInitSystem is only called from BugCheck when
         * /DEBUG isn't in boot options, leaving KiDebugRoutine == NULL and
         * losing all DbgPrint output. Call it early so BREAKPOINT_PRINT
         * exceptions reach KdpStub (which we patched to tee to HalDisplayString).
         */
        KdInitSystem(LoaderBlock, FALSE);

#if i386
        //
        // Interrupts can now be enabled
        //

        KiRestoreInterrupts (TRUE);
#endif

#if DEVL
        //
        // Set the default global flags value to show exceptions. Note that
        // this only has meaning on an x86 system. Exceptions are never shown
        // on a MIPS system.
        //

        NtGlobalFlag |= FLG_ENABLE_KDEBUG_SYMBOL_LOAD |
                            FLG_SHOW_EXCEPTIONS |
                            // FLG_SHOW_LDR_PROCESS_STARTS |
                            // FLG_SHOW_LDR_SNAPS |
                            // FLG_SHOW_OB_ALLOC_AND_FREE |
                            // FLG_STOP_ON_EXCEPTION |
                            FLG_IGNORE_DEBUG_PRIV;

#endif
        NtSystemPath = NtSystemPathBuffer;
        sprintf( NtSystemPath, "C:%s", LoaderBlock->NtBootPathName );
        RtlInitString( &NtSystemPathString, NtSystemPath );
        NtSystemPath[ --NtSystemPathString.Length ] = '\0';

        //
        // Scan the loaded module list and load the image symbols via the
        // kernel debugger for the system, the HAL, the boot file system, and
        // the boot drivers.
        //

        ImageCount = 0;
        NextEntry = LoaderBlock->LoadOrderListHead.Flink;
        while (NextEntry != &LoaderBlock->LoadOrderListHead) {

            //
            // Get the address of the data table entry for the next component.
            //

            DataTableEntry = CONTAINING_RECORD(NextEntry,
                                               LDR_DATA_TABLE_ENTRY,
                                               InLoadOrderLinks);

            //
            // Load the symbols via the kernel debugger for the next component.
            //

            sprintf( Buffer, "%s\\System32\\%s%wZ",
                     NtSystemPath + 2,
                     ImageCount++ < 2 ? "" : "Drivers\\",
                     &DataTableEntry->BaseDllName
                   );
            RtlInitString( &NameString, Buffer );
            DbgLoadImageSymbols(&NameString, DataTableEntry->DllBase, (ULONG)-1);

            /* Print FileVersion + PE checksum for each pre-staged module
             * (ntoskrnl, hal, and the UEFI loader's boot drivers). Module
             * inventory for the boot log — matches the format used later
             * when registry-driven drivers load via IopInitializeSystemDrivers. */
            IopDumpModuleVersion(DataTableEntry);

            NextEntry = NextEntry->Flink;
        }

#if DEVL
        //
        // Find the address of BugCheck message block resource and put it
        // in KiBugCodeMessages.
        //
        // WARNING: This code assumes that the LDR_DATA_TABLE_ENTRY for
        // ntoskrnl.exe is always the first in the loaded module list.
        //
        DataTableEntry = CONTAINING_RECORD(
                            LoaderBlock->LoadOrderListHead.Flink,
                            LDR_DATA_TABLE_ENTRY,
                            InLoadOrderLinks);

        ResourceIdPath[0] = 11;
        ResourceIdPath[1] = 1;
        ResourceIdPath[2] = 0;

        Status = LdrFindResource_U(
            DataTableEntry->DllBase,
            ResourceIdPath,
            3,
            (VOID *) &ResourceDataEntry);

        if (NT_SUCCESS(Status)) {
            Status = LdrAccessResource(
                DataTableEntry->DllBase,
                ResourceDataEntry,
                &MessageData,
                NULL);

            if (NT_SUCCESS(Status)) {
                KiBugCodeMessages = MessageData;
            }
        }
#endif

    } else {

        //
        // Initialize the Hardware Architecture Layer (HAL).
        //

        if (HalInitSystem(InitializationPhase, LoaderBlock) == FALSE) {
            KeBugCheck(HAL_INITIALIZATION_FAILED);
        }
    }

    if (Number == 0) {

        //
        // get system control values out of the registry
        //

        CmGetSystemControlValues(LoaderBlock->RegistryBase, &CmControlVector[0]);
        NtGlobalFlag |= CmNtGlobalFlag;
#if !DBG
        if (!(CmNtGlobalFlag & FLG_ENABLE_KDEBUG_SYMBOL_LOAD)) {
            NtGlobalFlag &= ~FLG_ENABLE_KDEBUG_SYMBOL_LOAD;
            }
#endif

        //
        // Initialize the ExResource package.
        //

        if (!ExInitSystem()) {
            KeBugCheck(PHASE0_INITIALIZATION_FAILED);
        }

        //
        // Initialize memory managment and the memory allocation pools.
        //

        ExBurnMemory(LoaderBlock);

        MmInitSystem(0, LoaderBlock, Memory);

        //
        // Snapshot the NLS tables into paged pool and then
        // reset the translation tables
        //

        {
            PLIST_ENTRY NextMd;
            PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;

            //
            //
            // Walk through the memory descriptors and size the nls data
            //

            NextMd = LoaderBlock->MemoryDescriptorListHead.Flink;

            while (NextMd != &LoaderBlock->MemoryDescriptorListHead) {

                MemoryDescriptor = CONTAINING_RECORD(NextMd,
                                                     MEMORY_ALLOCATION_DESCRIPTOR,
                                                     ListEntry);


                switch (MemoryDescriptor->MemoryType) {
                    case LoaderNlsData:
                        InitNlsTableSize += MemoryDescriptor->PageCount*PAGE_SIZE;
                        break;

                    default:
                        break;
                }

                NextMd = MemoryDescriptor->ListEntry.Flink;
            }

            InitNlsTableBase = ExAllocatePoolWithTag(NonPagedPool,InitNlsTableSize,' slN');
            if ( !InitNlsTableBase ) {
                KeBugCheck(PHASE0_INITIALIZATION_FAILED);
                }

            //
            // Copy the NLS data into the dynamic buffer so that we can
            // free the buffers allocated by the loader. The loader garuntees
            // contiguous buffers and the base of all the tables is the ANSI
            // code page data
            //


            RtlMoveMemory(
                InitNlsTableBase,
                LoaderBlock->NlsData->AnsiCodePageData,
                InitNlsTableSize
                );

            RtlInitNlsTables(
                (PVOID)((PUCHAR)InitNlsTableBase+InitAnsiCodePageDataOffset),
                (PVOID)((PUCHAR)InitNlsTableBase+InitOemCodePageDataOffset),
                (PVOID)((PUCHAR)InitNlsTableBase+InitUnicodeCaseTableDataOffset),
                &InitTableInfo
                );

            RtlResetRtlTranslations(&InitTableInfo);

        }

        //
        // Now that the HAL is available and memory management has sized
        // memory, Display Version number
        //

        DataTableEntry = CONTAINING_RECORD(LoaderBlock->LoadOrderListHead.Flink,
                                            LDR_DATA_TABLE_ENTRY,
                                            InLoadOrderLinks);

        Status = RtlFindMessage (DataTableEntry->DllBase, 11, 0,
                            WINDOWS_NT_BANNER, &MessageEntry);

        if (CmNtCSDVersion != 0) {
            Status = RtlFindMessage (DataTableEntry->DllBase, 11, 0,
                                WINDOWS_NT_CSD_STRING, &MessageEntry);
            if (NT_SUCCESS( Status )) {
                RtlInitAnsiString( &AnsiString, MessageEntry->Text );
                AnsiString.Length -= 2;
                CmCSDVersionString.MaximumLength =
                    sprintf( Buffer,
                             "%Z %u%c",
                             &AnsiString,
                             (CmNtCSDVersion & 0xFF00) >> 8,
                             (CmNtCSDVersion & 0xFF) ? 'A' + (CmNtCSDVersion & 0xFF) : '\0'
                           );
                }
            else {
                CmCSDVersionString.MaximumLength = sprintf( Buffer, "CSD %04x", CmNtCSDVersion & 0xFFFF );
                }

            CmCSDVersionString.MaximumLength = (USHORT)((CmCSDVersionString.MaximumLength + 1) * sizeof( WCHAR ));
            CmCSDVersionString.Buffer = (RtlAllocateStringRoutine)( CmCSDVersionString.MaximumLength );
            RtlInitAnsiString( &AnsiString, Buffer );
            RtlAnsiStringToUnicodeString( &CmCSDVersionString, &AnsiString, FALSE );
            }
        else {
            RtlCreateUnicodeStringFromAsciiz( &CmCSDVersionString, VER_PRODUCTBETA_STR );
            }

        Status = RtlFindMessage (DataTableEntry->DllBase, 11, 0,
                            WINDOWS_NT_BANNER, &MessageEntry);

        s = Buffer;
        if (CmCSDVersionString.Length != 0) {
            s += sprintf( s, ": %wZ", &CmCSDVersionString );
            }
        *s++ = '\0';

        sprintf( s,
                 NT_SUCCESS(Status) ? MessageEntry->Text : "MICROSOFT (R) WINDOWS NT (TM)\n",
                 VER_PRODUCTVERSION_STR,
                 NtBuildNumber & 0xFFFF,
                 Buffer
               );

        RtlCreateUnicodeStringFromAsciiz( &CmVersionString, VER_PRODUCTVERSION_STR );
        HalDisplayString(s);

#if DEVL
#if i386
        if (NtGlobalFlag & FLG_HEAP_TRACE_ALLOCS) {
            PVOID StackTraceDataBase;
            ULONG StackTraceDataBaseLength;
            NTSTATUS Status;

            StackTraceDataBaseLength =  512 * 1024;
            switch ( MmQuerySystemSize() ) {
                case MmMediumSystem :
                    StackTraceDataBaseLength = 1024 * 1024;
                    break;

                case MmLargeSystem :
                    StackTraceDataBaseLength = 2048 * 1024;
                    break;
                }

            StackTraceDataBase = ExAllocatePoolWithTag( NonPagedPool,
                                         StackTraceDataBaseLength,'catS'
                                       );
            if (StackTraceDataBase != NULL) {
                KdPrint(( "INIT: Kernel mode stack back trace enabled with %u KB buffer.\n", StackTraceDataBaseLength / 1024 ));
                Status = RtlInitStackTraceDataBaseEx( StackTraceDataBase,
                                                    StackTraceDataBaseLength,
                                                    StackTraceDataBaseLength,
                                                    (PRTL_INITIALIZE_LOCK_ROUTINE) ExpInitializeLockRoutine,
                                                    (PRTL_ACQUIRE_LOCK_ROUTINE) ExpAcquireLockRoutine,
                                                    (PRTL_RELEASE_LOCK_ROUTINE) ExpReleaseLockRoutine,
                                                    (PRTL_OKAY_TO_LOCK_ROUTINE) ExpOkayToLockRoutine
                                                  );
            } else {
                Status = STATUS_NO_MEMORY;
            }

            if (!NT_SUCCESS( Status )) {
                KdPrint(( "INIT: Unable to initialize stack trace data base - Status == %lx\n", Status ));
            }
        }
#endif // i386

        ExInitializeHandleTablePackage();
#endif // DEVL

        Status = RtlInitializeHeapManager();
        if (!NT_SUCCESS( Status )) {
            KeBugCheckEx(HEAP_INITIALIZATION_FAILED,(ULONG)Status,0,0,0);
        }

        //
        // Allocate and zero the system service count table.
        //

#if DBG

        KeServiceCountTable =
                    (PULONG)ExAllocatePoolWithTag(NonPagedPool,
                                           KiServiceLimit * sizeof(ULONG),
                                           'llac');

        RtlZeroMemory((PVOID)KeServiceCountTable,
                      KiServiceLimit * sizeof(ULONG));

#endif

        if (!ObInitSystem()) {
            KeBugCheck(OBJECT_INITIALIZATION_FAILED);
        }

        if (!SeInitSystem()) {
            KeBugCheck(SECURITY_INITIALIZATION_FAILED);
        }

        if (PsInitSystem(0, LoaderBlock) == FALSE) {
            KeBugCheck(PROCESS_INITIALIZATION_FAILED);
        }

        //
        // Compute the tick count multiplier that is used for computing the
        // windows millisecond tick count and copy the resultant value to
        // the memory that is shared between user and kernel mode.
        //

        ExpTickCountMultiplier = ExComputeTickCountMultiplier(KeMaximumIncrement);
        SharedUserData->TickCountMultiplier = ExpTickCountMultiplier;
    }
}



//
// MicroNT: the initial user-mode process is configured via a registry
// group under \Registry\Machine\System\CurrentControlSet\Control\Init:
//
//     Exe    (REG_SZ)  full NT path to initial image. Fallback:
//                      \SystemRoot\System32\smss.exe
//     Args   (REG_SZ)  argv tail appended to CommandLine after a space
//     Stdio  (REG_SZ)  NT device path opened inheritable, placed into
//                      ProcessParameters.Standard{Input,Output,Error}.
//                      Raw-mode serial timeouts applied automatically —
//                      a REPL can fread(1) without further configuration.
//
// Caller must ZwClose the returned StdioHandle after RtlCreateUserProcess
// (with InheritHandles=TRUE) has duplicated it into the child.
//

/* IOCTL_SERIAL_SET_TIMEOUTS + SERIAL_TIMEOUTS from NTDDSER.H, redeclared
 * locally so we don't pull the whole serial header into INIT.C. */
#define INIT_IOCTL_SERIAL_SET_TIMEOUTS   0x001B001C
typedef struct _INIT_SERIAL_TIMEOUTS {
    ULONG ReadIntervalTimeout;
    ULONG ReadTotalTimeoutMultiplier;
    ULONG ReadTotalTimeoutConstant;
    ULONG WriteTotalTimeoutMultiplier;
    ULONG WriteTotalTimeoutConstant;
} INIT_SERIAL_TIMEOUTS;

static VOID
QueryInitConfig(
    IN OUT PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    OUT    PHANDLE StdioHandle,
    OUT    PUNICODE_STRING StdioPath    /* caller inits Buffer + MaximumLength */
    )
{
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING    keyPath;
    UNICODE_STRING    valueName;
    HANDLE            key = NULL;
    NTSTATUS          status;
    ULONG             length = 0;
    UCHAR             buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
                             DOS_MAX_PATH_LENGTH * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION info =
        (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    BOOLEAN exeFromRegistry = FALSE;

    *StdioHandle = NULL;
    StdioPath->Length = 0;

    RtlInitUnicodeString(&keyPath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Init");
    InitializeObjectAttributes(&objectAttributes, &keyPath,
                               OBJ_CASE_INSENSITIVE, NULL, NULL);

    if (NT_SUCCESS(ZwOpenKey(&key, KEY_READ, &objectAttributes))) {
        /* Exe */
        RtlInitUnicodeString(&valueName, L"Exe");
        status = ZwQueryValueKey(key, &valueName,
                                 KeyValuePartialInformation,
                                 buffer, sizeof(buffer), &length);
        if (NT_SUCCESS(status) &&
            info->Type == REG_SZ &&
            info->DataLength >= sizeof(WCHAR)) {
            RtlAppendUnicodeToString(&ProcessParameters->ImagePathName,
                                     (PCWSTR)info->Data);
            exeFromRegistry = TRUE;
        }
    }

    if (!exeFromRegistry) {
        RtlAppendUnicodeToString(&ProcessParameters->ImagePathName,
                                 L"\\SystemRoot\\System32\\smss.exe");
    }

    RtlCopyUnicodeString(&ProcessParameters->CommandLine,
                         &ProcessParameters->ImagePathName);

    if (!key) {
        return;
    }

    /* Args */
    RtlInitUnicodeString(&valueName, L"Args");
    status = ZwQueryValueKey(key, &valueName,
                             KeyValuePartialInformation,
                             buffer, sizeof(buffer), &length);
    if (NT_SUCCESS(status) &&
        info->Type == REG_SZ &&
        info->DataLength > sizeof(WCHAR)) {
        RtlAppendUnicodeToString(&ProcessParameters->CommandLine, L" ");
        RtlAppendUnicodeToString(&ProcessParameters->CommandLine,
                                 (PCWSTR)info->Data);
    }

    /* Stdio — open device inheritable; place handle in all three stdio
     * slots of ProcessParameters. Child inherits on RtlCreateUserProcess
     * with InheritHandles=TRUE; the numeric handle value is preserved. */
    RtlInitUnicodeString(&valueName, L"Stdio");
    status = ZwQueryValueKey(key, &valueName,
                             KeyValuePartialInformation,
                             buffer, sizeof(buffer), &length);
    if (NT_SUCCESS(status) &&
        info->Type == REG_SZ &&
        info->DataLength > sizeof(WCHAR)) {
        UNICODE_STRING    devicePath;
        OBJECT_ATTRIBUTES stdioObja;
        IO_STATUS_BLOCK   iosb;
        HANDLE            h;

        RtlInitUnicodeString(&devicePath, (PCWSTR)info->Data);
        /* Copy path into caller's buffer for later display by DumpInitConfig. */
        if (devicePath.Length <= StdioPath->MaximumLength - sizeof(WCHAR)) {
            RtlCopyMemory(StdioPath->Buffer, devicePath.Buffer, devicePath.Length);
            StdioPath->Buffer[devicePath.Length / sizeof(WCHAR)] = 0;
            StdioPath->Length = devicePath.Length;
        }
        InitializeObjectAttributes(&stdioObja, &devicePath,
                                   OBJ_CASE_INSENSITIVE | OBJ_INHERIT,
                                   NULL, NULL);
        status = ZwCreateFile(&h,
                              /* DesiredAccess: GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE */
                              0xC0100000,
                              &stdioObja, &iosb,
                              NULL,                  /* AllocationSize */
                              0x80,                  /* FileAttributes = NORMAL */
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN,
                              FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL, 0);              /* EaBuffer */
        if (NT_SUCCESS(status)) {
            INIT_SERIAL_TIMEOUTS t;
            /* Raw mode: read returns as soon as any byte arrives. The
             * per-call-first-byte block is what an interactive REPL
             * wants — fread(1) blocks for a keystroke, fread(N) returns
             * on the first byte with whatever's buffered. */
            t.ReadIntervalTimeout         = (ULONG)-1;   /* MAXULONG */
            t.ReadTotalTimeoutMultiplier  = (ULONG)-1;
            t.ReadTotalTimeoutConstant    = 0;
            t.WriteTotalTimeoutMultiplier = 0;
            t.WriteTotalTimeoutConstant   = 0;
            /* Best-effort: ignore failure (device may not be a serial
             * port — e.g. stdio pointed at \Device\Null). */
            ZwDeviceIoControlFile(h, NULL, NULL, NULL, &iosb,
                                  INIT_IOCTL_SERIAL_SET_TIMEOUTS,
                                  &t, sizeof(t),
                                  NULL, 0);

            ProcessParameters->StandardInput  = h;
            ProcessParameters->StandardOutput = h;
            ProcessParameters->StandardError  = h;
            *StdioHandle = h;
        }
    }

    ZwClose(key);
}


//
// DumpInitConfig — dump the init-process configuration to the boot log,
// matching the format IopDumpModuleVersion uses for drivers:
//
//   INIT: exe   \SystemRoot\System32\lua.exe  3.50.0.1  (0x0002a7b4)
//   INIT: args  \SystemRoot\pkg\main.lua
//   INIT: stdio \Device\Serial0  (handle 0x00000040)
//
// Version + PE checksum come from mapping the Exe as an image section
// (SEC_IMAGE) into system space, reading OptionalHeader.CheckSum, and
// walking the RT_VERSION resource via LdrFindResource_U / LdrAccessResource.
//
static VOID
DumpInitConfig(
    IN PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    IN HANDLE StdioHandle,
    IN PUNICODE_STRING StdioPath
    )
{
    /* See IopDumpModuleVersion for struct provenance — fields we read
     * from VS_VERSIONINFO's embedded VS_FIXEDFILEINFO. */
    typedef struct {
        ULONG dwSignature;
        ULONG dwStrucVersion;
        ULONG dwFileVersionMS;
        ULONG dwFileVersionLS;
    } KERN_VS_FIXEDFILEINFO_HEAD;

    NTSTATUS                   Status;
    HANDLE                     FileHandle = NULL;
    HANDLE                     SectionHandle = NULL;
    OBJECT_ATTRIBUTES          oa;
    IO_STATUS_BLOCK            iosb;
    PVOID                      Base = NULL;
    ULONG                      ViewSize = 0;
    PIMAGE_NT_HEADERS          NtHeader;
    ULONG                      ResourceIdPath[3];
    PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry;
    PVOID                      ResourceBase;
    ULONG                      ResourceSize;
    KERN_VS_FIXEDFILEINFO_HEAD *ffi;
    ULONG                      CheckSum = 0;
    ULONG                      FileVerMS = 0, FileVerLS = 0;
    UNICODE_STRING             Args;

    /* Map the Exe image to read PE header fields and resources. */
    InitializeObjectAttributes(&oa, &ProcessParameters->ImagePathName,
                               OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenFile(&FileHandle,
                        FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE,
                        &oa, &iosb,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (NT_SUCCESS(Status)) {
        Status = ZwCreateSection(&SectionHandle,
                                 SECTION_MAP_READ | SECTION_MAP_EXECUTE,
                                 NULL, NULL,
                                 PAGE_EXECUTE_READ,
                                 SEC_IMAGE,
                                 FileHandle);
        ZwClose(FileHandle);
        if (NT_SUCCESS(Status)) {
            Status = ZwMapViewOfSection(SectionHandle, NtCurrentProcess(),
                                        &Base, 0, 0, NULL, &ViewSize,
                                        ViewShare, 0, PAGE_READONLY);
            if (NT_SUCCESS(Status)) {
                NtHeader = RtlImageNtHeader(Base);
                if (NtHeader) CheckSum = NtHeader->OptionalHeader.CheckSum;

                ResourceIdPath[0] = 16;    /* RT_VERSION */
                ResourceIdPath[1] = 1;
                ResourceIdPath[2] = 0;
                Status = LdrFindResource_U(Base, ResourceIdPath, 3,
                                           &ResourceDataEntry);
                if (NT_SUCCESS(Status)) {
                    Status = LdrAccessResource(Base, ResourceDataEntry,
                                               &ResourceBase, &ResourceSize);
                    if (NT_SUCCESS(Status) && ResourceSize >= 64) {
                        ffi = (KERN_VS_FIXEDFILEINFO_HEAD *)
                              ((PUCHAR)ResourceBase + 40);
                        if (ffi->dwSignature == 0xFEEF04BD) {
                            FileVerMS = ffi->dwFileVersionMS;
                            FileVerLS = ffi->dwFileVersionLS;
                        }
                    }
                }
                ZwUnmapViewOfSection(NtCurrentProcess(), Base);
            }
            ZwClose(SectionHandle);
        }
    }

    DbgPrint("INIT: exe   %wZ  %u.%u.%u.%u  (0x%08lx)\n",
             &ProcessParameters->ImagePathName,
             (FileVerMS >> 16) & 0xFFFF, FileVerMS & 0xFFFF,
             (FileVerLS >> 16) & 0xFFFF, FileVerLS & 0xFFFF,
             CheckSum);

    /* CommandLine was built as "<exe>[ <args>]". Slice past the exe
     * portion for display; if there's no trailing space there were no args. */
    if (ProcessParameters->CommandLine.Length >
            ProcessParameters->ImagePathName.Length + sizeof(WCHAR)) {
        Args.Buffer = (PWSTR)((PUCHAR)ProcessParameters->CommandLine.Buffer +
                              ProcessParameters->ImagePathName.Length +
                              sizeof(WCHAR));
        Args.Length = ProcessParameters->CommandLine.Length -
                      ProcessParameters->ImagePathName.Length -
                      sizeof(WCHAR);
        Args.MaximumLength = Args.Length;
        DbgPrint("INIT: args  %wZ\n", &Args);
    } else {
        DbgPrint("INIT: args  (none)\n");
    }

    if (StdioHandle != NULL && StdioPath != NULL && StdioPath->Length > 0) {
        DbgPrint("INIT: stdio %wZ  (handle 0x%08lx)\n",
                 StdioPath, StdioHandle);
    } else {
        DbgPrint("INIT: stdio (none)\n");
    }
}


VOID
Phase1Initialization(
    IN PVOID Context
    )

{

    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PETHREAD Thread;
    KPRIORITY Priority;
    NTSTATUS Status;
    UNICODE_STRING SessionManager;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PVOID Address;
    ULONG Size;
    RTL_USER_PROCESS_INFORMATION ProcessInformation;
    LARGE_INTEGER UniversalTime;
    LARGE_INTEGER CmosTime;
    LARGE_INTEGER OldTime;
    TIME_FIELDS TimeFields;
    UNICODE_STRING UnicodeDebugString;
    ANSI_STRING AnsiDebugString;
    UNICODE_STRING EnvString, NullString, UnicodeSystemPathString;
    CHAR DebugBuffer[256];
    PWSTR Src, Dst;
    BOOLEAN ResetActiveTimeBias;
    HANDLE NlsSection;
    LARGE_INTEGER SectionSize;
    LARGE_INTEGER SectionOffset;
    PVOID SectionBase;
    PVOID ViewBase;
    ULONG CapturedViewSize;
    ULONG SavedViewSize;
    LONG BootTimeZoneBias;
    PLDR_DATA_TABLE_ENTRY DataTableEntry;
    PMESSAGE_RESOURCE_ENTRY MessageEntry;
#ifndef NT_UP
    PMESSAGE_RESOURCE_ENTRY MessageEntry1;
#endif
    PCHAR MPKernelString;
    HANDLE StdioHandle = NULL;
    WCHAR StdioPathBuffer[128];
    UNICODE_STRING StdioPath;

    //
    // Set the phase number and raise the priority of current thread to
    // a high priority so it will not be prempted during initialization.
    //

    ResetActiveTimeBias = FALSE;
    InitializationPhase = 1;
    Thread = PsGetCurrentThread();
    Priority = KeSetPriorityThread( &Thread->Tcb,MAXIMUM_PRIORITY - 1 );

    //
    // Put phase 1 initialization calls here
    //

    LoaderBlock = (PLOADER_PARAMETER_BLOCK)Context;
    if (HalInitSystem(InitializationPhase, LoaderBlock) == FALSE) {
        KeBugCheck(HAL1_INITIALIZATION_FAILED);
    }

    //
    // Initialize the system time and set the time the system was booted.
    //
    // N.B. This cannot be done until after the phase one initialization
    //      of the HAL Layer.
    //

    if (HalQueryRealTimeClock(&TimeFields) != FALSE) {
        RtlTimeFieldsToTime(&TimeFields, &CmosTime);
        UniversalTime = CmosTime;
        if ( !ExpRealTimeIsUniversal ) {

            //
            // If the system stores time in local time. This is converted to
            // universal time before going any further
            //
            // If we have previously set the time through NT, then
            // ExpLastTimeZoneBias should contain the timezone bias in effect
            // when the clock was set.  Otherwise, we will have to resort to
            // our next best guess which would be the programmed bias stored in
            // the registry
            //

            if ( ExpLastTimeZoneBias == -1 ) {
                ResetActiveTimeBias = TRUE;
                ExpLastTimeZoneBias = ExpAltTimeZoneBias;
                }

            ExpTimeZoneBias.QuadPart = Int32x32To64(
                                ExpLastTimeZoneBias*60,   // Bias in seconds
                                10000000
                                );
#ifdef _ALPHA_
            SharedUserData->TimeZoneBias = ExpTimeZoneBias.QuadPart;
#else
            SharedUserData->TimeZoneBias.High2Time = ExpTimeZoneBias.HighPart;
            SharedUserData->TimeZoneBias.LowPart = ExpTimeZoneBias.LowPart;
            SharedUserData->TimeZoneBias.High1Time = ExpTimeZoneBias.HighPart;
#endif
            UniversalTime = RtlLargeIntegerAdd(CmosTime,ExpTimeZoneBias);
        }
        KeSetSystemTime(&UniversalTime, &OldTime, NULL);

        KeBootTime = UniversalTime;

    }

    MPKernelString = "";
    DataTableEntry = CONTAINING_RECORD(LoaderBlock->LoadOrderListHead.Flink,
                                        LDR_DATA_TABLE_ENTRY,
                                        InLoadOrderLinks);

#ifndef NT_UP

    //
    // If this is an MP build of the kernel start any other processors now
    //

    KeStartAllProcessors();

    //
    // Set the affinity of the boot processes and initialization thread
    // for all processors
    //

    KeGetCurrentThread()->ApcState.Process->Affinity = KeActiveProcessors;
    KeSetAffinityThread (KeGetCurrentThread(), KeActiveProcessors);
    Status = RtlFindMessage (DataTableEntry->DllBase, 11, 0,
                        WINDOWS_NT_MP_STRING, &MessageEntry1);

    if (NT_SUCCESS( Status )) {
        MPKernelString = MessageEntry1->Text;
        }
    else {
        MPKernelString = "MultiProcessor Kernel\r\n";
        }
#endif

    //
    // Signifiy to the HAL that all processors have been started and any
    // post initialization should be performed.
    //

    if (!HalAllProcessorsStarted()) {
        KeBugCheck(HAL1_INITIALIZATION_FAILED);
    }

    RtlInitAnsiString( &AnsiDebugString, MPKernelString );
    if (AnsiDebugString.Length >= 2) {
        AnsiDebugString.Length -= 2;
        }

    //
    // Now that the processors have started, display number of processors
    // and size of memory.
    //

    Status = RtlFindMessage (DataTableEntry->DllBase, 11, 0,
                        WINDOWS_NT_INFO_STRING, &MessageEntry);

    sprintf( DebugBuffer,
             NT_SUCCESS(Status) ? MessageEntry->Text : "%u System Processor%s [%u Kb Memory] %Z\n",
             KeNumberProcessors,
             KeNumberProcessors > 1 ? "s" : "",
             MmNumberOfPhysicalPages << (PAGE_SHIFT - 10),
             &AnsiDebugString
           );
    HalDisplayString(DebugBuffer);

    //
    // Display the memory configuration of the host system.
    //

    if ((NtGlobalFlag & FLG_DISPLAY_MEMORY_CONFIG) != 0) {
        CHAR DisplayBuffer[256];
        PLIST_ENTRY ListHead;
        PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
        PLIST_ENTRY NextEntry;
        PCHAR TypeOfMemory;

        //
        // Output display headings and enumerate memory types.
        //

        HalDisplayString("\nStart  End  Page  Type Of Memory\n Pfn   Pfn  Count\n\n");
        ListHead = &LoaderBlock->MemoryDescriptorListHead;
        NextEntry = ListHead->Flink;
        do {
            MemoryDescriptor = CONTAINING_RECORD(NextEntry,
                                                 MEMORY_ALLOCATION_DESCRIPTOR,
                                                 ListEntry);

            //
            // Switch on the memory type.
            //

            switch(MemoryDescriptor->MemoryType) {
            case LoaderExceptionBlock:
                TypeOfMemory = "Exception block";
                break;

            case LoaderSystemBlock:
                TypeOfMemory = "System block";
                break;

            case LoaderFree:
                TypeOfMemory = "Free memory";
                break;

            case LoaderBad:
                TypeOfMemory = "Bad memory";
                break;

            case LoaderLoadedProgram:
                TypeOfMemory = "Os Loader Program";
                break;

            case LoaderFirmwareTemporary:
                TypeOfMemory = "Firmware temporary";
                break;

            case LoaderFirmwarePermanent:
                TypeOfMemory = "Firmware permanent";
                break;

            case LoaderOsloaderHeap:
                TypeOfMemory = "Os Loader heap";
                break;

            case LoaderOsloaderStack:
                TypeOfMemory = "Os Loader stack";
                break;

            case LoaderSystemCode:
                TypeOfMemory = "Operating system code";
                break;

            case LoaderHalCode:
                TypeOfMemory = "HAL code";
                break;

            case LoaderBootDriver:
                TypeOfMemory = "Boot disk/file system driver";
                break;

            case LoaderConsoleInDriver:
                TypeOfMemory = "Console input driver";
                break;

            case LoaderConsoleOutDriver:
                TypeOfMemory = "Console output driver";
                break;

            case LoaderStartupDpcStack:
                TypeOfMemory = "DPC stack";
                break;

            case LoaderStartupKernelStack:
                TypeOfMemory = "Idle process stack";
                break;

            case LoaderStartupPanicStack:
                TypeOfMemory = "Panic stack";
                break;

            case LoaderStartupPcrPage:
                TypeOfMemory = "PCR Page";
                break;

            case LoaderStartupPdrPage:
                TypeOfMemory = "PDR Pages";
                break;

            case LoaderRegistryData:
                TypeOfMemory = "Registry data";
                break;

            case LoaderMemoryData:
                TypeOfMemory = "Memory data";
                break;

            case LoaderNlsData:
                TypeOfMemory = "Nls data";
                break;

            case LoaderSpecialMemory:
                TypeOfMemory = "Special memory";
                break;

            default :
                TypeOfMemory = "Unknown memory";
                break;
            }

            //
            // Display memory descriptor information.
            //

            sprintf(&DisplayBuffer[0], "%5lx %5lx %5lx %s\n",
                    MemoryDescriptor->BasePage,
                    MemoryDescriptor->BasePage + MemoryDescriptor->PageCount - 1,
                    MemoryDescriptor->PageCount,
                    TypeOfMemory);

            HalDisplayString(&DisplayBuffer[0]);
            NextEntry = NextEntry->Flink;
        } while (NextEntry != ListHead);

        HalDisplayString("\n");
    }

    if (!ObInitSystem())
        KeBugCheck(OBJECT1_INITIALIZATION_FAILED);

    if (!ExInitSystem())
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,0,0,0,0);

    //
    // Se expect directory and executive objects to be available, but
    // must be before device drivers are initialized.
    //

    if (!SeInitSystem())
            KeBugCheck(SECURITY1_INITIALIZATION_FAILED);

    //
    // Create the symbolic link to \SystemRoot.
    //

    Status = CreateSystemRootLink(LoaderBlock);
    if ( !NT_SUCCESS(Status) ) {
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED,Status,0,0,0);
    }

    if (MmInitSystem(1, (PLOADER_PARAMETER_BLOCK)Context, NULL) == FALSE)
        KeBugCheck(MEMORY1_INITIALIZATION_FAILED);

    //
    // Snapshot the NLS tables into a page file backed section, and then
    // reset the translation tables
    //

    SectionSize.HighPart = 0;
    SectionSize.LowPart = InitNlsTableSize;

    Status = ZwCreateSection(
                &NlsSection,
                SECTION_ALL_ACCESS,
                NULL,
                &SectionSize,
                PAGE_READWRITE,
                SEC_COMMIT,
                NULL
                );

    if (!NT_SUCCESS(Status)) {
        KdPrint(("INIT: Nls Section Creation Failed %x\n",Status));
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,Status,1,0,0);
    }

    Status = ObReferenceObjectByHandle(
                NlsSection,
                SECTION_ALL_ACCESS,
                MmSectionObjectType,
                KernelMode,
                &InitNlsSectionPointer,
                NULL
                );

    ZwClose(NlsSection);

    if ( !NT_SUCCESS(Status) ) {
        KdPrint(("INIT: Nls Section Reference Failed %x\n",Status));
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,Status,2,0,0);
    }

    SectionBase = NULL;
    CapturedViewSize = SectionSize.LowPart;
    SavedViewSize = CapturedViewSize;
    SectionSize.LowPart = 0;

    Status = MmMapViewInSystemCache(
                InitNlsSectionPointer,
                &SectionBase,
                &SectionSize,
                &CapturedViewSize
                );

    if ( !NT_SUCCESS(Status) ) {
        KdPrint(("INIT: Map In System Cache Failed %x\n",Status));
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,Status,3,0,0);
    }

    //
    // Copy the NLS data into the dynamic buffer so that we can
    // free the buffers allocated by the loader. The loader garuntees
    // contiguous buffers and the base of all the tables is the ANSI
    // code page data
    //

    RtlMoveMemory(
        SectionBase,
        InitNlsTableBase,
        InitNlsTableSize
        );

    //
    // Unmap the view to remove all pages from memory.  This prevents
    // these tables from consuming memory in the system cache while
    // the system cache is under utilized during bootup.
    //

    MmUnmapViewInSystemCache (SectionBase, InitNlsSectionPointer);

    SectionBase = NULL;

    //
    // Map it back into the system cache, but now the pages will no
    // longer be valid.
    //

    Status = MmMapViewInSystemCache(
                InitNlsSectionPointer,
                &SectionBase,
                &SectionSize,
                &SavedViewSize
                );

    if ( !NT_SUCCESS(Status) ) {
        KdPrint(("INIT: Map In System Cache Failed %x\n",Status));
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,Status,4,0,0);
    }

    ExFreePool(InitNlsTableBase);

    InitNlsTableBase = SectionBase;

    RtlInitNlsTables(
        (PVOID)((PUCHAR)InitNlsTableBase+InitAnsiCodePageDataOffset),
        (PVOID)((PUCHAR)InitNlsTableBase+InitOemCodePageDataOffset),
        (PVOID)((PUCHAR)InitNlsTableBase+InitUnicodeCaseTableDataOffset),
        &InitTableInfo
        );

    RtlResetRtlTranslations(&InitTableInfo);

    ViewBase = NULL;
    SectionOffset.LowPart = 0;
    SectionOffset.HighPart = 0;
    CapturedViewSize = 0;

    //
    // Map the system dll into the user part of the address space
    //

    Status = MmMapViewOfSection(
                InitNlsSectionPointer,
                PsGetCurrentProcess(),
                &ViewBase,
                0L,
                0L,
                &SectionOffset,
                &CapturedViewSize,
                ViewShare,
                0L,
                PAGE_READWRITE
                );

    if ( !NT_SUCCESS(Status) ) {
        KdPrint(("INIT: Map In User Portion Failed %x\n",Status));
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,Status,5,0,0);
    }

    RtlMoveMemory(
        ViewBase,
        InitNlsTableBase,
        InitNlsTableSize
        );

    InitNlsTableBase = ViewBase;

    //
    // Initialize the cache manager.
    //

    if (!CcInitializeCacheManager())
        KeBugCheck(CACHE_INITIALIZATION_FAILED);

    //
    // Config management (particularly the registry) gets inited in
    // two parts.  Part 1 makes \REGISTRY\MACHINE\SYSTEM and
    // \REGISTRY\MACHINE\HARDWARE available.  These are needed to
    // complete IO init.
    //

    if (!CmInitSystem1(LoaderBlock))
      KeBugCheck(CONFIG_INITIALIZATION_FAILED);

    //
    // Compute timezone bias and next cutover date
    //

    BootTimeZoneBias = ExpLastTimeZoneBias;

    ExRefreshTimeZoneInformation(&CmosTime);

    if ( ResetActiveTimeBias ) {
        ExLocalTimeToSystemTime(&CmosTime,&UniversalTime);
        KeBootTime = UniversalTime;
        KeSetSystemTime(&UniversalTime, &OldTime, NULL);
        }
    else {

        //
        // check to see if a timezone switch occured prior to boot...
        //

        if ( BootTimeZoneBias != ExpLastTimeZoneBias ) {
            ZwSetSystemTime(NULL,NULL);
            }
        }

    ExInitializeTimeRefresh();

    if (!FsRtlInitSystem())
        KeBugCheck(FILE_INITIALIZATION_FAILED);

    HalReportResourceUsage();

    if (!IoInitSystem(LoaderBlock))
        KeBugCheck(IO1_INITIALIZATION_FAILED);


    if (!LpcInitSystem())
        KeBugCheck(LPC_INITIALIZATION_FAILED);

#if i386
#endif

    //
    // Okay to call PsInitSystem now that \SystemRoot is defined so it can
    // locate NTDLL.DLL and SMSS.EXE
    //

    if (PsInitSystem(1, (PLOADER_PARAMETER_BLOCK)Context) == FALSE)
        KeBugCheck(PROCESS1_INITIALIZATION_FAILED);

    //
    // The process subsystem is up, so system threads can now be created.
    // Start the HAL's periodic entropy reseed thread (keeps the RNG pool
    // fresh from RDRAND + scheduling jitter, off the boot critical path).
    //
    {
        extern VOID HalStartEntropyThread(VOID);
        HalStartEntropyThread();
    }

    //
    // Free loader block.
    //

#if DEVL
    //
    // Force KeBugCheck to look at PsLoadedModuleList now that it is
    // setup.
    //
    if (LoaderBlock == KeLoaderBlock) {
        KeLoaderBlock = NULL;
    }
#endif // DEVL
    MmFreeLoaderBlock (LoaderBlock);
#if DEVL
    LoaderBlock = NULL;
    Context = NULL;
#endif // DEVL

    //
    // MicroNT: LSA removed. No \SeRmCommandPort, no \SeLsaCommandPort, no
    // RM command server thread. Token + access-check engine remains entirely
    // kernel-internal.
    //

    //
    // Set up process parameters for the Session Manager Subsystem
    //

    Size = sizeof( *ProcessParameters ) +
           ((DOS_MAX_PATH_LENGTH * 4) * sizeof( WCHAR ));
    ProcessParameters = NULL;
    Status = ZwAllocateVirtualMemory( NtCurrentProcess(),
                                      (PVOID *)&ProcessParameters,
                                      0,
                                      &Size,
                                      MEM_COMMIT,
                                      PAGE_READWRITE
                                    );
    if (!NT_SUCCESS( Status )) {
#if DBG
        sprintf(DebugBuffer,
                "INIT: Unable to allocate Process Parameters. 0x%lx\n",
                Status);

        RtlInitAnsiString(&AnsiDebugString, DebugBuffer);
        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&UnicodeDebugString,
                                              &AnsiDebugString,
                                          TRUE)) == FALSE) {
            KeBugCheck(SESSION1_INITIALIZATION_FAILED);
        }
        ZwDisplayString(&UnicodeDebugString);
#endif // DBG
        KeBugCheckEx(SESSION1_INITIALIZATION_FAILED,Status,0,0,0);
    }

    ProcessParameters->Length = Size;
    ProcessParameters->MaximumLength = Size;
    //
    // Reserve the low 1 MB of address space in the session manager.
    // Setup gets started using a replacement for the session manager
    // and that process needs to be able to use the vga driver on x86,
    // which uses int10 and thus requires the low 1 meg to be reserved
    // in the process. The cost is so low that we just do this all the
    // time, even when setup isn't running.
    //
    ProcessParameters->Flags = RTL_USER_PROC_PARAMS_NORMALIZED | RTL_USER_PROC_RESERVE_1MB;

    Size = PAGE_SIZE;
    Status = ZwAllocateVirtualMemory( NtCurrentProcess(),
                                      (PVOID *)&ProcessParameters->Environment,
                                      0,
                                      &Size,
                                      MEM_COMMIT,
                                      PAGE_READWRITE
                                    );
    if (!NT_SUCCESS( Status )) {
#if DBG
        sprintf(DebugBuffer,
                "INIT: Unable to allocate Process Environment 0x%lx\n",
                Status);

        RtlInitAnsiString(&AnsiDebugString, DebugBuffer);
        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&UnicodeDebugString,
                                              &AnsiDebugString,
                                          TRUE)) == FALSE) {
            KeBugCheck(SESSION2_INITIALIZATION_FAILED);
        }
        ZwDisplayString(&UnicodeDebugString);
#endif // DBG
        KeBugCheckEx(SESSION2_INITIALIZATION_FAILED,Status,0,0,0);
    }

    Dst = (PWSTR)(ProcessParameters + 1);
    ProcessParameters->CurrentDirectory.DosPath.Buffer = Dst;
    ProcessParameters->CurrentDirectory.DosPath.MaximumLength = DOS_MAX_PATH_LENGTH * sizeof( WCHAR );
    //
    // Real NT has SMSS seed the initial process's CurrentDirectory; we don't
    // run SMSS, so seed it ourselves with \SystemRoot.  The DllPath built
    // below copies CurDir then appends "\System32", giving \SystemRoot\System32
    // which the loader can resolve via the kernel \SystemRoot symbolic link.
    //
    RtlAppendUnicodeToString( &ProcessParameters->CurrentDirectory.DosPath,
                              L"\\SystemRoot" );

    Dst = (PWSTR)((PCHAR)ProcessParameters->CurrentDirectory.DosPath.Buffer +
                  ProcessParameters->CurrentDirectory.DosPath.MaximumLength
                 );
    ProcessParameters->DllPath.Buffer = Dst;
    ProcessParameters->DllPath.MaximumLength = DOS_MAX_PATH_LENGTH * sizeof( WCHAR );
    RtlCopyUnicodeString( &ProcessParameters->DllPath,
                          &ProcessParameters->CurrentDirectory.DosPath
                        );
    RtlAppendUnicodeToString( &ProcessParameters->DllPath, L"\\System32" );

    Dst = (PWSTR)((PCHAR)ProcessParameters->DllPath.Buffer +
                  ProcessParameters->DllPath.MaximumLength
                 );
    ProcessParameters->ImagePathName.Buffer = Dst;
    ProcessParameters->ImagePathName.MaximumLength = DOS_MAX_PATH_LENGTH * sizeof( WCHAR );

    Dst = (PWSTR)((PCHAR)ProcessParameters->ImagePathName.Buffer +
                  ProcessParameters->ImagePathName.MaximumLength
                 );
    ProcessParameters->CommandLine.Buffer = Dst;
    ProcessParameters->CommandLine.MaximumLength = DOS_MAX_PATH_LENGTH * sizeof( WCHAR );

    StdioPath.Buffer = StdioPathBuffer;
    StdioPath.Length = 0;
    StdioPath.MaximumLength = sizeof(StdioPathBuffer);

    QueryInitConfig( ProcessParameters, &StdioHandle, &StdioPath );
    DumpInitConfig( ProcessParameters, StdioHandle, &StdioPath );

    if (NT_SUCCESS(RtlAnsiStringToUnicodeString( &UnicodeSystemPathString,
                        &NtSystemPathString, TRUE)) == FALSE) {
            KeBugCheck(SESSION3_INITIALIZATION_FAILED);
        }

    NullString.Buffer = L"";
    NullString.Length = sizeof(WCHAR);
    NullString.MaximumLength = sizeof(WCHAR);
    EnvString.Buffer = ProcessParameters->Environment;
    EnvString.Length = 0;
    EnvString.MaximumLength = (USHORT)Size;
    RtlAppendUnicodeToString( &EnvString, L"Path=" );
    RtlAppendUnicodeStringToString( &EnvString, &UnicodeSystemPathString );
    RtlAppendUnicodeToString( &EnvString, L"\\System32" );
    RtlAppendUnicodeStringToString( &EnvString, &NullString );
    RtlAppendUnicodeToString( &EnvString, L"SystemRoot=" );
    RtlAppendUnicodeStringToString( &EnvString, &UnicodeSystemPathString );
    RtlAppendUnicodeStringToString( &EnvString, &NullString );
    if (NtGlobalFlag & FLG_SHOW_LDR_PROCESS_STARTS) {
        KdPrint(( "ProcessParameters at %lx\n", ProcessParameters ));
        KdPrint(( "    CurDir:    %wZ\n", &ProcessParameters->CurrentDirectory.DosPath ));
        KdPrint(( "    DllPath:   %wZ\n", &ProcessParameters->DllPath ));
        KdPrint(( "    ImageFile: %wZ\n", &ProcessParameters->ImagePathName ));
        KdPrint(( "    Environ:   %lx\n", ProcessParameters->Environment ));
        Src = ProcessParameters->Environment;
        while (*Src) {
            KdPrint(( "        %ws\n", Src ));
            while (*Src++) ;
            }
        }

    SessionManager = ProcessParameters->ImagePathName;
    Status = RtlCreateUserProcess(
                &SessionManager,
                OBJ_CASE_INSENSITIVE,
                RtlDeNormalizeProcessParams( ProcessParameters ),
                NULL,
                NULL,
                NULL,
                TRUE,                 /* InheritHandles: duplicates our
                                       * OBJ_INHERIT-marked Stdio handle
                                       * into the child's handle table. */
                NULL,
                NULL,
                &ProcessInformation
                );

    /* The child now holds its own duplicate of the stdio handle. Drop
     * ours (noop if QueryInitConfig didn't open one). */
    if (StdioHandle != NULL) {
        ZwClose( StdioHandle );
    }
    if ( !NT_SUCCESS(Status) ) {
#if DBG
        sprintf(DebugBuffer,
                "INIT: Unable to create Session Manager. 0x%lx\n",
                Status);

        RtlInitAnsiString(&AnsiDebugString, DebugBuffer);
        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&UnicodeDebugString,
                                              &AnsiDebugString,
                                          TRUE)) == FALSE) {
            KeBugCheck(SESSION3_INITIALIZATION_FAILED);
        }
        ZwDisplayString(&UnicodeDebugString);
#endif // DBG
        KeBugCheckEx(SESSION3_INITIALIZATION_FAILED,Status,0,0,0);
    }

    Status = ZwResumeThread(ProcessInformation.Thread,NULL);

    if ( !NT_SUCCESS(Status) ) {
#if DBG
        sprintf(DebugBuffer,
                "INIT: Unable to resume Session Manager. 0x%lx\n",
                Status);

        RtlInitAnsiString(&AnsiDebugString, DebugBuffer);
        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&UnicodeDebugString,
                                              &AnsiDebugString,
                                          TRUE)) == FALSE) {
            KeBugCheck(SESSION4_INITIALIZATION_FAILED);
        }
        ZwDisplayString(&UnicodeDebugString);
#endif // DBG
        KeBugCheckEx(SESSION4_INITIALIZATION_FAILED,Status,0,0,0);
    }

    //
    // Wait five seconds for the session manager to get started or
    // terminate. If the wait times out, then the session manager
    // is assumed to be healthy and the zero page thread is called.
    //

    OldTime.QuadPart = Int32x32To64(5, -(10 * 1000 * 1000));
    Status = ZwWaitForSingleObject(
                ProcessInformation.Process,
                FALSE,
                &OldTime
                );

    if (Status == STATUS_SUCCESS) {

        /* Capture smss's exit status so the bugcheck parameter reveals
         * why it died (unresolved import, section-map fault, syscall
         * returning non-NTSUCCESS, etc). Without this the SESSION5 code
         * is opaque. */
        PROCESS_BASIC_INFORMATION SmssBasicInfo;
        Status = ZwQueryInformationProcess( ProcessInformation.Process,
                                            ProcessBasicInformation,
                                            &SmssBasicInfo,
                                            sizeof(SmssBasicInfo),
                                            NULL );
        DbgPrint("INIT: SMSS terminated; exit status=0x%08x\n",
                 SmssBasicInfo.ExitStatus);

#if DBG

        sprintf(DebugBuffer, "INIT: Session Manager terminated.\n");
        RtlInitAnsiString(&AnsiDebugString, DebugBuffer);
        RtlAnsiStringToUnicodeString(&UnicodeDebugString,
                                     &AnsiDebugString,
                                     TRUE);

        ZwDisplayString(&UnicodeDebugString);

#endif // DBG

        KeBugCheckEx(SESSION5_INITIALIZATION_FAILED,
                     (ULONG)SmssBasicInfo.ExitStatus, 0, 0, 0);

    } else {
        //
        // Dont need these handles anymore.
        //

        ZwClose( ProcessInformation.Thread );
        ZwClose( ProcessInformation.Process );

        //
        // Free up memory used to pass arguments to session manager.
        //

        Size = 0;
        Address = ProcessParameters->Environment;
        ZwFreeVirtualMemory( NtCurrentProcess(),
                             (PVOID *)&Address,
                             &Size,
                             MEM_RELEASE
                           );

        Size = 0;
        Address = ProcessParameters;
        ZwFreeVirtualMemory( NtCurrentProcess(),
                             (PVOID *)&Address,
                             &Size,
                             MEM_RELEASE
                           );

        InitializationPhase += 1;
        MmZeroPageThread();
    }
}

NTSTATUS
CreateSystemRootLink(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

{
    HANDLE handle;
    UNICODE_STRING nameString;
    OBJECT_ATTRIBUTES objectAttributes;
    STRING linkString;
    UNICODE_STRING linkUnicodeString;
    NTSTATUS status;
    UCHAR deviceNameBuffer[256];
    STRING deviceNameString;
    UNICODE_STRING deviceNameUnicodeString;
    HANDLE linkHandle;

#if DBG

    UCHAR debugBuffer[256];
    STRING debugString;
    UNICODE_STRING debugUnicodeString;

#endif

    //
    // Create the root directory object for the \ArcName directory.
    //

    RtlInitUnicodeString( &nameString, L"\\ArcName" );

    InitializeObjectAttributes( &objectAttributes,
                                &nameString,
                                OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                                NULL,
                                SePublicDefaultSd );

    status = NtCreateDirectoryObject( &handle,
                                      DIRECTORY_ALL_ACCESS,
                                      &objectAttributes );
    if (!NT_SUCCESS( status )) {
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED,status,1,0,0);
        return status;
    } else {
        (VOID) NtClose( handle );
    }

    //
    // Create the root directory object for the \Device directory.
    //

    RtlInitUnicodeString( &nameString, L"\\Device" );


    InitializeObjectAttributes( &objectAttributes,
                                &nameString,
                                OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                                NULL,
                                SePublicDefaultSd );

    status = NtCreateDirectoryObject( &handle,
                                      DIRECTORY_ALL_ACCESS,
                                      &objectAttributes );
    if (!NT_SUCCESS( status )) {
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED,status,2,0,0);
        return status;
    } else {
        (VOID) NtClose( handle );
    }

    //
    // Create the symbolic link to the root of the system directory.
    //

    RtlInitAnsiString( &linkString, INIT_SYSTEMROOT_LINKNAME );

    status = RtlAnsiStringToUnicodeString( &linkUnicodeString,
                                           &linkString,
                                           TRUE);

    if (!NT_SUCCESS( status )) {
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED,status,3,0,0);
        return status;
    }

    InitializeObjectAttributes( &objectAttributes,
                                &linkUnicodeString,
                                OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                                NULL,
                                SePublicDefaultSd );

    //
    // Use ARC device name and system path from loader.
    //

    sprintf( deviceNameBuffer,
             "\\ArcName\\%s%s",
             LoaderBlock->ArcBootDeviceName,
             LoaderBlock->NtBootPathName);

    deviceNameBuffer[strlen(deviceNameBuffer)-1] = '\0';

    RtlInitString( &deviceNameString, deviceNameBuffer );

    status = RtlAnsiStringToUnicodeString( &deviceNameUnicodeString,
                                           &deviceNameString,
                                           TRUE );

    if (!NT_SUCCESS(status)) {
        RtlFreeUnicodeString( &linkUnicodeString );
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED,status,4,0,0);
        return status;
    }

    status = NtCreateSymbolicLinkObject( &linkHandle,
                                         SYMBOLIC_LINK_ALL_ACCESS,
                                         &objectAttributes,
                                         &deviceNameUnicodeString );

    RtlFreeUnicodeString( &linkUnicodeString );
    RtlFreeUnicodeString( &deviceNameUnicodeString );

    if (!NT_SUCCESS(status)) {
        KeBugCheckEx(SYMBOLIC_INITIALIZATION_FAILED,status,5,0,0);
        return status;
    }

#if DBG

    sprintf( debugBuffer, "INIT: %s => %s\n",
             INIT_SYSTEMROOT_LINKNAME,
             deviceNameBuffer );

    RtlInitAnsiString( &debugString, debugBuffer );

    status = RtlAnsiStringToUnicodeString( &debugUnicodeString,
                                           &debugString,
                                           TRUE );

    if (NT_SUCCESS(status)) {
        ZwDisplayString( &debugUnicodeString );
        RtlFreeUnicodeString( &debugUnicodeString );
    }

#endif // DBG

    NtClose( linkHandle );

    return STATUS_SUCCESS;
}

#if 0

PVOID
LookupImageBaseByName (
    IN PLIST_ENTRY ListHead,
    IN PSZ         Name
    )
/*++

    Lookups BaseAddress of ImageName - returned value can be used
    to find entry points via LookupEntryPoint

--*/
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY         Next;
    PVOID               Base;
    ANSI_STRING         ansiString;
    UNICODE_STRING      unicodeString;
    NTSTATUS            status;

    Next = ListHead->Flink;
    if (!Next) {
        return NULL;
    }

    RtlInitAnsiString(&ansiString, Name);
    status = RtlAnsiStringToUnicodeString( &unicodeString, &ansiString, TRUE );
    if (!NT_SUCCESS (status)) {
        return NULL;
    }

    Base = NULL;
    while (Next != ListHead) {
        Entry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        Next = Next->Flink;

        if (RtlEqualUnicodeString (&unicodeString, &Entry->BaseDllName, TRUE)) {
            Base = Entry->DllBase;
            break;
        }
    }

    RtlFreeUnicodeString( &unicodeString );
    return Base;
}

#endif

NTSTATUS
LookupEntryPoint (
    IN PVOID DllBase,
    IN PSZ NameOfEntryPoint,
    OUT PVOID *AddressOfEntryPoint
    )
/*++

Routine Description:

    Returns the address of an entry point given the DllBase and PSZ
    name of the entry point in question

--*/

{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    ULONG ExportSize;
    USHORT Ordinal;
    PULONG Addr;
    CHAR NameBuffer[64];

    ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)
        RtlImageDirectoryEntryToData(
            DllBase,
            TRUE,
            IMAGE_DIRECTORY_ENTRY_EXPORT,
            &ExportSize);

#if DBG
    if (!ExportDirectory) {
        DbgPrint("LookupENtryPoint: Can't locate system Export Directory\n");
    }
#endif

    if ( strlen(NameOfEntryPoint) > sizeof(NameBuffer)-2 ) {
        return STATUS_INVALID_PARAMETER;
    }

    strcpy(NameBuffer,NameOfEntryPoint);

    Ordinal = NameToOrdinal(
                NameBuffer,
                (ULONG)DllBase,
                ExportDirectory->NumberOfNames,
                (PULONG)((ULONG)DllBase + (ULONG)ExportDirectory->AddressOfNames),
                (PUSHORT)((ULONG)DllBase + (ULONG)ExportDirectory->AddressOfNameOrdinals)
                );

    //
    // If Ordinal is not within the Export Address Table,
    // then DLL does not implement function.
    //

    if ( (ULONG)Ordinal >= ExportDirectory->NumberOfFunctions ) {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    Addr = (PULONG)((ULONG)DllBase + (ULONG)ExportDirectory->AddressOfFunctions);
    *AddressOfEntryPoint = (PVOID)((ULONG)DllBase + Addr[Ordinal]);
    return STATUS_SUCCESS;
}

static USHORT
NameToOrdinal (
    IN PSZ NameOfEntryPoint,
    IN ULONG DllBase,
    IN ULONG NumberOfNames,
    IN PULONG NameTableBase,
    IN PUSHORT NameOrdinalTableBase
    )
{

    ULONG SplitIndex;
    LONG CompareResult;

    SplitIndex = NumberOfNames >> 1;

    CompareResult = strcmp(NameOfEntryPoint, (PSZ)(DllBase + NameTableBase[SplitIndex]));

    if ( CompareResult == 0 ) {
        return NameOrdinalTableBase[SplitIndex];
    }

    if ( NumberOfNames == 1 ) {
        return (USHORT)-1;
    }

    if ( CompareResult < 0 ) {
        NumberOfNames = SplitIndex;
    } else {
        NameTableBase = &NameTableBase[SplitIndex+1];
        NameOrdinalTableBase = &NameOrdinalTableBase[SplitIndex+1];
        NumberOfNames = NumberOfNames - SplitIndex - 1;
    }

    return NameToOrdinal(NameOfEntryPoint,DllBase,NumberOfNames,NameTableBase,NameOrdinalTableBase);

}
