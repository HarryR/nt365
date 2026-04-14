/*
 * LOADER_PARAMETER_BLOCK construction.
 *
 * Design:
 *   - Allocate a single "arena" (N physical pages) to hold the LPB and
 *     every structure it references (strings, descriptor array, LDR
 *     entries, configuration node, ARC disk signature).
 *   - A bump allocator carves sub-buffers. Each alloc returns a pointer
 *     addressable while we're still running with firmware paging, AND
 *     knows its future KSEG0 virtual address (phys | KSEG0_BASE).
 *   - Pointers written into NT structures are KSEG0 — no fixup pass.
 *
 * What goes into the block:
 *   LoadOrderList      : LDR_DATA_TABLE_ENTRY for ntoskrnl + hal + drivers
 *   BootDriverList     : BOOT_DRIVER_LIST_ENTRY for each driver
 *   MemoryDescriptorList: MEMORY_ALLOCATION_DESCRIPTOR chain from memmap
 *   NlsData            : pointers to the three NLS blobs we loaded
 *   RegistryBase/Length: from the registry allocation entry
 *   Arc* strings       : "multi(0)disk(0)rdisk(0)partition(1)" etc.
 *   ArcDiskInformation : single disk GUID/signature entry
 *   ConfigurationRoot  : single SystemClass ArcSystem node
 *   I386.{CommonDataArea, MachineType}
 *   KernelStack, Thread : idle stack top / ETHREAD placeholder (TODO)
 */
#include "loaderblock.h"
#include "com1.h"
#include "mmu.h"
#include "pe.h"
#include "memmap.h"
#include "nt.h"
#include <efilib.h>

/* ---- Arena allocator ---------------------------------------------------- */

#define ARENA_PAGES 4
static EFI_PHYSICAL_ADDRESS g_arena_phys = 0;
static UINTN                g_arena_used = 0;

static void *arena_alloc(UINTN size, UINTN align) {
    UINTN base = (UINTN)g_arena_phys + g_arena_used;
    UINTN pad  = (align - (base & (align - 1))) & (align - 1);
    void *p;
    g_arena_used += pad;
    p = (void *)(UINTN)(g_arena_phys + g_arena_used);
    g_arena_used += size;
    if (g_arena_used > (ARENA_PAGES << 12)) {
        com1_puts("[loaderblock] arena OOM\n");
        return 0;
    }
    /* Zero the allocation for determinism. */
    {
        UINT8 *u = p;
        for (UINTN i = 0; i < size; i++) u[i] = 0;
    }
    return p;
}

static void *kseg0_of(void *phys_ptr) {
    return (void *)((UINTN)phys_ptr | KSEG0_BASE);
}

/* ---- String/UNICODE helpers -------------------------------------------- */

static UINTN ascii_len(const char *s) {
    UINTN n = 0; while (s[n]) n++; return n;
}

