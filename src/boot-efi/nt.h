/*
 * NT kernel interface structures used by LOADER_PARAMETER_BLOCK.
 *
 * Uses EFI type names (UINT8/UINT16/UINT32/BOOLEAN) where they overlap
 * to avoid typedef clashes with gnu-efi headers. NT-specific type
 * aliases (UCHAR, USHORT, ULONG, PVOID, PCHAR) are all reused as macros
 * onto the EFI equivalents.
 */
#ifndef _BOOT_EFI_NT_H_
#define _BOOT_EFI_NT_H_

#include <efi.h>

/* Alias NT-style names onto their EFI equivalents. Safe because EFI's
 * typedefs are stable and NT's headers do the same under a different
 * toolchain root. */
#define UCHAR   UINT8
#define USHORT  UINT16
#define ULONG   UINT32
#define PCHAR   CHAR8 *
#define PVOID   VOID *

typedef struct _NT_LIST_ENTRY {
    struct _NT_LIST_ENTRY *Flink;
    struct _NT_LIST_ENTRY *Blink;
} NT_LIST_ENTRY;

typedef struct _UNICODE_STRING {
    UINT16  Length;
    UINT16  MaximumLength;
    UINT16 *Buffer;
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
    void *AnsiCodePageData;
    void *OemCodePageData;
    void *UnicodeCaseTableData;
} NLS_DATA_BLOCK;

typedef struct _ARC_DISK_SIGNATURE {
    NT_LIST_ENTRY ListEntry;
    UINT32        Signature;
    CHAR8        *ArcName;
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
    CHAR8 *Identifier;
} CONFIGURATION_COMPONENT;

typedef struct _CONFIGURATION_COMPONENT_DATA {
    struct _CONFIGURATION_COMPONENT_DATA *Parent;
    struct _CONFIGURATION_COMPONENT_DATA *Child;
    struct _CONFIGURATION_COMPONENT_DATA *Sibling;
    CONFIGURATION_COMPONENT ComponentEntry;
    void *ConfigurationData;
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
    void *DllBase;
    void *EntryPoint;
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
    LDR_DATA_TABLE_ENTRY *LdrEntry;
} BOOT_DRIVER_LIST_ENTRY;

typedef struct _I386_LOADER_BLOCK {
    void  *CommonDataArea;
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
    void   *RegistryBase;
    CONFIGURATION_COMPONENT_DATA *ConfigurationRoot;
    CHAR8  *ArcBootDeviceName;
    CHAR8  *ArcHalDeviceName;
    CHAR8  *NtBootPathName;
    CHAR8  *NtHalPathName;
    CHAR8  *LoadOptions;
    NLS_DATA_BLOCK       *NlsData;
    ARC_DISK_INFORMATION *ArcDiskInformation;
    void   *OemFontFile;
    void   *SetupLoaderBlock;
    UINT32  Spare1;
    I386_LOADER_BLOCK I386;
} LOADER_PARAMETER_BLOCK;

#endif
