/*
 * MicroNT Boot Loader - NT structure definitions
 *
 * Minimal definitions needed to build LOADER_PARAMETER_BLOCK
 * and related structures that KiSystemStartup expects.
 */
#ifndef _LOADER_H_
#define _LOADER_H_

typedef unsigned char       UCHAR;
typedef unsigned short      USHORT;
typedef unsigned int        ULONG;
typedef unsigned long long  ULONGLONG;
typedef int                 LONG;
typedef short               SHORT;
typedef char                CHAR;
typedef CHAR               *PCHAR;
typedef void                VOID;
typedef void               *PVOID;
typedef int                 BOOLEAN;
typedef ULONG               NTSTATUS;
typedef UCHAR               KIRQL;
typedef SHORT               CSHORT;
typedef ULONG               KAFFINITY;

#define TRUE  1
#define FALSE 0
#define NULL  ((void*)0)

/* NT LIST_ENTRY */
typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

#define InitializeListHead(ListHead) \
    (ListHead)->Flink = (ListHead)->Blink = (ListHead)

/* UNICODE_STRING */
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    USHORT *Buffer;
} UNICODE_STRING;

/* Memory descriptor types (from arc.h) */
typedef enum _MEMORY_TYPE {
    LoaderExceptionBlock,       // 0
    LoaderSystemBlock,          // 1
    LoaderFree,                 // 2
    LoaderBad,                  // 3
    LoaderLoadedProgram,        // 4
    LoaderFirmwareTemporary,    // 5
    LoaderFirmwarePermanent,    // 6
    LoaderOsloaderHeap,         // 7
    LoaderOsloaderStack,        // 8
    LoaderSystemCode,           // 9
    LoaderHalCode,              // 10
    LoaderBootDriver,           // 11
    LoaderConsoleInDriver,      // 12
    LoaderConsoleOutDriver,     // 13
    LoaderStartupDpcStack,      // 14
    LoaderStartupKernelStack,   // 15
    LoaderStartupPanicStack,    // 16
    LoaderStartupPcrPage,       // 17
    LoaderStartupPdrPage,       // 18
    LoaderRegistryData,         // 19
    LoaderMemoryData,           // 20
    LoaderNlsData,              // 21
    LoaderSpecialMemory,        // 22
    LoaderMaximum               // 23
} MEMORY_TYPE;

/* Memory allocation descriptor */
typedef struct _MEMORY_ALLOCATION_DESCRIPTOR {
    LIST_ENTRY ListEntry;
    MEMORY_TYPE MemoryType;
    ULONG BasePage;
    ULONG PageCount;
} MEMORY_ALLOCATION_DESCRIPTOR, *PMEMORY_ALLOCATION_DESCRIPTOR;

/* NLS data block */
typedef struct _NLS_DATA_BLOCK {
    PVOID AnsiCodePageData;
    PVOID OemCodePageData;
    PVOID UnicodeCaseTableData;
} NLS_DATA_BLOCK, *PNLS_DATA_BLOCK;

/* ARC disk information (minimal) */
typedef struct _ARC_DISK_INFORMATION {
    LIST_ENTRY DiskSignatures;
} ARC_DISK_INFORMATION, *PARC_DISK_INFORMATION;

/* Configuration tree (mirrors NT/PRIVATE/NTOS/INC/ARC.H).
 * The kernel's CmpInitializeHardwareConfiguration walks this tree and creates
 * \Registry\Machine\Hardware\Description\... keys named after the component
 * Type using CmTypeName[]. A SystemClass root becomes "System". */

typedef enum _CONFIGURATION_CLASS {
    SystemClass = 0,
    ProcessorClass,
    CacheClass,
    AdapterClass,
    ControllerClass,
    PeripheralClass,
    MemoryClass,
    MaximumClass
} CONFIGURATION_CLASS;

/* Only the CONFIGURATION_TYPE values we actually use are named.
 * Real enum has 38 entries; the kernel forces Type=ArcSystem for SystemClass
 * components regardless, so we just set Type=0 (ArcSystem) for the root. */
typedef enum _CONFIGURATION_TYPE {
    ArcSystem = 0,
    CentralProcessor = 1,
} CONFIGURATION_TYPE;