static char *arena_dup_ascii(const char *s) {
    UINTN n = ascii_len(s);
    char *d = arena_alloc(n + 1, 1);
    if (!d) return 0;
    for (UINTN i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
    return d;
}

static USHORT *arena_dup_utf16_from_ascii(const char *s, USHORT *out_len) {
    UINTN n = ascii_len(s);
    USHORT *d = arena_alloc((n + 1) * 2, 2);
    if (!d) return 0;
    for (UINTN i = 0; i < n; i++) d[i] = (USHORT)(unsigned char)s[i];
    d[n] = 0;
    *out_len = (USHORT)(n * 2);
    return d;
}

/* ---- LDR_DATA_TABLE_ENTRY builder -------------------------------------- */

static LDR_DATA_TABLE_ENTRY *make_ldr_entry(const pe_image_t *img,
                                            const char *full_path,
                                            NT_LIST_ENTRY *list_head_kseg0) {
    LDR_DATA_TABLE_ENTRY *ldr = arena_alloc(sizeof *ldr, 4);
    USHORT fname_len = 0, bname_len = 0;
    USHORT *fname = arena_dup_utf16_from_ascii(full_path, &fname_len);
    /* Basename: last path component of full_path (after last '\\' or '/') */
    const char *base = full_path;
    for (const char *p = full_path; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    USHORT *bname = arena_dup_utf16_from_ascii(base, &bname_len);
    if (!ldr || !fname || !bname) return 0;

    ldr->DllBase       = (PVOID)(UINTN)img->image_base_va;
    ldr->EntryPoint    = (PVOID)(UINTN)(img->image_base_va + img->entry_rva);
    ldr->SizeOfImage   = img->size_of_image;
    ldr->FullDllName.Length        = fname_len;
    ldr->FullDllName.MaximumLength = fname_len + 2;
    ldr->FullDllName.Buffer        = kseg0_of(fname);
    ldr->BaseDllName.Length        = bname_len;
    ldr->BaseDllName.MaximumLength = bname_len + 2;
    ldr->BaseDllName.Buffer        = kseg0_of(bname);
    ldr->LoadCount = 1;

    /* Link LDR into the LoadOrder list (tail insert via pre-KSEG0 pointers). */
    NT_LIST_ENTRY *entry_kseg0 = kseg0_of(&ldr->InLoadOrderLinks);
    NT_LIST_ENTRY *tail_phys   = (NT_LIST_ENTRY *)((UINTN)list_head_kseg0 & ~KSEG0_BASE);
    /* Can't walk via virtual yet — walk via physical representation,
     * then rewrite pointers in KSEG0. Since arena is contiguous and we
     * keep tail_kseg0 updated, track it explicitly below. */
    (void)entry_kseg0; (void)tail_phys;
    return ldr;
}

static void list_tail_insert_kseg0(NT_LIST_ENTRY *head_phys, NT_LIST_ENTRY *entry_phys) {
    /* Both head and entry live in arena phys; we operate on them here and
     * write KSEG0 pointers into their Flink/Blink fields. */
    NT_LIST_ENTRY *prev_tail_phys = head_phys->Blink ?
                                 (NT_LIST_ENTRY *)((UINTN)head_phys->Blink & ~KSEG0_BASE) :
                                 head_phys;
    NT_LIST_ENTRY *head_kseg0  = kseg0_of(head_phys);
    NT_LIST_ENTRY *entry_kseg0 = kseg0_of(entry_phys);
    NT_LIST_ENTRY *prev_kseg0  = kseg0_of(prev_tail_phys);

    entry_phys->Flink = head_kseg0;
    entry_phys->Blink = prev_kseg0;
    prev_tail_phys->Flink = entry_kseg0;
    head_phys->Blink = entry_kseg0;
}

static void init_list_head_kseg0(NT_LIST_ENTRY *head_phys) {
    NT_LIST_ENTRY *k = kseg0_of(head_phys);
    head_phys->Flink = k;
    head_phys->Blink = k;
}

/* ---- State kept for handoff -------------------------------------------- */

static LOADER_PARAMETER_BLOCK *g_lpb_phys        = 0;
static UINT32                  g_kernel_entry_v  = 0;

/* NLS: main.c reads all three code-page files into one contiguous block
 * and hands us the base phys + per-file offsets. NT's Phase1Initialization
 * computes UnicodeCaseTableData as (base + offset), so the three pointers
 * MUST describe one contiguous mapping. */
static EFI_PHYSICAL_ADDRESS g_nls_base_phys = 0;
static UINTN                g_nls_ansi_off  = 0;
static UINTN                g_nls_oem_off   = 0;
static UINTN                g_nls_uni_off   = 0;

void loaderblock_set_nls(EFI_PHYSICAL_ADDRESS base_phys,
                         UINTN ansi_off, UINTN oem_off, UINTN uni_off) {
    g_nls_base_phys = base_phys;
    g_nls_ansi_off  = ansi_off;
    g_nls_oem_off   = oem_off;
    g_nls_uni_off   = uni_off;
}

/* Boot disk info from UEFI BlockIo + MBR read. See loaderblock.h. */
static UINT64 g_boot_disk_blocks     = 0;
static UINT32 g_boot_disk_block_size = 0;
static UINT32 g_boot_disk_mbr_sig    = 0;
static UINT32 g_boot_disk_mbr_sum    = 0;   /* two's complement of MBR sum */

void loaderblock_set_boot_disk(UINT64 total_blocks, UINT32 block_size,
                               UINT32 mbr_signature, UINT32 mbr_checksum) {
    g_boot_disk_blocks     = total_blocks;
    g_boot_disk_block_size = block_size;
    g_boot_disk_mbr_sig    = mbr_signature;
    g_boot_disk_mbr_sum    = mbr_checksum;
}

unsigned long loaderblock_handoff_ptr(void) {
    return g_lpb_phys ? (unsigned long)kseg0_of(g_lpb_phys) : 0;
}
unsigned long loaderblock_kernel_entry(void) { return g_kernel_entry_v; }

/* ---- Main builder ------------------------------------------------------ */

EFI_STATUS loaderblock_build(void) {
    EFI_STATUS s;

    s = mmu_alloc(ARENA_PAGES, PK_MEMORY_DATA, &g_arena_phys);
    if (EFI_ERROR(s)) return s;
    g_arena_used = 0;

    LOADER_PARAMETER_BLOCK *lpb = arena_alloc(sizeof *lpb, 16);
    g_lpb_phys = lpb;
    init_list_head_kseg0(&lpb->LoadOrderListHead);
    init_list_head_kseg0(&lpb->MemoryDescriptorListHead);
    init_list_head_kseg0(&lpb->BootDriverListHead);

    /* MemoryDescriptorListHead is populated by loaderblock_link_memmap
     * AFTER memmap_capture — see note on MapKey invalidation. The list
     * is initialized empty here; linking happens post-capture. */

    /* --- Registry hive pointer (single PK_REGISTRY entry) */
    {
        UINTN n = mmu_registry_count();
        for (UINTN i = 0; i < n; i++) {
            const AllocEntry *e = mmu_registry_entry(i);
            if (e->kind == PK_REGISTRY) {
                lpb->RegistryBase   = (void *)(UINTN)(KSEG0_BASE | (UINT32)e->phys);
                lpb->RegistryLength = (ULONG)(e->pages << 12);
                break;
            }
        }
    }

    /* --- NLS: main.c allocated one contiguous block via loaderblock_set_nls.
     * Three KSEG0 pointers into that block — offsets match the concatenated
     * layout the kernel's Phase1Initialization expects. */
    {
        NLS_DATA_BLOCK *nls = arena_alloc(sizeof *nls, 4);
        UINT32 base_kseg0 = KSEG0_BASE | (UINT32)g_nls_base_phys;
        nls->AnsiCodePageData     = (void *)(UINTN)(base_kseg0 + g_nls_ansi_off);
        nls->OemCodePageData      = (void *)(UINTN)(base_kseg0 + g_nls_oem_off);
        nls->UnicodeCaseTableData = (void *)(UINTN)(base_kseg0 + g_nls_uni_off);
        lpb->NlsData = kseg0_of(nls);
    }

    /* --- ArcDiskInformation: single entry placeholder (GPT support later) */
    {
        ARC_DISK_INFORMATION *adi = arena_alloc(sizeof *adi, 4);
        init_list_head_kseg0(&adi->DiskSignatures);
        ARC_DISK_SIGNATURE *sig = arena_alloc(sizeof *sig, 4);
        sig->Signature = g_boot_disk_mbr_sig;
        sig->CheckSum  = g_boot_disk_mbr_sum;
        sig->ValidPartitionTable = 1;
        sig->ArcName = kseg0_of(arena_dup_ascii("multi(0)disk(0)rdisk(0)"));
        list_tail_insert_kseg0(&adi->DiskSignatures, &sig->ListEntry);
        lpb->ArcDiskInformation = kseg0_of(adi);
    }

    /* --- Strings (KSEG0-fixed). */
    lpb->ArcBootDeviceName = kseg0_of(arena_dup_ascii("multi(0)disk(0)rdisk(0)partition(1)"));
    lpb->ArcHalDeviceName  = kseg0_of(arena_dup_ascii("multi(0)disk(0)rdisk(0)partition(1)"));
    /* Our FAT has System32/ at the partition root (no \WINNT\ wrapper), so
     * \SystemRoot = partition-root, i.e. NtBootPathName = "\". smss,
     * kernel32.dll, ntdll.dll etc. are then found at \System32\*. */
    lpb->NtBootPathName    = kseg0_of(arena_dup_ascii("\\"));
    lpb->NtHalPathName     = kseg0_of(arena_dup_ascii("\\"));
    lpb->LoadOptions       = kseg0_of(arena_dup_ascii(""));

    /* --- ConfigurationRoot: SystemClass node with a "Configuration Data"
     *     blob describing INT13 drive parameters for our boot disk.
     *
     * atdisk.sys (NTOS/DD/HARDDISK/I386/ATD_CONF.C:1322) opens
     * \Registry\Machine\Hardware\Description\System and reads its
     * "Configuration Data" value, parsing it as
     * CM_FULL_RESOURCE_DESCRIPTOR followed by an array of
     * CM_INT13_DRIVE_PARAMETER. If that lookup fails, atdisk's
     * diskExtension->Pi.PartitionLength stays at zero and every
     * IoCallDriver read gets STATUS_INVALID_PARAMETER — partitions
     * never get enumerated, STOP 0x7B on boot-volume mount.
     *
     * CmpInitializeRegistryNode (NTOS/CONFIG/CMCONFIG.C:657-705) expects
     * ConfigurationData to point at a CM_PARTIAL_RESOURCE_LIST (starting
     * with Version), NOT a full descriptor — it synthesizes the full
     * descriptor itself by prepending InterfaceType + BusNumber header.
     * So we emit just the partial list + trailing INT13 param array.
     *
     * Legacy BIOS populated this via NTDETECT.COM. Under UEFI we
     * synthesize it here from disk layout we know statically: 64 MiB
     * disk = 132*16*63 CHS (slight overshoot, fully covers partition 1). */
    {
        struct {
            NT_CM_PARTIAL_RESOURCE_LIST partial;
            NT_CM_INT13_DRIVE_PARAMETER drive0;
        } __attribute__((aligned(4))) *blob =
            arena_alloc(sizeof *blob, 4);

        blob->partial.Version  = 1;
        blob->partial.Revision = 1;
        blob->partial.Count    = 1;
        blob->partial.PartialDescriptors[0].Type = NT_CmResourceTypeDeviceSpecific;
        blob->partial.PartialDescriptors[0].ShareDisposition = 0;
        blob->partial.PartialDescriptors[0].Flags = 0;
        blob->partial.PartialDescriptors[0].DeviceSpecificData.DataSize =
            sizeof(NT_CM_INT13_DRIVE_PARAMETER);
        blob->partial.PartialDescriptors[0].DeviceSpecificData.Reserved1 = 0;
        blob->partial.PartialDescriptors[0].DeviceSpecificData.Reserved2 = 0;

        /* Synthesize CHS from the UEFI-reported block count. Fix heads=16
         * and sectors/track=63 (the BIOS INT 13h maximum and industry-
         * standard translation), compute cylinders to cover the whole
         * disk with some headroom. atdisk uses CHS-derived size as its
         * PartitionLength cap, so we need cyl*heads*spt*block_size >=
         * total_blocks*block_size. */
        {
            UINT32 spt   = 63;
            UINT32 heads = 16;
            /* ceil(blocks / (heads * spt)). Use 32-bit math: ia32 + freestanding
             * has no __udivdi3 helper linked, and 32-bit blocks (up to 4 TiB @
             * 512 B) is well past anything NT 3.5 can address via INT 13h. */
            UINT32 blocks_u32 = (g_boot_disk_blocks > 0xFFFFFFFFULL) ?
                                 0xFFFFFFFFu : (UINT32)g_boot_disk_blocks;
            UINT32 cyls  = (blocks_u32 + (heads * spt) - 1) / (heads * spt);
            if (cyls == 0) cyls = 1;

            blob->drive0.DriveSelect      = 0x80;        /* INT13 disk 0 */
            blob->drive0.MaxCylinders     = cyls - 1;    /* 0-based */
            blob->drive0.SectorsPerTrack  = (UINT16)spt;
            blob->drive0.MaxHeads         = (UINT16)(heads - 1); /* 0-based */
            blob->drive0.NumberDrives     = 1;
            com1_puts("[loaderblock] INT13 disk0: ");
            com1_put_dec((unsigned long)cyls);
            com1_puts(" cyl x ");
            com1_put_dec(heads);
            com1_puts(" head x ");
            com1_put_dec(spt);
            com1_puts(" sec\n");
        }

        CONFIGURATION_COMPONENT_DATA *cr = arena_alloc(sizeof *cr, 4);
        cr->ComponentEntry.Class                   = SystemClass;
        cr->ComponentEntry.Type                    = ArcSystem;
        cr->ComponentEntry.ConfigurationDataLength = sizeof *blob;
        cr->ConfigurationData                      = kseg0_of(blob);
        lpb->ConfigurationRoot = kseg0_of(cr);
    }

    /* --- I386-specific fields */
    lpb->I386.CommonDataArea = 0;
    lpb->I386.MachineType    = 0;  /* ISA */

    /* --- KernelStack (top of idle stack) / Thread placeholder */
    lpb->KernelStack = (ULONG)(KSEG0_BASE | (mmu_idle_stack_base() + (4 << 12)));
    /* Thread field is an ETHREAD pointer; kernel tolerates placeholder.
     * Reuse idle-stack base as the stand-in — kernel's Phase-0 init
     * overwrites it with the real system thread early on. */
    lpb->Thread = (ULONG)(KSEG0_BASE | mmu_idle_stack_base());

    /* --- (LoadOrderList + BootDriverList wired by loaderblock_wire_modules) */

    com1_puts("[loaderblock] LPB at phys ");
    com1_put_hex((unsigned long)g_arena_phys);
    com1_puts(" -> KSEG0 ");
    com1_put_hex((unsigned long)loaderblock_handoff_ptr());
    com1_puts("\n");

    return EFI_SUCCESS;
}

/*
 * Called AFTER memmap_capture (and after loaderblock_wire_modules).
 * Reads the captured UEFI memory map via memmap_to_nt, writes each
 * translated entry into the arena as a MEMORY_ALLOCATION_DESCRIPTOR,
 * and links into LoaderBlock.MemoryDescriptorListHead.
 * Does not allocate any UEFI pages — purely arena writes — so the
 * MapKey remains valid for ExitBootServices.
 */
EFI_STATUS loaderblock_link_memmap(void) {
    if (!g_lpb_phys) return EFI_NOT_READY;
    LOADER_PARAMETER_BLOCK *lpb = g_lpb_phys;

    NtMemEntry *nt = arena_alloc(sizeof(NtMemEntry) * 256, 4);
    if (!nt) return EFI_OUT_OF_RESOURCES;
    UINTN n_nt = 0;
    memmap_to_nt(nt, 256, &n_nt);
    com1_puts("[loaderblock] linking ");
    com1_put_dec(n_nt);
    com1_puts(" memory descriptors\n");
    for (UINTN i = 0; i < n_nt; i++) {
        MEMORY_ALLOCATION_DESCRIPTOR *d = arena_alloc(sizeof *d, 4);
        if (!d) break;
        d->MemoryType = (NT_MEMORY_TYPE)nt[i].memory_type;
        d->BasePage   = nt[i].base_page;
        d->PageCount  = nt[i].page_count;
        list_tail_insert_kseg0(&lpb->MemoryDescriptorListHead, &d->ListEntry);
    }

    com1_puts("[loaderblock] arena used ");
    com1_put_dec(g_arena_used);
    com1_puts(" / ");
    com1_put_dec(ARENA_PAGES << 12);
    com1_puts(" bytes\n");
    return EFI_SUCCESS;
}

/*
 * After loaderblock_build and after the images are pe_staged, wire up
 * the LoadOrderList (ntoskrnl + hal + drivers) and BootDriverList
 * (drivers). Passed as parallel arrays because main.c already has the
 * pe_image_t handles for each.
 */
EFI_STATUS loaderblock_wire_modules(const pe_image_t *kernel,
                                    const pe_image_t *hal,
                                    const pe_image_t *boot_drivers,
                                    UINTN n_boot_drivers) {
    if (!g_lpb_phys) return EFI_NOT_READY;
    LOADER_PARAMETER_BLOCK *lpb = g_lpb_phys;

    /* LoadOrderList: kernel first, then HAL, then drivers. */
    LDR_DATA_TABLE_ENTRY *k_ldr = make_ldr_entry(kernel,
        "\\WINNT\\SYSTEM32\\ntoskrnl.exe", &lpb->LoadOrderListHead);
    list_tail_insert_kseg0(&lpb->LoadOrderListHead, &k_ldr->InLoadOrderLinks);

    LDR_DATA_TABLE_ENTRY *h_ldr = make_ldr_entry(hal,
        "\\WINNT\\SYSTEM32\\hal.dll", &lpb->LoadOrderListHead);
    list_tail_insert_kseg0(&lpb->LoadOrderListHead, &h_ldr->InLoadOrderLinks);

    for (UINTN i = 0; i < n_boot_drivers; i++) {
        const pe_image_t *d = &boot_drivers[i];
        /* Build a driver path "\WINNT\SYSTEM32\Drivers\<name>" */
        char full[64]; UINTN fn = 0;
        static const char prefix[] = "\\WINNT\\SYSTEM32\\Drivers\\";
        for (UINTN j = 0; prefix[j]; j++) full[fn++] = prefix[j];
        for (UINTN j = 0; d->name[j] && fn < sizeof full - 1; j++) full[fn++] = d->name[j];
        full[fn] = 0;
        LDR_DATA_TABLE_ENTRY *ldr = make_ldr_entry(d, full, &lpb->LoadOrderListHead);
        list_tail_insert_kseg0(&lpb->LoadOrderListHead, &ldr->InLoadOrderLinks);

        BOOT_DRIVER_LIST_ENTRY *bde = arena_alloc(sizeof *bde, 4);
        USHORT flen = 0, rlen = 0;
        USHORT *fbuf = arena_dup_utf16_from_ascii(full, &flen);
        /* RegistryPath e.g. \Registry\Machine\System\CurrentControlSet\Services\atdisk */
        char reg[96]; UINTN rn = 0;
        static const char rprefix[] =
            "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        for (UINTN j = 0; rprefix[j]; j++) reg[rn++] = rprefix[j];
        /* strip ".sys" from driver name */
        for (UINTN j = 0; d->name[j] && rn < sizeof reg - 1; j++) {
            if (d->name[j] == '.') break;
            reg[rn++] = d->name[j];
        }
        reg[rn] = 0;
        USHORT *rbuf = arena_dup_utf16_from_ascii(reg, &rlen);

        bde->FilePath.Length        = flen;
        bde->FilePath.MaximumLength = flen + 2;
        bde->FilePath.Buffer        = kseg0_of(fbuf);
        bde->RegistryPath.Length        = rlen;
        bde->RegistryPath.MaximumLength = rlen + 2;
        bde->RegistryPath.Buffer        = kseg0_of(rbuf);
        bde->LdrEntry = kseg0_of(ldr);
        list_tail_insert_kseg0(&lpb->BootDriverListHead, &bde->Link);
    }

    /* Kernel entry point to hand off to. */
    g_kernel_entry_v = kernel->image_base_va + kernel->entry_rva;
    return EFI_SUCCESS;
}
