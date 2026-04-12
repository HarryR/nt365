/*
 * MicroNT Boot Loader
 *
 * Loads ntoskrnl.exe and hal.dll PE images from multiboot modules,
 * sets up page tables, builds LOADER_PARAMETER_BLOCK, and returns
 * the address of KiSystemStartup.
 */

#include "loader.h"

/* Forward declarations for standard C functions (must match GCC builtins) */
typedef __SIZE_TYPE__ size_t;
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

/* Forward declarations */
static void serial_init(void);
static void serial_putc(char c);
static void serial_puts(const char *s);
static void serial_hex(ULONG v);
static void setup_page_tables(void);
static PVOID load_pe_image(ULONG file_base, ULONG file_size, const char *name);
static void setup_gdt_entry(int selector, ULONG base, ULONG limit, UCHAR access, UCHAR flags);
static void build_loader_block(multiboot_info_t *mbi);
static void halt(const char *msg);

/* Externs from entry.S */
extern GDT_ENTRY boot_gdt[];
extern UCHAR boot_idt[];
extern UCHAR boot_tss[];
#define BOOT_TSS_SIZE 108  /* 104 bytes TSS + 2 byte T flag + 2 byte IOMAP base */
extern ULONG loader_block;
extern UCHAR boot_stack_top[];

/* Static data areas */
static LOADER_PARAMETER_BLOCK LoaderBlock;
static NLS_DATA_BLOCK NlsData;
static ARC_DISK_INFORMATION ArcDiskInfo;
static LDR_DATA_TABLE_ENTRY KernelModule;
static LDR_DATA_TABLE_ENTRY HalModule;
static MEMORY_ALLOCATION_DESCRIPTOR MemDescriptors[32];
static int NumMemDescriptors;

/* Dummy NLS code page data (minimal - just enough to not crash) */
static UCHAR DummyAnsiNls[8192];
static UCHAR DummyOemNls[8192];
static UCHAR DummyUnicaseNls[8192];

/* Dummy registry hive (empty) */
static UCHAR DummyRegistry[4096];

/*
 * Page directory and page tables.
 *
 * We match the real OSLOADER layout:
 *   - 4KB page tables (no PSE/4MB pages)
 *   - Identity map (phys == virt) for low memory (uses temporary page tables)
 *   - KSEG0 map (0x80000000 + phys) for all physical memory (permanent page tables)
 *   - Self-mapping PDE at entry 768 (0xC0000000) — page dir maps itself
 *   - HAL page table at PDE 1023 (0xFFC00000) — for PCR, shared data, HAL mappings
 *
 * With 64MB RAM, we need 16 page tables for identity + 16 for KSEG0 + 1 HAL = 33
 */
#define MAX_PAGE_TABLES 40
static ULONG PageDirectory[1024] __attribute__((aligned(4096)));
static ULONG PageTables[MAX_PAGE_TABLES][1024] __attribute__((aligned(4096)));
static int NextPageTable = 0;

static ULONG *alloc_page_table(void) {
    if (NextPageTable >= MAX_PAGE_TABLES) {
        halt("Out of page tables");
    }
    ULONG *pt = PageTables[NextPageTable++];
    memset(pt, 0, 4096);
    return pt;
}

/* PCR - Processor Control Region (one page, KIP0PCRADDRESS = 0xFFDFF000) */
static UCHAR BootPcr[4096] __attribute__((aligned(4096)));

/* Shared user data page (KI_USER_SHARED_DATA = 0xFFDF0000) */
static UCHAR SharedUserDataPage[4096] __attribute__((aligned(4096)));

/* Idle thread/process/stack - allocated statically */
static UCHAR IdleThreadStorage[4096] __attribute__((aligned(16)));
static UCHAR IdleProcessStorage[4096] __attribute__((aligned(16)));
static UCHAR IdleStack[16384] __attribute__((aligned(16)));

/* Kernel/HAL load info */
static PVOID KernelBase;
static PVOID HalBase;
static ULONG KernelEntryPoint;
static ULONG KernelSizeOfImage;
static ULONG HalEntryPoint;
static ULONG HalSizeOfImage;