typedef struct _CONFIGURATION_COMPONENT {
    CONFIGURATION_CLASS Class;
    CONFIGURATION_TYPE Type;
    ULONG Flags;                    /* DEVICE_FLAGS */
    USHORT Version;
    USHORT Revision;
    ULONG Key;
    ULONG AffinityMask;
    ULONG ConfigurationDataLength;
    ULONG IdentifierLength;
    PCHAR Identifier;
} CONFIGURATION_COMPONENT;

typedef struct _CONFIGURATION_COMPONENT_DATA {
    struct _CONFIGURATION_COMPONENT_DATA *Parent;
    struct _CONFIGURATION_COMPONENT_DATA *Child;
    struct _CONFIGURATION_COMPONENT_DATA *Sibling;
    CONFIGURATION_COMPONENT ComponentEntry;
    PVOID ConfigurationData;
} CONFIGURATION_COMPONENT_DATA, *PCONFIGURATION_COMPONENT_DATA;

/* LDR_DATA_TABLE_ENTRY - loaded module list entry */
typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    LIST_ENTRY HashLinks;
    ULONG TimeDateStamp;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

/* I386 loader block extension */
typedef struct _I386_LOADER_BLOCK {
    PVOID CommonDataArea;
    ULONG MachineType;
} I386_LOADER_BLOCK;

/* Setup loader block (we don't use this) */
struct _SETUP_LOADER_BLOCK;

/* The main loader parameter block */
typedef struct _LOADER_PARAMETER_BLOCK {
    LIST_ENTRY LoadOrderListHead;
    LIST_ENTRY MemoryDescriptorListHead;
    LIST_ENTRY BootDriverListHead;
    ULONG KernelStack;
    ULONG Prcb;
    ULONG Process;
    ULONG Thread;
    ULONG RegistryLength;
    PVOID RegistryBase;
    PCONFIGURATION_COMPONENT_DATA ConfigurationRoot;
    PCHAR ArcBootDeviceName;
    PCHAR ArcHalDeviceName;
    PCHAR NtBootPathName;
    PCHAR NtHalPathName;
    PCHAR LoadOptions;
    PNLS_DATA_BLOCK NlsData;
    PARC_DISK_INFORMATION ArcDiskInformation;
    PVOID OemFontFile;
    struct _SETUP_LOADER_BLOCK *SetupLoaderBlock;
    ULONG Spare1;
    I386_LOADER_BLOCK I386;
} LOADER_PARAMETER_BLOCK, *PLOADER_PARAMETER_BLOCK;

/* Multiboot info structure (from multiboot spec) */
typedef struct {
    ULONG flags;
    ULONG mem_lower;
    ULONG mem_upper;
    ULONG boot_device;
    ULONG cmdline;
    ULONG mods_count;
    ULONG mods_addr;
    ULONG syms[4];
    ULONG mmap_length;
    ULONG mmap_addr;
} multiboot_info_t;

typedef struct {
    ULONG mod_start;
    ULONG mod_end;
    ULONG string;
    ULONG reserved;
} multiboot_module_t;

typedef struct {
    ULONG size;
    ULONGLONG addr;
    ULONGLONG len;
    ULONG type;
} __attribute__((packed)) multiboot_mmap_entry_t;

/* PE image structures (minimal for loading) */
#define IMAGE_DOS_SIGNATURE     0x5A4D
#define IMAGE_NT_SIGNATURE      0x00004550

typedef struct _IMAGE_DATA_DIRECTORY {
    ULONG VirtualAddress;
    ULONG Size;
} IMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_DOS_HEADER {
    USHORT e_magic;
    USHORT e_cblp;
    USHORT e_cp;
    USHORT e_crlc;
    USHORT e_cparhdr;
    USHORT e_minalloc;
    USHORT e_maxalloc;
    USHORT e_ss;
    USHORT e_sp;
    USHORT e_csum;
    USHORT e_ip;
    USHORT e_cs;
    USHORT e_lfarlc;
    USHORT e_ovno;
    USHORT e_res[4];
    USHORT e_oemid;
    USHORT e_oeminfo;
    USHORT e_res2[10];
    LONG   e_lfanew;
} IMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER {
    USHORT Machine;
    USHORT NumberOfSections;
    ULONG  TimeDateStamp;
    ULONG  PointerToSymbolTable;
    ULONG  NumberOfSymbols;
    USHORT SizeOfOptionalHeader;
    USHORT Characteristics;
} IMAGE_FILE_HEADER;

