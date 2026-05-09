#include "lpb.h"
#include "arena.h"
#include "fwcfg.h"
#include "log.h"
#include "mmu.h"
#include "memmap.h"
#include "nt.h"

/* ---- KSEG0 / list-head helpers ---------------------------------------- */

static UINT32 kseg0_of(void *phys_ptr) {
    return (UINT32)((UINTN)phys_ptr | KSEG0_BASE);
}

static void init_list_head(NT_LIST_ENTRY *head_phys) {
    UINT32 k = kseg0_of(head_phys);
    head_phys->Flink = k;
    head_phys->Blink = k;
}

/* Both head and entry live in arena phys. Flink/Blink are UINT32 wire-
 * format KSEG0 VAs — we strip the high bit when we need to walk the list
 * physically (here, pre-handoff). */
static void list_tail_insert(NT_LIST_ENTRY *head_phys, NT_LIST_ENTRY *entry_phys) {
    NT_LIST_ENTRY *prev_tail_phys =
        head_phys->Blink
            ? (NT_LIST_ENTRY *)((UINTN)head_phys->Blink & ~KSEG0_BASE)
            : head_phys;

    entry_phys->Flink     = kseg0_of(head_phys);
    entry_phys->Blink     = kseg0_of(prev_tail_phys);
    prev_tail_phys->Flink = kseg0_of(entry_phys);
    head_phys->Blink      = kseg0_of(entry_phys);
}

/* ---- LDR_DATA_TABLE_ENTRY builder ------------------------------------- */