/* String constants */
static char ArcBootDevice[] = "multi(0)disk(0)rdisk(0)partition(1)";
static char ArcHalDevice[]  = "multi(0)disk(0)rdisk(0)partition(1)";
static char NtBootPath[]    = "\\";
static char NtHalPath[]     = "\\";
static char LoadOptions[]   = "DEBUG DEBUGPORT=COM1 BAUDRATE=115200";

/* Module names (wide char for UNICODE_STRING) */
static USHORT KernelNameW[] = { 'n','t','o','s','k','r','n','l','.','e','x','e',0 };
static USHORT HalNameW[]    = { 'h','a','l','.','d','l','l',0 };

/*======================================================================
 * Serial port output (COM1 0x3F8) for debug
 *====================================================================*/

#define COM1_PORT 0x3F8

static inline void outb(USHORT port, UCHAR val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline UCHAR inb(USHORT port) {
    UCHAR ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);  /* Disable interrupts */
    outb(COM1_PORT + 3, 0x80);  /* Enable DLAB */
    outb(COM1_PORT + 0, 0x03);  /* 38400 baud */
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);  /* 8N1 */
    outb(COM1_PORT + 2, 0xC7);  /* Enable FIFO */
    outb(COM1_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */
}

static void serial_putc(char c) {
    while (!(inb(COM1_PORT + 5) & 0x20));
    outb(COM1_PORT, c);
    if (c == '\n') serial_putc('\r');
}

static void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

static void serial_hex(ULONG v) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        serial_putc(hex[(v >> i) & 0xF]);
}

/*======================================================================
 * VGA text output
 *====================================================================*/

static USHORT *vga = (USHORT *)0xB8000;
static int vga_pos = 0;

static void vga_putc(char c) {
    if (c == '\n') {
        vga_pos = (vga_pos / 80 + 1) * 80;
    } else {
        vga[vga_pos++] = 0x0F00 | (UCHAR)c;
    }
    if (vga_pos >= 80 * 25) vga_pos = 0;
}

static void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}

static void print(const char *s) {
    serial_puts(s);
    vga_puts(s);
}

static void print_hex(ULONG v) {
    serial_hex(v);
    /* Also to VGA */
    const char hex[] = "0123456789ABCDEF";
    vga_putc('0'); vga_putc('x');
    for (int i = 28; i >= 0; i -= 4)
        vga_putc(hex[(v >> i) & 0xF]);
}

static void halt(const char *msg) {
    print("FATAL: ");
    print(msg);
    print("\n");
    for (;;) __asm__ volatile("cli; hlt");
}

/*======================================================================
 * Memory copy
 *====================================================================*/

void *memcpy(void *dst, const void *src, size_t n) {
    UCHAR *d = dst;
    const UCHAR *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    UCHAR *d = dst;
    while (n--) *d++ = (UCHAR)c;
    return dst;
}

/*======================================================================
 * GDT manipulation
 *====================================================================*/

static void setup_gdt_entry(int selector, ULONG base, ULONG limit, UCHAR access, UCHAR flags) {
    GDT_ENTRY *e = &boot_gdt[selector / 8];
    e->LimitLow = limit & 0xFFFF;
    e->BaseLow  = base & 0xFFFF;
    e->BaseMid  = (base >> 16) & 0xFF;
    e->Access   = access;
    e->LimitHigh = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    e->BaseHigh = (base >> 24) & 0xFF;
}

/*======================================================================
 * PE image loader
 *====================================================================*/

static PVOID load_pe_image(ULONG file_base, ULONG load_addr, const char *name) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)file_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        print("Bad DOS sig: ");
        print(name);
        halt("");
    }

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(file_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        print("Bad PE sig: ");
        print(name);
        halt("");
    }

    ULONG size_of_image = nt->OptionalHeader.SizeOfImage;
    ULONG num_sections = nt->FileHeader.NumberOfSections;

    print("  Loading ");
    print(name);
    print(" -> phys ");
    print_hex(load_addr);
    print(" size ");
    print_hex(size_of_image);
    print("\n");

    /* Clear the entire image region at the PHYSICAL address */
    memset((void *)load_addr, 0, size_of_image);

    /* Copy headers */
    ULONG header_size = nt->OptionalHeader.SizeOfHeaders;
    memcpy((void *)load_addr, (void *)file_base, header_size);

    /* Copy sections */
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)
        ((UCHAR *)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);

    for (ULONG i = 0; i < num_sections; i++) {
        if (sec[i].SizeOfRawData == 0) continue;
        ULONG dst = load_addr + sec[i].VirtualAddress;
        ULONG src = file_base + sec[i].PointerToRawData;
        ULONG len = sec[i].SizeOfRawData;
        memcpy((void *)dst, (void *)src, len);
    }

    return (PVOID)load_addr;
}

