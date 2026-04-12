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

/* Hardware configuration tree.
 *
 * The kernel's CmpInitializeHardwareConfiguration walks this tree and
 * creates \Registry\Machine\Hardware\Description\... keys. A SystemClass
 * root produces "Description\System", which CmpInitializeMachineDependent-
 * Configuration (INIT386.C) then opens to add CentralProcessor etc.
 * Without this, NtOpenKey of that path fails with STATUS_OBJECT_NAME_NOT_FOUND.
 *
 * For QEMU with known hardware, a single-node root is sufficient — the
 * kernel populates the processor/NPX entries itself from KPRCB data. */
static CONFIGURATION_COMPONENT_DATA ConfigRoot;

/* NLS data — contiguous buffer with page-aligned sections, matching OSLOADER layout.
 * The kernel expects all three NLS files in one contiguous block typed LoaderNlsData. */
#define NLS_BUFFER_SIZE (256 * 1024)  /* 256KB should be plenty */
static UCHAR NlsBuffer[NLS_BUFFER_SIZE] __attribute__((aligned(4096)));
static ULONG NlsTotalSize;
static ULONG NlsAnsiPadded, NlsOemPadded;

/* Registry hive pointer (set from multiboot module, converted to KSEG0) */
static PVOID RegistryBase;
static ULONG RegistryLength;

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

/* PCR + Shared User Data: TWO contiguous pages (matching OSLOADER layout).
 * Page 0 = PCR (KIP0PCRADDRESS = 0xFFDFF000)
 * Page 1 = Shared User Data (KI_USER_SHARED_DATA = 0xFFDF0000)
 * The OSLOADER allocates these as LoaderStartupPcrPage. */
static UCHAR BootPcrPages[2 * 4096] __attribute__((aligned(4096)));
#define BootPcr           (BootPcrPages)
#define SharedUserDataPage (BootPcrPages + 4096)

/* TSS — separate allocation matching OSLOADER's LoaderMemoryData.
 * KTSS is ~0x2000 bytes, allocate 2 pages. */
static UCHAR BootTssPages[2 * 4096] __attribute__((aligned(4096)));

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
static char LoadOptions[]   = "CRASHDEBUG DEBUGPORT=COM1 BAUDRATE=115200";

/* Module names (wide char for UNICODE_STRING) */
static USHORT KernelNameW[] = { 'n','t','o','s','k','r','n','l','.','e','x','e',0 };
static USHORT HalNameW[]    = { 'h','a','l','.','d','l','l',0 };

/*======================================================================
 * Serial port output (COM2 0x2F8) for debug
 * COM1 is reserved for KD (kernel debugger binary protocol)
 *====================================================================*/

#define SERIAL_PORT 0x2F8  /* COM2 */

static inline void outb(USHORT port, UCHAR val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline UCHAR inb(USHORT port) {
    UCHAR ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00);  /* Disable interrupts */
    outb(SERIAL_PORT + 3, 0x80);  /* Enable DLAB */
    outb(SERIAL_PORT + 0, 0x03);  /* 38400 baud */
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);  /* 8N1 */
    outb(SERIAL_PORT + 2, 0xC7);  /* Enable FIFO */
    outb(SERIAL_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */
}

