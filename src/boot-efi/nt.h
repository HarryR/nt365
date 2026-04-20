/*
 * NT kernel interface structures used by LOADER_PARAMETER_BLOCK.
 *
 * CRITICAL: these structs are wire-format between our 64-bit UEFI
 * loader and the 32-bit NT kernel. Native C pointer types would be
 * 8 bytes under -m64 and 4 bytes under -m32, shifting every subsequent
 * field by 4 bytes and making the kernel read garbage. All pointer-
 * equivalent fields are therefore declared as UINT32 — the loader
 * stores KSEG0 virtual addresses (0x80000000..0xFFFFFFFF) which
 * always fit in 32 bits. C code that assigns pointers into these
 * fields casts via `(UINT32)(UINTN)ptr`.
 *
 * UCHAR/USHORT/ULONG aliases cover NT type conventions without
 * clashing with gnu-efi's typedefs. PVOID/PCHAR are defined as
 * UINT32 (the "32-bit pointer" wire type) rather than real pointers
 * so existing `PVOID field` declarations keep working.
 */
#ifndef _BOOT_EFI_NT_H_
#define _BOOT_EFI_NT_H_

#include <efi.h>

#define UCHAR   UINT8
#define USHORT  UINT16
#define ULONG   UINT32
/* Wire-format pointers: 4 bytes, holds a KSEG0 VA. */
#define PCHAR   UINT32
#define PVOID   UINT32

typedef struct _NT_LIST_ENTRY {
    UINT32 Flink;   /* wire: KSEG0 VA of next entry */
    UINT32 Blink;   /* wire: KSEG0 VA of prev entry */
} NT_LIST_ENTRY;

typedef struct _UNICODE_STRING {
    UINT16  Length;
    UINT16  MaximumLength;
    UINT32  Buffer;   /* wire: KSEG0 VA of UTF-16 buffer */
} UNICODE_STRING;

typedef enum _NT_MEMORY_TYPE {
    LoaderExceptionBlock,       /* 0  */
    LoaderSystemBlock,          /* 1  */
    LoaderFree,                 /* 2  */
    LoaderBad,                  /* 3  */
    LoaderLoadedProgram,        /* 4  */
    LoaderFirmwareTemporary,    /* 5  */
    LoaderFirmwarePermanent,    /* 6  */
    LoaderOsloaderHeap,         /* 7  */
    LoaderOsloaderStack,        /* 8  */
    LoaderSystemCode,           /* 9  */
    LoaderHalCode,              /* 10 */
    LoaderBootDriver,           /* 11 */
    LoaderConsoleInDriver,      /* 12 */
    LoaderConsoleOutDriver,     /* 13 */
    LoaderStartupDpcStack,      /* 14 */
    LoaderStartupKernelStack,   /* 15 */
    LoaderStartupPanicStack,    /* 16 */
    LoaderStartupPcrPage,       /* 17 */
    LoaderStartupPdrPage,       /* 18 */
    LoaderRegistryData,         /* 19 */
    LoaderMemoryData,           /* 20 */
    LoaderNlsData,              /* 21 */
    LoaderSpecialMemory,        /* 22 */
    LoaderMaximum
} NT_MEMORY_TYPE;

typedef struct _MEMORY_ALLOCATION_DESCRIPTOR {
    NT_LIST_ENTRY   ListEntry;
    NT_MEMORY_TYPE  MemoryType;
    UINT32          BasePage;
    UINT32          PageCount;
} MEMORY_ALLOCATION_DESCRIPTOR;

typedef struct _NLS_DATA_BLOCK {
    UINT32 AnsiCodePageData;       /* wire: KSEG0 VA */
    UINT32 OemCodePageData;        /* wire: KSEG0 VA */
    UINT32 UnicodeCaseTableData;   /* wire: KSEG0 VA */
} NLS_DATA_BLOCK;

typedef struct _ARC_DISK_SIGNATURE {
    NT_LIST_ENTRY ListEntry;
    UINT32        Signature;
    UINT32        ArcName;    /* wire: KSEG0 VA of CHAR8* */
    UINT32        CheckSum;
    BOOLEAN       ValidPartitionTable;
} ARC_DISK_SIGNATURE;

typedef struct _ARC_DISK_INFORMATION {
    NT_LIST_ENTRY DiskSignatures;
} ARC_DISK_INFORMATION;

typedef enum _CONFIGURATION_CLASS {
    SystemClass = 0,
    ProcessorClass, CacheClass, AdapterClass, ControllerClass,
    PeripheralClass, MemoryClass, MaximumClass
} CONFIGURATION_CLASS;

typedef enum _CONFIGURATION_TYPE {
    ArcSystem = 0, CentralProcessor = 1,
} CONFIGURATION_TYPE;

typedef struct _CONFIGURATION_COMPONENT {
    CONFIGURATION_CLASS Class;
    CONFIGURATION_TYPE  Type;
    UINT32 Flags;
    UINT16 Version, Revision;
    UINT32 Key, AffinityMask;
    UINT32 ConfigurationDataLength, IdentifierLength;
    UINT32 Identifier;       /* wire: KSEG0 VA of CHAR8* */
} CONFIGURATION_COMPONENT;