/*======================================================================
 * String comparison
 *====================================================================*/

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int stricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/*======================================================================
 * PE import/export resolution
 *
 * The kernel (ntoskrnl.exe) imports functions from hal.dll, and
 * hal.dll imports functions from ntoskrnl.exe. The loader must
 * resolve these cross-references by patching the Import Address
 * Tables (IATs) with the actual function addresses from the
 * export tables.
 *====================================================================*/

static ULONG pe_lookup_export(ULONG image_base, const char *func_name) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image_base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(image_base + dos->e_lfanew);

    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
        return 0;

    ULONG exp_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (exp_rva == 0)
        return 0;

    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(image_base + exp_rva);
    ULONG *func_table = (ULONG *)(image_base + exp->AddressOfFunctions);
    ULONG *name_table = (ULONG *)(image_base + exp->AddressOfNames);
    USHORT *ord_table = (USHORT *)(image_base + exp->AddressOfNameOrdinals);

    for (ULONG i = 0; i < exp->NumberOfNames; i++) {
        const char *name = (const char *)(image_base + name_table[i]);
        if (strcmp(name, func_name) == 0) {
            USHORT ordinal = ord_table[i];
            return image_base + func_table[ordinal];
        }
    }
    return 0;
}

static void resolve_imports(ULONG image_base, const char *image_name,
                           ULONG kernel_base, ULONG hal_base) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image_base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(image_base + dos->e_lfanew);

    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return;

    ULONG imp_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (imp_rva == 0)
        return;

    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(image_base + imp_rva);

    while (imp->Name != 0) {
        const char *dll_name = (const char *)(image_base + imp->Name);

        /* Determine which image provides this DLL */
        ULONG provider_base = 0;
        if (stricmp(dll_name, "hal.dll") == 0) {
            provider_base = hal_base;
        } else if (stricmp(dll_name, "ntoskrnl.exe") == 0) {
            provider_base = kernel_base;
        } else {
            print("    WARNING: Unknown import DLL: ");
            print(dll_name);
            print("\n");
            imp++;
            continue;
        }

        /* Walk the Import Name Table and patch the Import Address Table */
        ULONG *int_entry = (ULONG *)(image_base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        ULONG *iat_entry = (ULONG *)(image_base + imp->FirstThunk);

        ULONG resolved = 0, unresolved = 0;

        while (*int_entry != 0) {
            if (IMAGE_SNAP_BY_ORDINAL(*int_entry)) {
                /* Import by ordinal — not common for kernel/HAL */
                print("    WARNING: ordinal import not supported\n");
                unresolved++;
            } else {
                /* Import by name */
                IMAGE_IMPORT_BY_NAME *hint_name =
                    (IMAGE_IMPORT_BY_NAME *)(image_base + *int_entry);
                ULONG addr = pe_lookup_export(provider_base, hint_name->Name);
                if (addr != 0) {
                    *iat_entry = addr;
                    resolved++;
                } else {
                    print("    UNRESOLVED: ");
                    print(hint_name->Name);
                    print("\n");
                    unresolved++;
                }
            }
            int_entry++;
            iat_entry++;
        }

        print("  ");
        print(image_name);
        print(" -> ");
        print(dll_name);
        print(": ");
        print_hex(resolved);
        print(" resolved");
        if (unresolved) {
            print(", ");
            print_hex(unresolved);
            print(" UNRESOLVED");
        }
        print("\n");

        imp++;
    }
}

/*======================================================================
 * Page tables
 *
 * NT kernel expects:
 *   - Paging enabled
 *   - Identity mapping of low memory (where hardware structures live)
 *   - KSEG0 (0x80000000+) mapped to physical 0
 *
 * We use 4MB pages (PSE) for simplicity where possible.
 *====================================================================*/