static LDR_DATA_TABLE_ENTRY *make_ldr_entry(const pe_image_t *img,
                                            const char *full_path) {
    LDR_DATA_TABLE_ENTRY *ldr = arena_alloc(sizeof *ldr, 4);
    UINT16 fname_len = 0, bname_len = 0;
    UINT16 *fname = arena_dup_utf16_from_ascii(full_path, &fname_len);

    /* Basename: last path component of full_path (after last '\\' or '/'). */
    const char *base = full_path;
    for (const char *p = full_path; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    UINT16 *bname = arena_dup_utf16_from_ascii(base, &bname_len);
    if (!ldr || !fname || !bname) return 0;

    ldr->DllBase                   = (PVOID)(UINTN)img->image_base_va;
    ldr->EntryPoint                = (PVOID)(UINTN)(img->image_base_va + img->entry_rva);
    ldr->SizeOfImage               = img->size_of_image;
    ldr->FullDllName.Length        = fname_len;
    ldr->FullDllName.MaximumLength = fname_len + 2;
    ldr->FullDllName.Buffer        = kseg0_of(fname);
    ldr->BaseDllName.Length        = bname_len;
    ldr->BaseDllName.MaximumLength = bname_len + 2;
    ldr->BaseDllName.Buffer        = kseg0_of(bname);
    ldr->LoadCount                 = 1;
    return ldr;
}

/* ---- Caller-supplied state (latched before lpb_build) ------------------ */

static LOADER_PARAMETER_BLOCK *g_lpb_phys       = 0;
static UINT32                  g_kernel_entry_v = 0;

/* NLS: main.c reads all three code-page files into one contiguous block
 * and hands us the base phys + per-file offsets. Phase1Initialization
 * computes UnicodeCaseTableData as `base + offset`, so the three
 * pointers MUST describe one contiguous mapping. */
static EFI_PHYSICAL_ADDRESS g_nls_base_phys = 0;
static UINTN                g_nls_ansi_off  = 0;
static UINTN                g_nls_oem_off   = 0;
static UINTN                g_nls_uni_off   = 0;

void lpb_set_nls(EFI_PHYSICAL_ADDRESS base_phys,
                 UINTN ansi_off, UINTN oem_off, UINTN uni_off) {
    g_nls_base_phys = base_phys;
    g_nls_ansi_off  = ansi_off;
    g_nls_oem_off   = oem_off;
    g_nls_uni_off   = uni_off;
}

/* Boot-disk MBR identity (ArcDiskInformation) + the partition numbers
 * the kernel resolves \SystemRoot and HAL against.  Layout-probe in
 * main.c picks the partition numbers from the actual MBR. */
static UINT32 g_boot_disk_mbr_sig  = 0;
static UINT32 g_boot_disk_mbr_sum  = 0;
static UINT8  g_boot_part          = 1;
static UINT8  g_hal_part           = 1;

void lpb_set_boot_disk(UINT32 mbr_signature, UINT32 mbr_checksum,
                       UINT8 boot_part, UINT8 hal_part) {
    g_boot_disk_mbr_sig = mbr_signature;
    g_boot_disk_mbr_sum = mbr_checksum;
    g_boot_part         = boot_part;
    g_hal_part          = hal_part;
}

/* Wall-clock seed (UEFI gRT->GetTime() result).  Year==0 = unset;
 * lpb_build skips the arena emit in that case and Spare1 stays 0. */
static EFI_TIME g_boot_time;

void lpb_set_boot_time(const EFI_TIME *t) { g_boot_time = *t; }

static UINT32 g_config_root = 0;

void lpb_set_configuration_root(UINT32 root_kseg0) {
    g_config_root = root_kseg0;
}

unsigned long lpb_handoff_ptr(void) {
    return g_lpb_phys ? (unsigned long)kseg0_of(g_lpb_phys) : 0;
}
unsigned long lpb_kernel_entry(void) { return g_kernel_entry_v; }

/* ---- Main builder ------------------------------------------------------ */

EFI_STATUS lpb_build(void) {
    /* Caller (main.c) is expected to have already called arena_init with
     * LPB_ARENA_PAGES so hwtree_build and any other arena consumer can
     * emit before lpb_build runs. */
    LOADER_PARAMETER_BLOCK *lpb = arena_alloc(sizeof *lpb, 16);
    if (!lpb) return EFI_OUT_OF_RESOURCES;
    g_lpb_phys = lpb;

    init_list_head(&lpb->LoadOrderListHead);
    init_list_head(&lpb->MemoryDescriptorListHead);
    init_list_head(&lpb->BootDriverListHead);

    /* MemoryDescriptorListHead populated later by lpb_link_memmap (must
     * wait for memmap_capture, and the list write needs no UEFI calls
     * so MapKey stays valid). */

    /* --- Registry hive pointer (single PK_REGISTRY entry) */
    {
        UINTN n = mmu_registry_count();
        for (UINTN i = 0; i < n; i++) {
            const AllocEntry *e = mmu_registry_entry(i);
            if (e->kind == PK_REGISTRY) {
                lpb->RegistryBase   = (UINT32)(KSEG0_BASE | (UINT32)e->phys);
                lpb->RegistryLength = (ULONG)(e->pages << 12);
                break;
            }
        }
    }

    /* --- NLS: main.c already allocated a contiguous block. Three KSEG0
     * pointers into that block at the per-file offsets. */
    {
        NLS_DATA_BLOCK *nls = arena_alloc(sizeof *nls, 4);
        if (!nls) return EFI_OUT_OF_RESOURCES;
        UINT32 base_kseg0 = KSEG0_BASE | (UINT32)g_nls_base_phys;
        nls->AnsiCodePageData     = base_kseg0 + (UINT32)g_nls_ansi_off;
        nls->OemCodePageData      = base_kseg0 + (UINT32)g_nls_oem_off;
        nls->UnicodeCaseTableData = base_kseg0 + (UINT32)g_nls_uni_off;
        lpb->NlsData = kseg0_of(nls);
    }

    /* --- ArcDiskInformation: single entry for the boot disk. */
    {
        ARC_DISK_INFORMATION *adi = arena_alloc(sizeof *adi, 4);
        if (!adi) return EFI_OUT_OF_RESOURCES;
        init_list_head(&adi->DiskSignatures);

        ARC_DISK_SIGNATURE *sig = arena_alloc(sizeof *sig, 4);
        if (!sig) return EFI_OUT_OF_RESOURCES;
        sig->Signature           = g_boot_disk_mbr_sig;
        sig->CheckSum            = g_boot_disk_mbr_sum;
        sig->ValidPartitionTable = 1;
        sig->ArcName             = kseg0_of(arena_dup_ascii("multi(0)disk(0)rdisk(0)"));
        list_tail_insert(&adi->DiskSignatures, &sig->ListEntry);
        lpb->ArcDiskInformation = kseg0_of(adi);
    }

    /* --- Paths + options.  Partition numbers come from the layout
     * probe in main.c; we don't bake any partition number into the
     * loader at build time.
     *
     * Layouts handled today:
     *   1 partition  (FAT16):                       boot=hal=partition(1)
     *   ESP + FAT16 system:                         hal=ESP, boot=system
     *   ESP + NTFS system:                          hal=ESP, boot=system
     *
     * ArcBootDeviceName is what the kernel resolves \SystemRoot
     * against; ArcHalDeviceName tracks where HAL was loaded from
     * (the ESP for two-partition layouts; same as boot for single).
     * NtBootPathName = "\" puts \SystemRoot at the system-partition
     * root (no \WINNT\ wrapper). */
    {
        static const char prefix[] = "multi(0)disk(0)rdisk(0)partition(";
        const UINTN prefix_len = sizeof(prefix) - 1;
        /* "multi(0)disk(0)rdisk(0)partition(" + "N" + ")" + NUL = +3. */
        char *boot_arc = arena_alloc(prefix_len + 3, 1);
        char *hal_arc  = arena_alloc(prefix_len + 3, 1);
        if (!boot_arc || !hal_arc) return EFI_OUT_OF_RESOURCES;
        for (UINTN i = 0; i < prefix_len; i++) {
            boot_arc[i] = prefix[i];
            hal_arc[i]  = prefix[i];
        }
        boot_arc[prefix_len + 0] = '0' + (g_boot_part % 10);
        boot_arc[prefix_len + 1] = ')';
        boot_arc[prefix_len + 2] = 0;
        hal_arc [prefix_len + 0] = '0' + (g_hal_part  % 10);
        hal_arc [prefix_len + 1] = ')';
        hal_arc [prefix_len + 2] = 0;
        lpb->ArcBootDeviceName = kseg0_of(boot_arc);
        lpb->ArcHalDeviceName  = kseg0_of(hal_arc);
    }
    lpb->NtBootPathName    = kseg0_of(arena_dup_ascii("\\"));
    lpb->NtHalPathName     = kseg0_of(arena_dup_ascii("\\"));

    /* LoadOptions: read from the qemu fw_cfg file `opt/micront/loadopts`
     * if present (boot.sh --kernel-opts populates it).  Empty string
     * otherwise — kernel parsers tolerate it the same as a missing
     * flag.  256 bytes is roughly twice the longest plausible flag
     * combo (NT 4 max LoadOptions in boot.ini was 128). */
    {
        char *opts = arena_alloc(256, 1);
        if (!opts) return EFI_OUT_OF_RESOURCES;
        opts[0] = 0;
        unsigned n = fwcfg_read_string("opt/micront/loadopts", opts, 256);
        if (n > 1) {
            BXLOG(L"LoadOptions: '%a'", opts);
        }
        lpb->LoadOptions = kseg0_of(opts);
    }

    /* --- UEFI wall-clock seed (optional).  Stash the EFI_TIME
     * latched by lpb_set_boot_time into the arena, point Spare1 at
     * its KSEG0 VA.  HAL declares a parallel struct (it doesn't
     * include efi.h) and converts at HAL init. */
    if (g_boot_time.Year != 0) {
        EFI_TIME *t = arena_alloc(sizeof(EFI_TIME), 4);
        if (t) {
            *t = g_boot_time;
            lpb->Spare1 = kseg0_of(t);
        }
    }

    /* --- Hardware inventory root (built by hwtree; caller set it). */
    lpb->ConfigurationRoot = g_config_root;

    /* --- I386-specific. */
    lpb->I386.CommonDataArea = 0;
    lpb->I386.MachineType    = 0;        /* ISA */

    /* --- KernelStack (top of idle stack) + Thread placeholder. */
    lpb->KernelStack = (ULONG)(KSEG0_BASE | (mmu_idle_stack_base() + (4 << 12)));
    /* Thread is an ETHREAD pointer; kernel tolerates a placeholder.
     * Reuse idle-stack base — Phase-0 init overwrites it with the real
     * system thread early on. */
    lpb->Thread = (ULONG)(KSEG0_BASE | mmu_idle_stack_base());

    BXLOG(L"LPB at phys 0x%lx -> KSEG0 0x%lx",
          (UINT64)arena_phys(), (UINT64)lpb_handoff_ptr());

    return EFI_SUCCESS;
}

EFI_STATUS lpb_link_memmap(void) {
    if (!g_lpb_phys) return EFI_NOT_READY;
    LOADER_PARAMETER_BLOCK *lpb = g_lpb_phys;

    /* Runs AFTER memmap_capture, so no BXLOG here: Print() allocates
     * internally via UEFI BootServices, which invalidates the MapKey
     * we need for ExitBootServices. Only pure arena writes are safe.
     * memmap_to_nt's own "%lu NT descriptors" line logs pre-capture
     * from within the UEFI-map walk, which is enough to know what got
     * translated. */
    NtMemEntry *nt = arena_alloc(sizeof(NtMemEntry) * 256, 4);
    if (!nt) return EFI_OUT_OF_RESOURCES;
    UINTN n_nt = 0;
    memmap_to_nt(nt, 256, &n_nt);

    for (UINTN i = 0; i < n_nt; i++) {
        MEMORY_ALLOCATION_DESCRIPTOR *d = arena_alloc(sizeof *d, 4);
        if (!d) break;
        d->MemoryType = (NT_MEMORY_TYPE)nt[i].memory_type;
        d->BasePage   = nt[i].base_page;
        d->PageCount  = nt[i].page_count;
        list_tail_insert(&lpb->MemoryDescriptorListHead, &d->ListEntry);
    }

    return EFI_SUCCESS;
}

EFI_STATUS lpb_wire_modules(const pe_image_t *kernel,
                            const pe_image_t *hal,
                            const pe_image_t *boot_drivers,
                            UINTN n_boot_drivers) {
    if (!g_lpb_phys) return EFI_NOT_READY;
    LOADER_PARAMETER_BLOCK *lpb = g_lpb_phys;

    /* LoadOrderList: kernel first, then HAL, then drivers. */
    LDR_DATA_TABLE_ENTRY *k_ldr = make_ldr_entry(kernel,
        "\\WINNT\\SYSTEM32\\ntoskrnl.exe");
    list_tail_insert(&lpb->LoadOrderListHead, &k_ldr->InLoadOrderLinks);

    LDR_DATA_TABLE_ENTRY *h_ldr = make_ldr_entry(hal,
        "\\WINNT\\SYSTEM32\\hal.dll");
    list_tail_insert(&lpb->LoadOrderListHead, &h_ldr->InLoadOrderLinks);

    for (UINTN i = 0; i < n_boot_drivers; i++) {
        const pe_image_t *d = &boot_drivers[i];

        /* Build driver path "\WINNT\SYSTEM32\Drivers\<name>". */
        char full[64]; UINTN fn = 0;
        static const char prefix[] = "\\WINNT\\SYSTEM32\\Drivers\\";
        for (UINTN j = 0; prefix[j]; j++) full[fn++] = prefix[j];
        for (UINTN j = 0; d->name[j] && fn < sizeof full - 1; j++) full[fn++] = d->name[j];
        full[fn] = 0;

        LDR_DATA_TABLE_ENTRY *ldr = make_ldr_entry(d, full);
        list_tail_insert(&lpb->LoadOrderListHead, &ldr->InLoadOrderLinks);

        BOOT_DRIVER_LIST_ENTRY *bde = arena_alloc(sizeof *bde, 4);
        UINT16 flen = 0, rlen = 0;
        UINT16 *fbuf = arena_dup_utf16_from_ascii(full, &flen);

        /* RegistryPath: \Registry\Machine\System\CurrentControlSet\Services\<base>
         * where <base> is the driver name minus the .sys suffix. */
        char reg[96]; UINTN rn = 0;
        static const char rprefix[] =
            "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        for (UINTN j = 0; rprefix[j]; j++) reg[rn++] = rprefix[j];
        for (UINTN j = 0; d->name[j] && rn < sizeof reg - 1; j++) {
            if (d->name[j] == '.') break;
            reg[rn++] = d->name[j];
        }
        reg[rn] = 0;
        UINT16 *rbuf = arena_dup_utf16_from_ascii(reg, &rlen);

        bde->FilePath.Length            = flen;
        bde->FilePath.MaximumLength     = flen + 2;
        bde->FilePath.Buffer            = kseg0_of(fbuf);
        bde->RegistryPath.Length        = rlen;
        bde->RegistryPath.MaximumLength = rlen + 2;
        bde->RegistryPath.Buffer        = kseg0_of(rbuf);
        bde->LdrEntry                   = kseg0_of(ldr);
        list_tail_insert(&lpb->BootDriverListHead, &bde->Link);
    }

    /* Kernel entry point — handoff.S tail-calls this. */
    g_kernel_entry_v = kernel->image_base_va + kernel->entry_rva;
    return EFI_SUCCESS;
}