typedef struct _IMAGE_OPTIONAL_HEADER {
    USHORT Magic;
    UCHAR  MajorLinkerVersion;
    UCHAR  MinorLinkerVersion;
    ULONG  SizeOfCode;
    ULONG  SizeOfInitializedData;
    ULONG  SizeOfUninitializedData;
    ULONG  AddressOfEntryPoint;
    ULONG  BaseOfCode;
    ULONG  BaseOfData;
    ULONG  ImageBase;
    ULONG  SectionAlignment;
    ULONG  FileAlignment;
    USHORT MajorOperatingSystemVersion;
    USHORT MinorOperatingSystemVersion;
    USHORT MajorImageVersion;
    USHORT MinorImageVersion;
    USHORT MajorSubsystemVersion;
    USHORT MinorSubsystemVersion;
    ULONG  Win32VersionValue;
    ULONG  SizeOfImage;
    ULONG  SizeOfHeaders;
    ULONG  CheckSum;
    USHORT Subsystem;
    USHORT DllCharacteristics;
    ULONG  SizeOfStackReserve;
    ULONG  SizeOfStackCommit;
    ULONG  SizeOfHeapReserve;
    ULONG  SizeOfHeapCommit;
    ULONG  LoaderFlags;
    ULONG  NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER;

typedef struct _IMAGE_NT_HEADERS {
    ULONG Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER OptionalHeader;
} IMAGE_NT_HEADERS;

typedef struct _IMAGE_SECTION_HEADER {
    CHAR  Name[8];
    ULONG VirtualSize;
    ULONG VirtualAddress;
    ULONG SizeOfRawData;
    ULONG PointerToRawData;
    ULONG PointerToRelocations;
    ULONG PointerToLinenumbers;
    USHORT NumberOfRelocations;
    USHORT NumberOfLinenumbers;
    ULONG Characteristics;
} IMAGE_SECTION_HEADER;

/* PE data directory indices */
#define IMAGE_DIRECTORY_ENTRY_EXPORT    0
#define IMAGE_DIRECTORY_ENTRY_IMPORT    1

/* Import descriptor */
typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    ULONG OriginalFirstThunk;   /* RVA to INT (Import Name Table) */
    ULONG TimeDateStamp;
    ULONG ForwarderChain;
    ULONG Name;                 /* RVA to DLL name string */
    ULONG FirstThunk;           /* RVA to IAT (Import Address Table) */
} IMAGE_IMPORT_DESCRIPTOR;

/* Import by name */
typedef struct _IMAGE_IMPORT_BY_NAME {
    USHORT Hint;
    CHAR   Name[1];
} IMAGE_IMPORT_BY_NAME;

#define IMAGE_ORDINAL_FLAG  0x80000000
#define IMAGE_SNAP_BY_ORDINAL(o) ((o) & IMAGE_ORDINAL_FLAG)

/* Export directory */
typedef struct _IMAGE_EXPORT_DIRECTORY {
    ULONG Characteristics;
    ULONG TimeDateStamp;
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG Name;
    ULONG Base;
    ULONG NumberOfFunctions;
    ULONG NumberOfNames;
    ULONG AddressOfFunctions;
    ULONG AddressOfNames;
    ULONG AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY;

/* GDT entry structure */
typedef struct _GDT_ENTRY {
    USHORT LimitLow;
    USHORT BaseLow;
    UCHAR  BaseMid;
    UCHAR  Access;
    UCHAR  LimitHigh;   /* includes flags in upper nibble */
    UCHAR  BaseHigh;
} __attribute__((packed)) GDT_ENTRY;

/* Page table constants */
#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PDE_SHIFT           22
#define PAGE_PRESENT        0x01
#define PAGE_READWRITE      0x03
#define PAGE_4MB            0x80

/* NT kernel virtual base */
#define KSEG0_BASE          0x80000000

#endif /* _LOADER_H_ */