static void setup_page_tables(void) {
    ULONG page, pde_idx;

    memset(PageDirectory, 0, sizeof(PageDirectory));
    NextPageTable = 0;

    /*
     * Map all physical memory with 4KB page tables, matching the real OSLOADER.
     * For each 4MB region we allocate TWO page tables:
     *   1. Identity map: PDE[i] -> physical == virtual (temporary, freed by MM)
     *   2. KSEG0 map:    PDE[512+i] -> virtual 0x80000000+phys (permanent)
     *
     * With 64MB RAM = 16 regions * 2 = 32 page tables.
     */
    ULONG total_pages = 64 * 1024 * 1024 / PAGE_SIZE;  /* 16384 pages for 64MB */

    for (page = 0; page < total_pages; page++) {
        pde_idx = page >> 10;  /* page / 1024 = PDE index */

        /* Allocate page tables on first page of each 4MB region */
        if ((page & 0x3FF) == 0) {
            /* Identity map page table */
            ULONG *phys_pt = alloc_page_table();
            PageDirectory[pde_idx] = ((ULONG)phys_pt) | PAGE_PRESENT | PAGE_READWRITE;

            /* KSEG0 page table */
            ULONG *kseg_pt = alloc_page_table();
            PageDirectory[pde_idx + (KSEG0_BASE >> 22)] = ((ULONG)kseg_pt) | PAGE_PRESENT | PAGE_READWRITE;
        }

        /* Get current page tables */
        ULONG *phys_pt = (ULONG *)(PageDirectory[pde_idx] & ~0xFFF);
        ULONG *kseg_pt = (ULONG *)(PageDirectory[pde_idx + (KSEG0_BASE >> 22)] & ~0xFFF);
        ULONG pte_idx = page & 0x3FF;

        if (page == 0) {
            /* Page 0 is not mapped (NULL pointer detection) */
            phys_pt[pte_idx] = 0;
            kseg_pt[pte_idx] = 0;
        } else {
            phys_pt[pte_idx] = (page << PAGE_SHIFT) | PAGE_PRESENT | PAGE_READWRITE;
            kseg_pt[pte_idx] = (page << PAGE_SHIFT) | PAGE_PRESENT | PAGE_READWRITE;
        }
    }

    /*
     * Self-mapping: PDE[768] points to the page directory itself.
     * This makes all page tables accessible at virtual 0xC0000000.
     * The page directory itself appears at 0xC0300000.
     * The kernel's MiGetPteAddress() depends on this.
     */
    PageDirectory[768] = ((ULONG)PageDirectory) | PAGE_PRESENT | PAGE_READWRITE;

    /*
     * HAL page table: PDE[1023] for VA 0xFFC00000-0xFFFFFFFF
     * The PCR and shared user data are mapped here.
     */
    ULONG *hal_pt = alloc_page_table();
    PageDirectory[1023] = ((ULONG)hal_pt) | PAGE_PRESENT | PAGE_READWRITE;

    /* KIP0PCRADDRESS (0xFFDFF000) -> BootPcr
     * PTE index = (0xFFDFF000 - 0xFFC00000) / 4096 = 511 */
    hal_pt[511] = ((ULONG)BootPcr) | PAGE_PRESENT | PAGE_READWRITE;

    /* KI_USER_SHARED_DATA (0xFFDF0000) -> SharedUserDataPage
     * PTE index = (0xFFDF0000 - 0xFFC00000) / 4096 = 496 */
    hal_pt[496] = ((ULONG)SharedUserDataPage) | PAGE_PRESENT | PAGE_READWRITE;

    print("  PCR at phys ");
    print_hex((ULONG)BootPcr);
    print(" -> virt 0xFFDFF000\n");
    print("  Page tables: ");
    print_hex(NextPageTable);
    print(" allocated\n");

    /* Load CR3 and enable paging */
    ULONG cr0;
    __asm__ volatile("mov %0, %%cr3" : : "r"((ULONG)PageDirectory));
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* PG bit */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    print("  Paging enabled. CR3=");
    print_hex((ULONG)PageDirectory);
    print("\n");
}