static void serial_putc(char c) {
    while (!(inb(SERIAL_PORT + 5) & 0x20));
    outb(SERIAL_PORT, c);
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

static int vga_enabled = 1;

static void vga_putc(char c) {
    if (!vga_enabled) return;
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

    /* Zero the entire image region first. BSS sections (where
     * VirtualSize > SizeOfRawData) must be zero — kernel globals like
     * KiIdleProcess, PsInitialSystemProcess live in BSS and must be NULL
     * on startup. Without this, Token/other pointer fields contain garbage. */
    memset((void *)load_addr, 0, size_of_image);

    /* Copy headers */
    ULONG header_size = nt->OptionalHeader.SizeOfHeaders;
    memcpy((void *)load_addr, (void *)file_base, header_size);

    /* Copy sections */
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)
        ((UCHAR *)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);

    for (ULONG i = 0; i < num_sections; i++) {
        ULONG dst = load_addr + sec[i].VirtualAddress;
        ULONG src = file_base + sec[i].PointerToRawData;
        ULONG raw_len = sec[i].SizeOfRawData;
        ULONG virt_len = sec[i].VirtualSize;
        (void)virt_len;  /* BSS tail zeroed by initial memset */
        serial_puts("    sec ");
        serial_hex(sec[i].VirtualAddress);
        serial_puts(" raw ");
        serial_hex(sec[i].PointerToRawData);
        serial_puts(" sz ");
        serial_hex(raw_len);
        serial_puts("/");
        serial_hex(virt_len);
        serial_puts(" -> ");
        serial_hex(dst);
        serial_putc('\n');
        if (raw_len > 0) {
            memcpy((void *)dst, (void *)src, raw_len);
        }
        /* BSS tail already zeroed by initial memset above */
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
            /* Return KSEG0 virtual address, not physical.
             * The kernel expects all function pointers in KSEG0 space. */
            return KSEG0_BASE | (image_base + func_table[ordinal]);
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

    /* NLS data — contiguous buffer with real code page files.
     * Point into the KSEG0-mapped NlsBuffer with page-aligned offsets. */
    {
        ULONG nls_kseg0 = KSEG0_BASE | (ULONG)NlsBuffer;
        NlsData.AnsiCodePageData     = (PVOID)nls_kseg0;
        NlsData.OemCodePageData      = (PVOID)(nls_kseg0 + NlsAnsiPadded);
        NlsData.UnicodeCaseTableData = (PVOID)(nls_kseg0 + NlsAnsiPadded + NlsOemPadded);
    }
    LoaderBlock.NlsData = &NlsData;

    /* Registry SYSTEM hive — loaded from multiboot module 5 */
    LoaderBlock.RegistryBase   = RegistryBase;
    LoaderBlock.RegistryLength = RegistryLength;

    /* ARC disk info */
    InitializeListHead(&ArcDiskInfo.DiskSignatures);
    LoaderBlock.ArcDiskInformation = &ArcDiskInfo;

    /* Hardware configuration root: single SystemClass node. The kernel walks
     * this tree to create \Registry\Machine\Hardware\Description\System which
     * CmpInitializeMachineDependentConfiguration needs to open.
     * Fields not listed are zero-init by BSS; that's fine because:
     *   - Parent/Child/Sibling are NULL (leaf node)
     *   - CmpInitializeRegistryNode forces Class=SystemClass -> Type=ArcSystem
     *   - ConfigurationDataLength/IdentifierLength=0 -> no data/identifier written */
    ConfigRoot.ComponentEntry.Class = SystemClass;
    ConfigRoot.ComponentEntry.Type  = ArcSystem;
    LoaderBlock.ConfigurationRoot = &ConfigRoot;  /* KSEG0-fixed up below */

    /* I386 specific */
    LoaderBlock.I386.CommonDataArea = NULL;
    LoaderBlock.I386.MachineType = 0;  /* ISA */

    /* No setup loader */
    LoaderBlock.SetupLoaderBlock = NULL;
    LoaderBlock.OemFontFile = NULL;

    /*
     * Memory descriptors — must cover ALL physical memory with NO overlaps.
     *
     * Layout for QEMU with 64MB:
     *   0x000-0x09F  LoaderFree        640KB   low memory
     *   0x0A0-0x0FF  LoaderFirmwarePerm 384KB  VGA/ROM area (not usable RAM)
     *   0x100-0x1C6  LoaderSystemCode   ~800KB kernel image (actual size)
     *   0x1C7-0x1FF  LoaderFree         ~228KB gap after kernel
     *   0x200-0x3FF  LoaderFree         2MB
     *   0x400-0x406  LoaderHalCode       28KB  HAL image (actual size)
     *   0x407-0x4FF  LoaderFree         ~1MB   gap after HAL
     *   0x500-0x5FF  LoaderFree         1MB
     *   0x600-0xDFF  LoaderFree         8MB
     *   0xE00-0xEFF  LoaderOsloaderHeap 1MB    boot stub code+BSS
     *     (PCR, TSS, NLS, page tables all live within this range)
     *   0xF00-end    LoaderFree         rest
     *
     * PCR, TSS, and NLS are NOT separate descriptors — they're part of
     * the OsloaderHeap range. The kernel finds them through specific
     * pointers in the LoaderBlock, not through memory type scanning.
     * Exception: LoaderNlsData IS needed — the kernel uses it to
     * calculate InitNlsTableSize. LoaderStartupPcrPage IS needed for
     * MmInitSystem. LoaderMemoryData IS needed for TSS.
     *
     * Solution: carve the 0xE00-0xEFF range into sub-regions.
     */
    {
        ULONG kern_pages = (KernelSizeOfImage + PAGE_SIZE - 1) >> PAGE_SHIFT;
        ULONG hal_pages = (HalSizeOfImage + PAGE_SIZE - 1) >> PAGE_SHIFT;
        ULONG nls_pages = (NlsTotalSize + PAGE_SIZE - 1) >> PAGE_SHIFT;
        ULONG nls_base = (ULONG)NlsBuffer >> PAGE_SHIFT;
        ULONG pcr_base = (ULONG)BootPcrPages >> PAGE_SHIFT;
        ULONG tss_base = (ULONG)BootTssPages >> PAGE_SHIFT;
        ULONG tss_pages = (sizeof(BootTssPages) + PAGE_SIZE - 1) >> PAGE_SHIFT;

        /* Total physical pages from multiboot */
        ULONG total_phys_pages = 0x4000;  /* default 64MB = 16384 pages */
        if (mbi && (mbi->flags & 0x01)) {
            total_phys_pages = (mbi->mem_upper + 1024) / 4;
        }

        NumMemDescriptors = 0;

        /* Low memory */
        add_memory_descriptor(LoaderFree,             0x00,  0xA0);
        add_memory_descriptor(LoaderFirmwarePermanent, 0xA0,  0x60);  /* VGA/ROM hole */

        /* Kernel image (exact size) + free gap */
        add_memory_descriptor(LoaderSystemCode,   0x100, kern_pages);
        if (0x100 + kern_pages < 0x200) {
            add_memory_descriptor(LoaderFree, 0x100 + kern_pages, 0x200 - (0x100 + kern_pages));
        }

        add_memory_descriptor(LoaderFree,         0x200, 0x200);     /* 2-4MB */

        /* HAL image (exact size) + free gap */
        add_memory_descriptor(LoaderHalCode,      0x400, hal_pages);
        if (0x400 + hal_pages < 0x500) {
            add_memory_descriptor(LoaderFree, 0x400 + hal_pages, 0x500 - (0x400 + hal_pages));
        }

        add_memory_descriptor(LoaderFree,         0x500, 0x100);     /* 5-6MB */
        add_memory_descriptor(LoaderFree,         0x600, 0x800);     /* 6-14MB */

        /* Boot stub region (0xE00-0xEFF): carve out specific sub-regions.
         * These BSS variables have known addresses within this range.
         * We mark the whole range as OsloaderHeap, then add specific
         * typed descriptors for sub-regions the kernel needs to find.
         * The kernel handles overlapping descriptors — the more specific
         * type wins during the PFN walk. */
        add_memory_descriptor(LoaderOsloaderHeap, 0xE00, 0x100);

        /* Specific typed sub-regions within the osloader heap */
        add_memory_descriptor(LoaderStartupPcrPage, pcr_base, 2);
        add_memory_descriptor(LoaderMemoryData,     tss_base, tss_pages);
        add_memory_descriptor(LoaderNlsData,        nls_base, nls_pages);

        /* Registry hive — multiboot module loaded by QEMU into physical RAM.
         * Physical address comes from the multiboot module table. */
        {
            ULONG reg_phys = (ULONG)RegistryBase & ~KSEG0_BASE;
            ULONG reg_base_page = reg_phys >> PAGE_SHIFT;
            ULONG reg_pages = (RegistryLength + PAGE_SIZE - 1) >> PAGE_SHIFT;
            add_memory_descriptor(LoaderRegistryData, reg_base_page, reg_pages);
        }

        /* Multiboot modules area — QEMU loads kernel/HAL/NLS/hive files
         * into physical memory starting around 0xE7E000. We need to protect
         * this region from being treated as free. The modules are:
         *   mod 0: ntoskrnl.exe (already covered by LoaderSystemCode at 0x100)
         *   mod 1: hal.dll (already covered by LoaderHalCode at 0x400)
         *   mod 2-4: NLS files (copied to NlsBuffer, originals can be freed)
         *   mod 5: SYSTEM hive (covered by LoaderRegistryData above)
         * The multiboot module DATA lives above 0xE7E000 through ~0xFFF000.
         * Mark this entire range as LoaderOsloaderHeap to protect it. */
        if (mbi && mbi->mods_count > 0) {
            multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;
            ULONG mods_start = mods[0].mod_start >> PAGE_SHIFT;
            ULONG mods_end_page = (mods[mbi->mods_count - 1].mod_end + PAGE_SIZE - 1) >> PAGE_SHIFT;
            /* Only add if it's in the free range above our boot stub */
            if (mods_start >= 0xF00) {
                add_memory_descriptor(LoaderOsloaderHeap, mods_start, mods_end_page - mods_start);
            }
        }

        /* Free memory: regions not covered by anything else.
         * Split around the multiboot modules area. */
        {
            ULONG free_start = 0xF00;
            if (mbi && mbi->mods_count > 0) {
                multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;
                ULONG mods_start = mods[0].mod_start >> PAGE_SHIFT;
                ULONG mods_end_page = (mods[mbi->mods_count - 1].mod_end + PAGE_SIZE - 1) >> PAGE_SHIFT;

                /* Free before modules */
                if (mods_start > free_start) {
                    add_memory_descriptor(LoaderFree, free_start, mods_start - free_start);
                }
                /* Free after modules */
                if (mods_end_page < total_phys_pages) {
                    add_memory_descriptor(LoaderFree, mods_end_page, total_phys_pages - mods_end_page);
                }
            } else if (total_phys_pages > free_start) {
                add_memory_descriptor(LoaderFree, free_start, total_phys_pages - free_start);
            }
        }
    }

    /* Dump memory descriptors for debugging */
    {
        static const char *type_names[] = {
            "ExceptnBl", "SystemCode", "HalCode   ", "LdrOsCode ",
            "BootDriver", "ConsAlloc ", "MaxAlloc  ", "OsloaderHp",
            "OsloaderSt", "FirmwareTP", "FirmwarePM", "Free      ",
            "Bad       ", "LoadedProg", "FirmwareTP", "SpecialMem",
            "BBTMemory ", "StartupPCR", "MemoryData", "NlsData   ",
            "SpecialTss", "DeviceTree"
        };
        print("\n  Memory Map:\n");
        for (int i = 0; i < NumMemDescriptors; i++) {
            MEMORY_ALLOCATION_DESCRIPTOR *d = &MemDescriptors[i];
            print("    ");
            print_hex(d->BasePage);
            print("-");
            print_hex(d->BasePage + d->PageCount - 1);
            print(" (");
            print_hex(d->PageCount);
            print(") ");
            if (d->MemoryType < 22)
                print(type_names[d->MemoryType]);
            else {
                print("type=");
                print_hex(d->MemoryType);
            }
            print("\n");
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

    /* Store pointer for assembly code — will be fixed up to KSEG0 later */
    loader_block = (ULONG)&LoaderBlock;
}

/*======================================================================
 * Main loader entry point
 * Called from entry.S after GDT/IDT/TSS are set up
 * Returns address of KiSystemStartup
 *====================================================================*/

ULONG loader_main(multiboot_info_t *mbi) {
    serial_init();

    /* Disable VGA on -cpu 486 / -display none configurations.
     * VGA access at 0xB8000 may fault without a VGA device. */
    vga_enabled = 0;

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

    /* We expect 6 multiboot modules:
     *   0: ntoskrnl.exe
     *   1: hal.dll
     *   2: c_1252.nls (ANSI code page)
     *   3: c_437.nls  (OEM code page)
     *   4: l_intl.nls (Unicode case table)
     *   5: SYSTEM     (registry hive)
     */
    if (!mbi || mbi->mods_count < 6) {
        halt("Need 6 modules: ntoskrnl.exe,hal.dll,c_1252.nls,c_437.nls,l_intl.nls,SYSTEM");
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

    /* Modules 2-4: NLS code page files.
     * The kernel expects all three in a single contiguous block with page-aligned
     * boundaries between them (matching OSLOADER's BlLoadNLSData layout).
     * Copy them into NlsBuffer. */
    {
        ULONG ansi_size = mods[2].mod_end - mods[2].mod_start;
        ULONG oem_size  = mods[3].mod_end - mods[3].mod_start;
        ULONG lang_size = mods[4].mod_end - mods[4].mod_start;
        NlsAnsiPadded = (ansi_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        NlsOemPadded  = (oem_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        ULONG lang_padded = (lang_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        NlsTotalSize = NlsAnsiPadded + NlsOemPadded + lang_padded;
        if (NlsTotalSize > NLS_BUFFER_SIZE) {
            halt("NLS data too large for buffer");
        }

        memset(NlsBuffer, 0, NlsTotalSize);
        memcpy(NlsBuffer, (void *)mods[2].mod_start, ansi_size);
        memcpy(NlsBuffer + NlsAnsiPadded, (void *)mods[3].mod_start, oem_size);
        memcpy(NlsBuffer + NlsAnsiPadded + NlsOemPadded, (void *)mods[4].mod_start, lang_size);

        print("  NLS: ANSI=");
        print_hex(ansi_size);
        print(" OEM=");
        print_hex(oem_size);
        print(" UniCS=");
        print_hex(lang_size);
        print(" total=");
        print_hex(NlsTotalSize);
        print("\n");
    }

    /* Module 5: SYSTEM registry hive */
    RegistryBase   = (PVOID)(KSEG0_BASE | mods[5].mod_start);
    RegistryLength = mods[5].mod_end - mods[5].mod_start;
    print("  Registry: ");
    print_hex((ULONG)RegistryBase);
    print(" (");
    print_hex(RegistryLength);
    print(" bytes)\n");

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

    /* Image pointers are now virtual addresses (identity-mapped = physical for us) */
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
    ULONG tss_kseg0 = KSEG0_BASE | (ULONG)BootTssPages;

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
        e->LimitLow = sizeof(BootTssPages) - 1;  /* Full KTSS size */
        e->BaseLow  = tss_kseg0 & 0xFFFF;
        e->BaseMid  = (tss_kseg0 >> 16) & 0xFF;
        e->Access   = 0x89;  /* Present, DPL=0, TSS32 available */
        e->LimitHigh = 0x00;
        e->BaseHigh = (tss_kseg0 >> 24) & 0xFF;
    }

    /* KGDT_GDT_ALIAS (GDT entry 14, selector 0x70) - describes the GDT itself.
     * KiInitializeAbios reads this to find the GDT length. Without it,
     * the ABIOS GDT free list scan loops forever. */
    {
        GDT_ENTRY *e = &gdt_virt[14];  /* selector 0x70 */
        ULONG gdt_size = 32 * 8;       /* 32 entries * 8 bytes = 256 (0x100) */
        e->LimitLow = (gdt_size - 1) & 0xFFFF;
        e->BaseLow  = gdt_kseg0 & 0xFFFF;
        e->BaseMid  = (gdt_kseg0 >> 16) & 0xFF;
        e->Access   = 0x92;  /* Present, DPL=0, data, read/write */
        e->LimitHigh = (((gdt_size - 1) >> 16) & 0x0F) | 0x40; /* D/B=1, G=0 (byte granularity) */
        e->BaseHigh = (gdt_kseg0 >> 24) & 0xFF;
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

        /* GDT: 32 entries * 8 = 256 bytes */
        gdt_ptr.limit = (32 * 8) - 1;
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

    /* Build the loader parameter block (must be before PTE zeroing,
     * since the zeroing iterates the memory descriptor list) */
    print("\nBuilding LOADER_PARAMETER_BLOCK...\n");
    build_loader_block(mbi);

    /*
     * NSUnmapFreeDescriptors equivalent: zero KSEG0 PTEs for free pages.
     * MmInitSystem uses PTE valid bits to determine which pages are free.
     * Without this, ALL pages have valid PTEs and the MM sees zero free pages.
     * Must run AFTER build_loader_block (which creates the descriptor list).
     */
    print("  Unmapping free pages from KSEG0...\n");
    {
        int i;
        for (i = 0; i < NumMemDescriptors; i++) {
            if (MemDescriptors[i].MemoryType == LoaderFree) {
                ULONG page;
                for (page = MemDescriptors[i].BasePage;
                     page < MemDescriptors[i].BasePage + MemDescriptors[i].PageCount;
                     page++) {
                    ULONG kseg0_va = KSEG0_BASE | (page << PAGE_SHIFT);
                    ULONG pde_idx = kseg0_va >> 22;
                    ULONG pte_idx = (kseg0_va >> 12) & 0x3FF;
                    ULONG pde = PageDirectory[pde_idx];
                    if (pde & PAGE_PRESENT) {
                        ULONG *pt = (ULONG *)(pde & ~0xFFF);
                        pt[pte_idx] = 0;
                    }
                }
            }
        }
        /* Flush TLB */
        ULONG cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
    }

    /*
     * Fix up all LoaderBlock pointers to KSEG0 addresses.
     * The kernel expects everything in KSEG0 (0x80000000+) space.
     * After MmInitSystem destroys the identity map, physical addresses
     * become inaccessible — only KSEG0 mappings survive.
     */
    {
        #define TOKSEG(p) ((PVOID)((ULONG)(p) | KSEG0_BASE))
        #define FIXLIST(head) do { \
            LIST_ENTRY *_h = (head); \
            LIST_ENTRY *_e = _h->Flink; \
            /* Fix head's Flink/Blink */ \
            _h->Flink = TOKSEG(_h->Flink); \
            _h->Blink = TOKSEG(_h->Blink); \
            /* Walk list and fix each entry's Flink/Blink */ \
            while (_e != _h) { \
                LIST_ENTRY *_next = _e->Flink; \
                _e->Flink = TOKSEG(_e->Flink); \
                _e->Blink = TOKSEG(_e->Blink); \
                _e = _next; \
            } \
        } while(0)

        /* String pointers */
        LoaderBlock.ArcBootDeviceName = TOKSEG(LoaderBlock.ArcBootDeviceName);
        LoaderBlock.ArcHalDeviceName  = TOKSEG(LoaderBlock.ArcHalDeviceName);
        LoaderBlock.NtBootPathName    = TOKSEG(LoaderBlock.NtBootPathName);
        LoaderBlock.NtHalPathName     = TOKSEG(LoaderBlock.NtHalPathName);
        LoaderBlock.LoadOptions       = TOKSEG(LoaderBlock.LoadOptions);

        /* Struct pointers */
        LoaderBlock.NlsData           = TOKSEG(LoaderBlock.NlsData);
        LoaderBlock.ArcDiskInformation = TOKSEG(LoaderBlock.ArcDiskInformation);
        LoaderBlock.ConfigurationRoot  = TOKSEG(LoaderBlock.ConfigurationRoot);

        /* Idle thread/stack */
        LoaderBlock.KernelStack = (ULONG)LoaderBlock.KernelStack | KSEG0_BASE;
        LoaderBlock.Thread      = (ULONG)LoaderBlock.Thread | KSEG0_BASE;

        /* ArcDiskInfo list head */
        {
            ARC_DISK_INFORMATION *adi = (ARC_DISK_INFORMATION *)TOKSEG(&ArcDiskInfo);
            /* Empty list — just fix up the self-pointers */
            adi->DiskSignatures.Flink = &adi->DiskSignatures;
            adi->DiskSignatures.Blink = &adi->DiskSignatures;
        }

        /* Fix linked lists */
        FIXLIST(&LoaderBlock.MemoryDescriptorListHead);
        FIXLIST(&LoaderBlock.LoadOrderListHead);
        FIXLIST(&LoaderBlock.BootDriverListHead);

        /* Fix LDR_DATA_TABLE_ENTRY name buffers */
        KernelModule.BaseDllName.Buffer = TOKSEG(KernelModule.BaseDllName.Buffer);
        KernelModule.FullDllName.Buffer = TOKSEG(KernelModule.FullDllName.Buffer);
        HalModule.BaseDllName.Buffer = TOKSEG(HalModule.BaseDllName.Buffer);
        HalModule.FullDllName.Buffer = TOKSEG(HalModule.FullDllName.Buffer);

        /* LoaderBlock pointer itself */
        loader_block = (ULONG)&LoaderBlock | KSEG0_BASE;

        #undef TOKSEG
        #undef FIXLIST
    }

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