typedef struct _CONFIGURATION_COMPONENT_DATA {
    UINT32 Parent;           /* wire: KSEG0 VA */
    UINT32 Child;            /* wire: KSEG0 VA */
    UINT32 Sibling;          /* wire: KSEG0 VA */
    CONFIGURATION_COMPONENT ComponentEntry;
    UINT32 ConfigurationData; /* wire: KSEG0 VA */
} CONFIGURATION_COMPONENT_DATA;

/* NT's kernel-visible resource descriptor wire format (NTIFS.H). Used
 * for "Configuration Data" values under \Registry\Machine\Hardware\...
 * Must match binary layout NT expects. pshpack4 for CM_PARTIAL_*. */

typedef enum {
    NT_InterfaceTypeInternal = 0, NT_InterfaceTypeIsa = 1,
} NT_INTERFACE_TYPE;

/* CmResourceType enum positions (NTIFS.H _CM_RESOURCE_TYPE). */
#define NT_CmResourceTypeDeviceSpecific  5

typedef struct __attribute__((packed, aligned(4))) _NT_CM_PARTIAL_RESOURCE_DESCRIPTOR {
    UINT8  Type;
    UINT8  ShareDisposition;
    UINT16 Flags;
    struct {
        UINT32 DataSize;
        UINT32 Reserved1;
        UINT32 Reserved2;
    } DeviceSpecificData;
    /* Extra padding: the union in NT's struct is 12 bytes (DataSize + two
     * Reserved ULONGs), but other arms (Memory/Port with LARGE_INTEGER
     * Start + ULONG Length) are also 12 bytes @ align 4. Total struct = 16. */
} NT_CM_PARTIAL_RESOURCE_DESCRIPTOR;

typedef struct __attribute__((aligned(4))) _NT_CM_PARTIAL_RESOURCE_LIST {
    UINT16 Version;
    UINT16 Revision;
    UINT32 Count;
    NT_CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[1];
} NT_CM_PARTIAL_RESOURCE_LIST;

typedef struct __attribute__((aligned(4))) _NT_CM_FULL_RESOURCE_DESCRIPTOR {
    NT_INTERFACE_TYPE         InterfaceType;
    UINT32                    BusNumber;
    NT_CM_PARTIAL_RESOURCE_LIST PartialResourceList;
} NT_CM_FULL_RESOURCE_DESCRIPTOR;

/* Byte-packed (see pshpack1.h before the struct in ntifs.h:5148). */
typedef struct __attribute__((packed)) _NT_CM_INT13_DRIVE_PARAMETER {
    UINT16 DriveSelect;
    UINT32 MaxCylinders;
    UINT16 SectorsPerTrack;
    UINT16 MaxHeads;
    UINT16 NumberDrives;
} NT_CM_INT13_DRIVE_PARAMETER;

typedef struct _LDR_DATA_TABLE_ENTRY {
    NT_LIST_ENTRY InLoadOrderLinks;
    NT_LIST_ENTRY InMemoryOrderLinks;
    NT_LIST_ENTRY InInitializationOrderLinks;
    UINT32 DllBase;          /* wire: KSEG0 VA */
    UINT32 EntryPoint;       /* wire: KSEG0 VA */
    UINT32 SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    UINT32 Flags;
    UINT16 LoadCount;
    UINT16 TlsIndex;
    NT_LIST_ENTRY HashLinks;
    UINT32 TimeDateStamp;
} LDR_DATA_TABLE_ENTRY;

typedef struct _BOOT_DRIVER_LIST_ENTRY {
    NT_LIST_ENTRY Link;
    UNICODE_STRING FilePath;
    UNICODE_STRING RegistryPath;
    UINT32 LdrEntry;         /* wire: KSEG0 VA of LDR_DATA_TABLE_ENTRY */
} BOOT_DRIVER_LIST_ENTRY;

typedef struct _I386_LOADER_BLOCK {
    UINT32 CommonDataArea;   /* wire: KSEG0 VA */
    UINT32 MachineType;
} I386_LOADER_BLOCK;

typedef struct _LOADER_PARAMETER_BLOCK {
    NT_LIST_ENTRY LoadOrderListHead;
    NT_LIST_ENTRY MemoryDescriptorListHead;
    NT_LIST_ENTRY BootDriverListHead;
    UINT32  KernelStack;
    UINT32  Prcb;
    UINT32  Process;
    UINT32  Thread;
    UINT32  RegistryLength;
    UINT32  RegistryBase;            /* wire: KSEG0 VA */
    UINT32  ConfigurationRoot;       /* wire: KSEG0 VA */
    UINT32  ArcBootDeviceName;       /* wire: KSEG0 VA of CHAR8* */
    UINT32  ArcHalDeviceName;        /* wire: KSEG0 VA of CHAR8* */
    UINT32  NtBootPathName;          /* wire: KSEG0 VA of CHAR8* */
    UINT32  NtHalPathName;           /* wire: KSEG0 VA of CHAR8* */
    UINT32  LoadOptions;             /* wire: KSEG0 VA of CHAR8* */
    UINT32  NlsData;                 /* wire: KSEG0 VA of NLS_DATA_BLOCK* */
    UINT32  ArcDiskInformation;      /* wire: KSEG0 VA of ARC_DISK_INFORMATION* */
    UINT32  OemFontFile;             /* wire: KSEG0 VA */
    UINT32  SetupLoaderBlock;        /* wire: KSEG0 VA */
    UINT32  Spare1;
    I386_LOADER_BLOCK I386;
} LOADER_PARAMETER_BLOCK;

#endif