/*======================================================================
 * Memory descriptor list builder
 *====================================================================*/

static void add_memory_descriptor(MEMORY_TYPE type, ULONG base_page, ULONG page_count) {
    if (NumMemDescriptors >= 32) return;
    MEMORY_ALLOCATION_DESCRIPTOR *desc = &MemDescriptors[NumMemDescriptors++];
    desc->MemoryType = type;
    desc->BasePage = base_page;
    desc->PageCount = page_count;
    /* Will be linked into list later */
}

/*======================================================================
 * Build the LOADER_PARAMETER_BLOCK
 *====================================================================*/

static void build_loader_block(multiboot_info_t *mbi) {
    memset(&LoaderBlock, 0, sizeof(LoaderBlock));

    /* Initialize list heads */
    InitializeListHead(&LoaderBlock.LoadOrderListHead);
    InitializeListHead(&LoaderBlock.MemoryDescriptorListHead);
    InitializeListHead(&LoaderBlock.BootDriverListHead);

    /* Kernel stack for idle thread */
    LoaderBlock.KernelStack = (ULONG)&IdleStack[sizeof(IdleStack)];
    LoaderBlock.Thread = (ULONG)IdleThreadStorage;

    /* Strings */
    LoaderBlock.ArcBootDeviceName = ArcBootDevice;
    LoaderBlock.ArcHalDeviceName  = ArcHalDevice;
    LoaderBlock.NtBootPathName    = NtBootPath;
    LoaderBlock.NtHalPathName     = NtHalPath;
    LoaderBlock.LoadOptions       = LoadOptions;

    /* NLS data (dummy code page tables) */
    /* Initialize minimal ANSI code page header */
    memset(DummyAnsiNls, 0, sizeof(DummyAnsiNls));
    memset(DummyOemNls, 0, sizeof(DummyOemNls));
    memset(DummyUnicaseNls, 0, sizeof(DummyUnicaseNls));
    /* The first USHORT of each is the codepage ID */
    *(USHORT *)DummyAnsiNls = 1252;  /* Windows-1252 */
    *(USHORT *)DummyOemNls  = 437;   /* OEM 437 */

    NlsData.AnsiCodePageData     = DummyAnsiNls;
    NlsData.OemCodePageData      = DummyOemNls;
    NlsData.UnicodeCaseTableData = DummyUnicaseNls;
    LoaderBlock.NlsData = &NlsData;

    /* Registry (empty for now — will crash in config init) */
    memset(DummyRegistry, 0, sizeof(DummyRegistry));
    LoaderBlock.RegistryBase   = DummyRegistry;
    LoaderBlock.RegistryLength = sizeof(DummyRegistry);

    /* ARC disk info */
    InitializeListHead(&ArcDiskInfo.DiskSignatures);
    LoaderBlock.ArcDiskInformation = &ArcDiskInfo;

    /* Configuration root - NULL for now */
    LoaderBlock.ConfigurationRoot = NULL;

    /* I386 specific */
    LoaderBlock.I386.CommonDataArea = NULL;
    LoaderBlock.I386.MachineType = 0;  /* ISA */

    /* No setup loader */
    LoaderBlock.SetupLoaderBlock = NULL;
    LoaderBlock.OemFontFile = NULL;

    /*
     * Memory descriptors
     * Build a simple memory map. For QEMU with 64MB:
     *   0x000000 - 0x09FFFF  (640KB)   LoaderFree (low memory)
     *   0x100000 - 0x1FFFFF  (1MB)     LoaderSystemCode (kernel image area)
     *   0x200000 - 0x3FFFFF  (2MB)     LoaderFree
     *   0x400000 - 0x4FFFFF  (1MB)     LoaderHalCode (HAL image area)
     *   0x500000 - 0x5FFFFF  (1MB)     LoaderOsloaderHeap (our data)
     *   0x600000 - 0x3FFFFFF (58MB)    LoaderFree (rest of RAM)
     */
    NumMemDescriptors = 0;
    add_memory_descriptor(LoaderFree,         0x00,  0xA0);    /* 0-640KB */
    add_memory_descriptor(LoaderSystemCode,   0x100, 0x100);   /* 1-2MB */
    add_memory_descriptor(LoaderFree,         0x200, 0x200);   /* 2-4MB */
    add_memory_descriptor(LoaderHalCode,      0x400, 0x100);   /* 4-5MB */
    add_memory_descriptor(LoaderOsloaderHeap, 0x500, 0x100);   /* 5-6MB */
    add_memory_descriptor(LoaderFree,         0x600, 0x3A00);  /* 6-64MB */

    /* If multiboot gave us better memory info, use it */
    if (mbi && (mbi->flags & 0x01)) {
        /* Update the last free entry with actual RAM size */
        ULONG total_pages = (mbi->mem_upper + 1024) / 4;  /* mem_upper is KB above 1MB */
        if (total_pages > 0x600) {
            MemDescriptors[NumMemDescriptors - 1].PageCount = total_pages - 0x600;
        }
    }

    /* Link memory descriptors into the list */
    for (int i = 0; i < NumMemDescriptors; i++) {
        LIST_ENTRY *entry = &MemDescriptors[i].ListEntry;
        LIST_ENTRY *head = &LoaderBlock.MemoryDescriptorListHead;
        entry->Flink = head;
        entry->Blink = head->Blink;
        head->Blink->Flink = entry;
        head->Blink = entry;
    }

    /*
     * Load order list — ntoskrnl first, then hal
     */
    /* Fill in kernel module entry — must match what BlAllocateDataTableEntry does */
    memset(&KernelModule, 0, sizeof(KernelModule));
    KernelModule.DllBase = KernelBase;
    KernelModule.EntryPoint = (PVOID)KernelEntryPoint;
    KernelModule.SizeOfImage = KernelSizeOfImage;
    KernelModule.BaseDllName.Length = sizeof(KernelNameW) - sizeof(USHORT);
    KernelModule.BaseDllName.MaximumLength = sizeof(KernelNameW);
    KernelModule.BaseDllName.Buffer = KernelNameW;
    KernelModule.FullDllName = KernelModule.BaseDllName;
    /* CheckSum can be 0, the kernel just stores it */

    memset(&HalModule, 0, sizeof(HalModule));
    HalModule.DllBase = HalBase;
    HalModule.EntryPoint = (PVOID)HalEntryPoint;
    HalModule.SizeOfImage = HalSizeOfImage;
    HalModule.BaseDllName.Length = sizeof(HalNameW) - sizeof(USHORT);
    HalModule.BaseDllName.MaximumLength = sizeof(HalNameW);
    HalModule.BaseDllName.Buffer = HalNameW;
    HalModule.FullDllName = HalModule.BaseDllName;

    /* Insert into load order list */
    LIST_ENTRY *head = &LoaderBlock.LoadOrderListHead;
    KernelModule.InLoadOrderLinks.Flink = &HalModule.InLoadOrderLinks;
    KernelModule.InLoadOrderLinks.Blink = head;
    HalModule.InLoadOrderLinks.Flink = head;
    HalModule.InLoadOrderLinks.Blink = &KernelModule.InLoadOrderLinks;
    head->Flink = &KernelModule.InLoadOrderLinks;
    head->Blink = &HalModule.InLoadOrderLinks;

    /* Store pointer for assembly code */
    loader_block = (ULONG)&LoaderBlock;
}

/*======================================================================
 * Main loader entry point
 * Called from entry.S after GDT/IDT/TSS are set up
 * Returns address of KiSystemStartup
 *====================================================================*/

ULONG loader_main(multiboot_info_t *mbi) {
    serial_init();

    print("\n");
    print("MicroNT Boot Loader\n");
    print("===================\n\n");

    if (mbi) {
        print("Multiboot info at ");
        print_hex((ULONG)mbi);
        print("\n");
        if (mbi->flags & 0x01) {
            print("  Memory: lower=");
            print_hex(mbi->mem_lower);
            print("KB upper=");
            print_hex(mbi->mem_upper);
            print("KB\n");
        }
        print("  Modules: ");
        print_hex(mbi->mods_count);
        print("\n");
    }

    /* We expect two multiboot modules: ntoskrnl.exe and hal.dll */
    if (!mbi || mbi->mods_count < 2) {
        halt("Need 2 modules: ntoskrnl.exe and hal.dll\n"
             "Usage: qemu-system-i386 -kernel boot.elf -initrd \"ntoskrnl.exe,hal.dll\"");
    }

    multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;

    print("\nLoading PE images...\n");

    /* Module 0: ntoskrnl.exe (loads at 0x80100000, but PE is at physical 0x100000) */
    ULONG kern_file = mods[0].mod_start;
    ULONG kern_size = mods[0].mod_end - mods[0].mod_start;
    print("  Module 0: file at ");
    print_hex(kern_file);
    print(" size ");
    print_hex(kern_size);
    print("\n");

    /* Module 1: hal.dll (loads at 0x80400000, physical 0x400000) */
    ULONG hal_file = mods[1].mod_start;
    ULONG hal_size = mods[1].mod_end - mods[1].mod_start;
    print("  Module 1: file at ");
    print_hex(hal_file);
    print(" size ");
    print_hex(hal_size);
    print("\n");

    /* Parse PE headers to get load addresses */
    IMAGE_DOS_HEADER *kern_dos = (IMAGE_DOS_HEADER *)kern_file;
    IMAGE_NT_HEADERS *kern_nt = (IMAGE_NT_HEADERS *)(kern_file + kern_dos->e_lfanew);
    ULONG kern_image_base = kern_nt->OptionalHeader.ImageBase;
    KernelSizeOfImage = kern_nt->OptionalHeader.SizeOfImage;
    KernelEntryPoint = kern_image_base + kern_nt->OptionalHeader.AddressOfEntryPoint;

    IMAGE_DOS_HEADER *hal_dos = (IMAGE_DOS_HEADER *)hal_file;
    IMAGE_NT_HEADERS *hal_nt = (IMAGE_NT_HEADERS *)(hal_file + hal_dos->e_lfanew);
    ULONG hal_image_base = hal_nt->OptionalHeader.ImageBase;
    HalSizeOfImage = hal_nt->OptionalHeader.SizeOfImage;
    HalEntryPoint = hal_image_base + hal_nt->OptionalHeader.AddressOfEntryPoint;

    /*
     * The PE images have ImageBase at 0x80100000 / 0x80400000 (virtual).
     * These map to physical 0x100000 / 0x400000 via KSEG0.
     * We load into the PHYSICAL addresses first (before paging is on),
     * then enable paging with KSEG0 mapping.
     */
    ULONG kern_phys = kern_image_base - KSEG0_BASE;  /* 0x100000 */
    ULONG hal_phys  = hal_image_base - KSEG0_BASE;   /* 0x400000 */

    print("\n  Kernel: virtual ");
    print_hex(kern_image_base);
    print(" physical ");
    print_hex(kern_phys);
    print("\n");
    print("  HAL:    virtual ");
    print_hex(hal_image_base);
    print(" physical ");
    print_hex(hal_phys);
    print("\n\n");

    /* Load PE sections into PHYSICAL memory (before paging is enabled) */
    KernelBase = load_pe_image(kern_file, kern_phys, "ntoskrnl.exe");
    HalBase    = load_pe_image(hal_file, hal_phys, "hal.dll");

    /* Resolve PE imports between kernel and HAL */
    print("\nResolving imports...\n");
    resolve_imports(kern_phys, "ntoskrnl", kern_phys, hal_phys);
    resolve_imports(hal_phys, "hal", kern_phys, hal_phys);

    /* Set up page tables and enable paging */
    print("\nSetting up page tables...\n");
    setup_page_tables();

    /*
     * After paging: physical addresses are accessible via both
     * identity map (0-64MB) and KSEG0 (0x80000000+).
     * Update image pointers to virtual addresses.
     */
    KernelBase = (PVOID)kern_image_base;
    HalBase    = (PVOID)hal_image_base;

    /*
     * NSFixProcessorContext equivalent:
     * The kernel (GetMachineBootPointers) uses SGDT/SIDT to find the GDT/IDT.
     * It expects them at KSEG0 addresses. The real osloader relocates the
     * GDTR/IDTR base to KSEG0_BASE | physical_addr before calling the kernel.
     * The TSS must also be in KSEG0 space.
     */
    print("  Relocating GDT/IDT/TSS to KSEG0...\n");

    ULONG gdt_kseg0 = KSEG0_BASE | (ULONG)boot_gdt;
    ULONG idt_kseg0 = KSEG0_BASE | (ULONG)boot_idt;
    ULONG tss_kseg0 = KSEG0_BASE | (ULONG)boot_tss;

    print("    GDT: ");
    print_hex(gdt_kseg0);
    print("  IDT: ");
    print_hex(idt_kseg0);
    print("  TSS: ");
    print_hex(tss_kseg0);
    print("\n");

    /* Set up GDT entries with KSEG0 addresses */

    /* We need to write to the GDT via its KSEG0 address now */
    GDT_ENTRY *gdt_virt = (GDT_ENTRY *)gdt_kseg0;

    /* TSS descriptor (GDT entry 5, selector 0x28) - base = TSS KSEG0 address */
    {
        GDT_ENTRY *e = &gdt_virt[5];
        e->LimitLow = BOOT_TSS_SIZE - 1;
        e->BaseLow  = tss_kseg0 & 0xFFFF;
        e->BaseMid  = (tss_kseg0 >> 16) & 0xFF;
        e->Access   = 0x89;  /* Present, DPL=0, TSS32 available */
        e->LimitHigh = 0x00;
        e->BaseHigh = (tss_kseg0 >> 24) & 0xFF;
    }

    /* PCR descriptor (GDT entry 6, selector 0x30) - base = 0xFFDFF000 */
    {
        ULONG pcr_addr = 0xFFDFF000;
        GDT_ENTRY *e = &gdt_virt[6];
        e->LimitLow = PAGE_SIZE - 1;
        e->BaseLow  = pcr_addr & 0xFFFF;
        e->BaseMid  = (pcr_addr >> 16) & 0xFF;
        e->Access   = 0x92;  /* Present, DPL=0, data, read/write */
        e->LimitHigh = 0xC0; /* 32-bit, byte granularity */
        e->BaseHigh = (pcr_addr >> 24) & 0xFF;
    }

    /* Reload GDTR and IDTR with KSEG0 base addresses */
    {
        struct __attribute__((packed)) {
            USHORT limit;
            ULONG  base;
        } gdt_ptr, idt_ptr;

        /* GDT: 12 entries * 8 = 96 bytes minimum, but we allocated more */
        gdt_ptr.limit = (12 * 8) - 1;
        gdt_ptr.base  = gdt_kseg0;

        idt_ptr.limit = (256 * 8) - 1;
        idt_ptr.base  = idt_kseg0;

        __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));
        __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    }

    print("  Reloading segment registers...\n");

    /* Reload CS via far jump to pick up the KSEG0 GDT */
    __asm__ volatile(
        "ljmp $0x08, $1f\n"
        "1:\n"
    );

    /* Reload data segments */
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov $0x30, %%ax\n"
        "mov %%ax, %%fs\n"
        "xor %%ax, %%ax\n"
        "mov %%ax, %%gs\n"
        ::: "eax"
    );

    /* Load the TSS register */
    __asm__ volatile(
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        ::: "eax"
    );

    print("  Machine state relocated to KSEG0.\n");

    /* Build the loader parameter block */
    print("\nBuilding LOADER_PARAMETER_BLOCK...\n");
    build_loader_block(mbi);

    /*
     * Find KiSystemStartup in ntoskrnl's exports
     * For now, we use the PE entry point which is 'main' in NTOSKRNL.C
     * which calls ExpInitializeExecutive. But KiSystemStartup is the
     * real entry that sets up the idle thread etc.
     *
     * Actually, the PE entry point for ntoskrnl IS the startup function.
     * The INIT/UP SOURCES says NTTEST=ntoskrnl and the link uses -entry:main.
     * But main() in NTOSKRNL.C just calls KiSystemStartup.
     * So we can just use the PE entry point.
     */
    print("\nReady to start kernel.\n");
    print("  KiSystemStartup (entry): ");
    print_hex(KernelEntryPoint);
    print("\n");
    print("  LoaderBlock:             ");
    print_hex((ULONG)&LoaderBlock);
    print("\n\n");
    print("Jumping to kernel...\n\n");

    return KernelEntryPoint;
}
