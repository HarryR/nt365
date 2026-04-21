#include "mmu.h"
#include "log.h"
#include <efilib.h>

/*
 * Pre-ExitBootServices we AllocatePages() via UEFI. Post-exit, UEFI is
 * gone; all allocation must be done by now. The registry lets other
 * modules (fs, loaderblock, memmap) find out what we grabbed and why.
 */

#define MMU_REGISTRY_MAX 64

static AllocEntry g_reg[MMU_REGISTRY_MAX];
static UINTN      g_reg_n = 0;

/* Identity-only registry: phys ranges that need to be present in the
 * 32-bit PD's identity map but shouldn't show up in the KSEG0 mirror or
 * the memmap NT-type overlay. Loader image (UEFI-placed) + current stack
 * land here. Small — 8 slots is plenty since we only register a handful
 * of well-defined ranges. */
#define MMU_IMAGE_MAX 8
static AllocEntry g_image[MMU_IMAGE_MAX];
static UINTN      g_image_n = 0;

UINTN             mmu_registry_count(void)      { return g_reg_n; }
const AllocEntry *mmu_registry_entry(UINTN i) {
    return (i < g_reg_n) ? &g_reg[i] : 0;
}

static const char *kind_name(PageKind k) {
    switch (k) {
    case PK_FREE:           return "FREE";
    case PK_KERNEL_IMAGE:   return "KernelImage";
    case PK_HAL_IMAGE:      return "HalImage";
    case PK_BOOT_DRIVER:    return "BootDriver";
    case PK_REGISTRY:       return "Registry";
    case PK_NLS:            return "Nls";
    case PK_PCR:            return "Pcr";
    case PK_MEMORY_DATA:    return "MemoryData";
    case PK_FIRMWARE_PERM:  return "FwPerm";
    case PK_FIRMWARE_TEMP:  return "FwTemp";
    default:                return "?";
    }
}

static EFI_STATUS mmu_alloc_impl(UINTN pages, PageKind kind,
                                 EFI_ALLOCATE_TYPE mode,
                                 EFI_PHYSICAL_ADDRESS *out_phys,
                                 EFI_PHYSICAL_ADDRESS preferred) {
    EFI_PHYSICAL_ADDRESS phys = preferred;
    EFI_STATUS status;

    if (g_reg_n >= MMU_REGISTRY_MAX) {
        BXLOG(L"registry full");
        return EFI_OUT_OF_RESOURCES;
    }

    status = uefi_call_wrapper(BS->AllocatePages, 4,
                               mode, EfiLoaderData,
                               pages, &phys);
    if (EFI_ERROR(status)) {
        BXLOG(L"AllocatePages(%u) for %a failed: 0x%lx",
              (UINT32)pages, kind_name(kind), (UINT64)status);
        return status;
    }

    g_reg[g_reg_n].phys  = phys;
    g_reg[g_reg_n].pages = pages;
    g_reg[g_reg_n].kind  = kind;
    g_reg_n++;

    /* No per-alloc log on success — callers that care (fs_read,
     * pe_stage, hwtree, lpb_build, mmu_alloc_pt_pool) emit their own
     * semantic lines with phys + purpose; an additional line per
     * mmu_alloc is pure noise. The error paths above still log. */
    if (out_phys) *out_phys = phys;
    return EFI_SUCCESS;
}

EFI_STATUS mmu_alloc(UINTN pages, PageKind kind,
                     EFI_PHYSICAL_ADDRESS *out_phys) {
    return mmu_alloc_impl(pages, kind, AllocateAnyPages, out_phys, 0);
}

EFI_STATUS mmu_alloc_at(UINTN pages, PageKind kind,
                        EFI_PHYSICAL_ADDRESS preferred,
                        EFI_PHYSICAL_ADDRESS *out_phys) {
    return mmu_alloc_impl(pages, kind, AllocateAddress, out_phys, preferred);
}

EFI_STATUS mmu_alloc_below(UINTN pages, PageKind kind,
                           EFI_PHYSICAL_ADDRESS max_addr,
                           EFI_PHYSICAL_ADDRESS *out_phys) {
    return mmu_alloc_impl(pages, kind, AllocateMaxAddress, out_phys, max_addr);
}

EFI_STATUS mmu_register_image(EFI_PHYSICAL_ADDRESS phys, UINTN pages) {
    if (g_image_n >= MMU_IMAGE_MAX) {
        BXLOG(L"image registry full");
        return EFI_OUT_OF_RESOURCES;
    }
    g_image[g_image_n].phys  = phys;
    g_image[g_image_n].pages = pages;
    g_image[g_image_n].kind  = PK_FIRMWARE_TEMP;
    g_image_n++;

    BXLOG(L"register image %u pages at 0x%lx (identity only, not in KSEG0)",
          (UINT32)pages, (UINT64)phys);
    return EFI_SUCCESS;
}

/*============================================================================
 * Page-table + GDT/IDT layout
 *
 * Identity mapping: every phys page in mmu_alloc'd regions + the loader
 *                   image + the current stack, so the CR3 swap is
 *                   survivable. Kernel tears these PDEs down in
 *                   MiInitMachineDependent anyway, so "spurious" entries
 *                   are harmless.
 * KSEG0 mirror:     virt 0x80000000.. maps only pages from mmu_alloc
 *                   (loader image / stack intentionally NOT mirrored —
 *                   those are boot-transient). Kernel references only
 *                   its own allocations via KSEG0.
 * Separate PTs:     identity and KSEG0 use distinct PT pages so kernel
 *                   edits on KSEG0 PTs don't bleed into the identity
 *                   view during teardown.
 * Self-map:         PD[768] = PD itself (kernel's MiGetPteAddress).
 * HAL page:         PD[1023] = hal_pt.
 *                     hal_pt[511] = PCR       (KIP0PCRADDRESS  = 0xFFDFF000)
 *                     hal_pt[496] = SharedUD  (KI_USER_SHARED_DATA = 0xFFDF0000)
 *
 * PT pool sized on demand: walk the registries, count unique PDE slots,
 * allocate exactly that many PTs + 1 HAL. Dynamic sizing replaces the
 * old blanket "identity-map 0..IDENTITY_MB" preallocation which would
 * break the moment UEFI placed the loader image above that cutoff.
 *===========================================================================*/

#define PAGE_PRESENT      0x001u
#define PAGE_RW           0x002u
#define PAGE_USER         0x004u

/* KGDT selectors (mirror of boot/entry.S; see ks386.inc). */
#define KGDT_R0_CODE      0x08
#define KGDT_R0_DATA      0x10
#define KGDT_R3_CODE      0x18
#define KGDT_R3_DATA      0x20
#define KGDT_TSS          0x28
#define KGDT_R0_PCR       0x30
#define KGDT_R3_TEB       0x38
#define KGDT_GDT_ALIAS    0x70

/* Kernel expects PCR/SharedUserData at these fixed virtual addresses. */
#define KIP0PCR_VA        0xFFDFF000u
#define KI_USER_SHARED_VA 0xFFDF0000u

static EFI_PHYSICAL_ADDRESS g_phys_pd        = 0;
static EFI_PHYSICAL_ADDRESS g_phys_pts       = 0;  /* base of PT pool */
static UINTN                g_pt_pool_pages  = 0;  /* sized by mmu_alloc_pt_pool */
static EFI_PHYSICAL_ADDRESS g_phys_pcr       = 0;
static EFI_PHYSICAL_ADDRESS g_phys_sud       = 0;
static EFI_PHYSICAL_ADDRESS g_phys_tss       = 0;
static EFI_PHYSICAL_ADDRESS g_phys_idlestack = 0;
static EFI_PHYSICAL_ADDRESS g_phys_gdt       = 0;
static EFI_PHYSICAL_ADDRESS g_phys_idt       = 0;

/* Accessor exported for loaderblock (needs to know the idle-stack top). */
EFI_PHYSICAL_ADDRESS mmu_idle_stack_base(void)   { return g_phys_idlestack; }
EFI_PHYSICAL_ADDRESS mmu_tss_base(void)          { return g_phys_tss; }
EFI_PHYSICAL_ADDRESS mmu_pd_base(void)           { return g_phys_pd; }

/* Top of the idle stack as a KSEG0 virtual address. Used by handoff.S
 * so the kernel runs on a stack that survives the low-2GB unmap in
 * MiInitMachineDependent. */
unsigned long mmu_handoff_stack_top(void) {
    return (unsigned long)(KSEG0_BASE | (g_phys_idlestack + (4 << 12)));
}

EFI_STATUS mmu_alloc_reserved(void) {
    EFI_STATUS s;
    /* All structures reachable via KSEG0 at runtime (PD, PTs, TSS, PCR,
     * GDT, IDT, idle stack) must live in phys 0..16 MiB so their KSEG0
     * aliases fall within PDE[512..515] — the only KSEG0 PDEs NT 3.5
     * copies into a new process's page directory (PROCSUP.C:52,297). */
    const EFI_PHYSICAL_ADDRESS LOW16M = 0x01000000;

    s = mmu_alloc_below(1, PK_MEMORY_DATA, LOW16M, &g_phys_pd);       if (EFI_ERROR(s)) return s;
    s = mmu_alloc_below(1, PK_PCR,         LOW16M, &g_phys_pcr);      if (EFI_ERROR(s)) return s;
    s = mmu_alloc_below(1, PK_PCR,         LOW16M, &g_phys_sud);      if (EFI_ERROR(s)) return s;
    /* KTSS is ~8364 bytes: 0x68 header + KIIO_ACCESS_MAP (32-byte
     * DirectionMap + 8196-byte IoMap) + 32-byte IntDirectionMap. Kernel's
     * KiInitializeTSS (ntoskrnl .text ~0x801b2279) fills the bitmap with
     * `rep stos` of 0x801 dwords starting at TSS+0x88, which overflows
     * any allocation smaller than 3 pages. 2 pages = silent corruption
     * of whatever is in phys-adjacent memory (fastfat headers, in our case,
     * which then makes PsLoadedModule scan STOP 0x1E on the driver). */
    s = mmu_alloc_below(3, PK_MEMORY_DATA, LOW16M, &g_phys_tss);      if (EFI_ERROR(s)) return s;
    s = mmu_alloc_below(4, PK_MEMORY_DATA, LOW16M, &g_phys_idlestack);if (EFI_ERROR(s)) return s;
    s = mmu_alloc_below(1, PK_MEMORY_DATA, LOW16M, &g_phys_gdt);     if (EFI_ERROR(s)) return s;
    s = mmu_alloc_below(1, PK_MEMORY_DATA, LOW16M, &g_phys_idt);     if (EFI_ERROR(s)) return s;

    BXLOG(L"reserved core pages below 16 MiB: "
          L"PD=0x%lx PCR=0x%lx SUD=0x%lx TSS=0x%lx stack=0x%lx GDT=0x%lx IDT=0x%lx",
          (UINT64)g_phys_pd, (UINT64)g_phys_pcr, (UINT64)g_phys_sud,
          (UINT64)g_phys_tss, (UINT64)g_phys_idlestack,
          (UINT64)g_phys_gdt, (UINT64)g_phys_idt);
    return EFI_SUCCESS;
}

/*
 * Walk both registries, count unique 4 MB-aligned PDE slots needed
 * for the identity map (all entries) and KSEG0 mirror (g_reg only).
 * Each unique slot consumes one PT. Plus one HAL PT at PDE[1023].
 */
static UINTN count_pt_slots(UINT8 id_slot[512], UINT8 ks_slot[512]) {
    for (UINTN i = 0; i < 512; i++) { id_slot[i] = 0; ks_slot[i] = 0; }

    for (UINTN r = 0; r < g_reg_n; r++) {
        UINT64 lo = g_reg[r].phys;
        UINT64 hi = lo + ((UINT64)g_reg[r].pages << 12) - 1;
        UINTN  start = (UINTN)(lo >> 22);
        UINTN  end   = (UINTN)(hi >> 22);
        for (UINTN i = start; i <= end && i < 512; i++) {
            id_slot[i] = 1;
            ks_slot[i] = 1;
        }
    }
    for (UINTN r = 0; r < g_image_n; r++) {
        UINT64 lo = g_image[r].phys;
        UINT64 hi = lo + ((UINT64)g_image[r].pages << 12) - 1;
        UINTN  start = (UINTN)(lo >> 22);
        UINTN  end   = (UINTN)(hi >> 22);
        for (UINTN i = start; i <= end && i < 512; i++) {
            id_slot[i] = 1;    /* identity only */
        }
    }

    UINTN id_count = 0, ks_count = 0;
    for (int i = 0; i < 512; i++) {
        if (id_slot[i]) id_count++;
        if (ks_slot[i]) ks_count++;
    }
    return id_count + ks_count + 1;   /* +1 for HAL PT */
}

EFI_STATUS mmu_alloc_pt_pool(void) {
    const EFI_PHYSICAL_ADDRESS LOW16M = 0x01000000;
    UINT8 id_slot[512], ks_slot[512];
    UINTN total = count_pt_slots(id_slot, ks_slot);

    EFI_STATUS s = mmu_alloc_below(total, PK_MEMORY_DATA, LOW16M, &g_phys_pts);
    if (EFI_ERROR(s)) return s;
    g_pt_pool_pages = total;

    BXLOG(L"PT pool: %u pages (identity+kseg0+hal)", (UINT32)total);
    return EFI_SUCCESS;
}

/*----------------------------------------------------------------------------
 * GDT builder — mirror of boot/entry.S layout, filled in at runtime so we
 * can point TSS/PCR/GDT_ALIAS at the KSEG0 bases of our allocated pages.
 *---------------------------------------------------------------------------*/

typedef struct __attribute__((packed)) {
    UINT16 limit_low;
    UINT16 base_low;
    UINT8  base_mid;
    UINT8  access;
    UINT8  limit_hi_flags;  /* low 4 bits = limit[19:16], high 4 = flags */
    UINT8  base_hi;
} gdt_entry_t;

static void set_gdt(gdt_entry_t *e, UINT32 base, UINT32 limit,
                    UINT8 access, UINT8 flags) {
    e->limit_low      = limit & 0xFFFF;
    e->base_low       = base  & 0xFFFF;
    e->base_mid       = (base >> 16) & 0xFF;
    e->access         = access;
    e->limit_hi_flags = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    e->base_hi        = (base >> 24) & 0xFF;
}

static void build_gdt(gdt_entry_t *gdt, UINT32 gdt_kseg0,
                      UINT32 tss_kseg0, UINT32 tss_limit) {
    /* 32 entries = 256 bytes, matches boot/entry.S. */
    for (int i = 0; i < 32; i++) set_gdt(&gdt[i], 0, 0, 0, 0);

    /* Ring-0 code/data, flat 4 GB. */
    set_gdt(&gdt[KGDT_R0_CODE / 8], 0, 0xFFFFF, 0x9A, 0xC0);
    set_gdt(&gdt[KGDT_R0_DATA / 8], 0, 0xFFFFF, 0x92, 0xC0);

    /* Ring-3 code/data. */
    set_gdt(&gdt[KGDT_R3_CODE / 8], 0, 0xFFFFF, 0xFA, 0xC0);
    set_gdt(&gdt[KGDT_R3_DATA / 8], 0, 0xFFFFF, 0xF2, 0xC0);

    /* KGDT_TSS at 0x28 — 32-bit TSS available, DPL=0. */
    set_gdt(&gdt[KGDT_TSS / 8], tss_kseg0, tss_limit, 0x89, 0x00);

    /* KGDT_R0_PCR at 0x30 — ring-0 data, base = KIP0PCR_VA. */
    set_gdt(&gdt[KGDT_R0_PCR / 8], KIP0PCR_VA, 0xFFF, 0x92, 0xC0);

    /* KGDT_R3_TEB at 0x38 — ring-3 data for TEB (base set per-thread by
     * SwapContext). Access=0xF3 / flags=0x40 match OSLOADER SUDATA.ASM. */
    set_gdt(&gdt[KGDT_R3_TEB / 8], 0, 0xFFF, 0xF3, 0x40);

    /* KGDT_GDT_ALIAS at 0x70 — describes the GDT itself. */
    set_gdt(&gdt[KGDT_GDT_ALIAS / 8], gdt_kseg0, 32 * 8 - 1, 0x92, 0x40);
}

static void build_tss(void *tss) {
    UINT8 *p = (UINT8 *)tss;
    for (unsigned i = 0; i < 3 * 4096; i++) p[i] = 0;
    /* Classic KTSS: 104 bytes + optional I/O bitmap. I/O map base = size
     * of TSS => no I/O bitmap present. */
    *(UINT16 *)(p + 0x66) = 0x68;   /* IoMapBase = 104 */
}

/*----------------------------------------------------------------------------
 * Page-directory + page-table construction.
 *
 * We precompute everything into the pre-allocated PD/PT pages here, then
 * a single `mov cr3` (in mmu_switch_cr3) activates them.
 *---------------------------------------------------------------------------*/

static void build_page_tables(void) {
    UINT32 *pd = (UINT32 *)(UINTN)g_phys_pd;
    int kseg0_pd_idx = (int)(KSEG0_BASE >> 22);

    /* Zero PD + full PT pool. */
    for (int i = 0; i < 1024; i++) pd[i] = 0;
    for (UINTN i = 0; i < g_pt_pool_pages * 1024; i++) {
        ((UINT32 *)(UINTN)g_phys_pts)[i] = 0;
    }

    /* Recompute slot bitmaps — we deliberately re-walk the registries
     * rather than cache the count_pt_slots() output. Layout decisions
     * all derive from the same pass, so any inconsistency between
     * sizing and layout would manifest as a PT-pool over-run. */
    UINT8 id_slot[512], ks_slot[512];
    (void)count_pt_slots(id_slot, ks_slot);

    /* Assign PTs from the pool: one per identity slot, one per KSEG0
     * slot, one for HAL. Track slot → PT phys so PTE writes hit the
     * right table. */
    UINT32 id_pt_phys[512] = {0};
    UINT32 ks_pt_phys[512] = {0};
    UINT32 pt_cursor = (UINT32)g_phys_pts;
    for (int i = 0; i < 512; i++) {
        if (id_slot[i]) { id_pt_phys[i] = pt_cursor; pt_cursor += 4096; }
    }
    for (int i = 0; i < 512; i++) {
        if (ks_slot[i]) { ks_pt_phys[i] = pt_cursor; pt_cursor += 4096; }
    }
    UINT32 hal_pt_phys = pt_cursor;

    /* Install per-slot PDEs — identity in the low half, KSEG0 mirror in
     * the upper half (KSEG0_BASE >> 22 = 512 on i386). Only slots with
     * actual registered pages are present; every other PDE stays zero. */
    for (int i = 0; i < 512; i++) {
        if (id_pt_phys[i]) pd[i] = id_pt_phys[i] | PAGE_PRESENT | PAGE_RW;
        if (ks_pt_phys[i]) pd[kseg0_pd_idx + i] = ks_pt_phys[i] | PAGE_PRESENT | PAGE_RW;
    }

    /* Self-map PDE[768] — kernel's MiGetPteAddress walks here. */
    pd[768] = (UINT32)(UINTN)g_phys_pd | PAGE_PRESENT | PAGE_RW;

    /* HAL page table at PDE[1023]. */
    pd[1023] = hal_pt_phys | PAGE_PRESENT | PAGE_RW;

    /* Populate identity PTEs from mmu_alloc'd entries (kernel, HAL,
     * drivers, PCR, TSS, stacks, GDT, IDT, LPB arena, PT pool itself,
     * registry, NLS) AND from the image-only list (loader image, stack).
     * KSEG0 PTEs only see the former — boot-transient pages aren't
     * mirrored into kernel virt. NULL phys (page 0) is excluded from
     * identity so NULL-pointer deref still faults. */
#define POPULATE_IDENTITY(entry)                                           \
    do {                                                                   \
        UINT32 base = (UINT32)(entry)->phys;                               \
        UINTN  pages = (entry)->pages;                                     \
        for (UINTN p = 0; p < pages; p++) {                                \
            UINT32 phys = base + (UINT32)(p << 12);                        \
            if (phys == 0) continue;                                       \
            UINT32 pdi = phys >> 22;                                       \
            if (pdi >= 512 || id_pt_phys[pdi] == 0) continue;              \
            UINT32 pti = (phys >> 12) & 0x3FF;                             \
            ((UINT32 *)(UINTN)id_pt_phys[pdi])[pti] =                      \
                phys | PAGE_PRESENT | PAGE_RW;                             \
        }                                                                  \
    } while (0)

    for (UINTN r = 0; r < g_reg_n; r++) {
        POPULATE_IDENTITY(&g_reg[r]);
        /* KSEG0 mirror: mmu_alloc entries only */
        UINT32 base = (UINT32)g_reg[r].phys;
        UINTN  pages = g_reg[r].pages;
        for (UINTN p = 0; p < pages; p++) {
            UINT32 phys = base + (UINT32)(p << 12);
            UINT32 pdi = phys >> 22;
            if (pdi >= 512 || ks_pt_phys[pdi] == 0) continue;
            UINT32 pti = (phys >> 12) & 0x3FF;
            ((UINT32 *)(UINTN)ks_pt_phys[pdi])[pti] =
                phys | PAGE_PRESENT | PAGE_RW;
        }
    }
    for (UINTN r = 0; r < g_image_n; r++) POPULATE_IDENTITY(&g_image[r]);

#undef POPULATE_IDENTITY

    /* HAL PT: PCR at VA 0xFFDFF000 (PTE 511), SharedUserData at 0xFFDF0000
     * (PTE 496). PTE index = (VA - 0xFFC00000) >> 12. */
    {
        UINT32 *hal_pt = (UINT32 *)(UINTN)hal_pt_phys;
        hal_pt[511] = (UINT32)(UINTN)g_phys_pcr | PAGE_PRESENT | PAGE_RW;
        hal_pt[496] = (UINT32)(UINTN)g_phys_sud | PAGE_PRESENT | PAGE_RW;
    }

    /* PCR + SharedUserData pages start life zero. */
    {
        UINT8 *p;
        p = (UINT8 *)(UINTN)g_phys_pcr; for (int i = 0; i < 4096; i++) p[i] = 0;
        p = (UINT8 *)(UINTN)g_phys_sud; for (int i = 0; i < 4096; i++) p[i] = 0;
    }
}

/*----------------------------------------------------------------------------
 * mmu_build_and_activate
 *
 * Runs post-ExitBootServices in long mode (64-bit UEFI). We're NOT
 * switching to our NT-facing 32-bit paging here — that's the job of
 * the 64→32 transition stub in transition.S. This function's job is
 * purely to materialize everything in memory that the stub will later
 * install: the 32-bit page directory + page tables, the 32-bit GDT,
 * a zeroed IDT, and the TSS.
 *
 * The transition stub, running post-mode-drop in 32-bit protected
 * mode, does the actual lgdt / lidt / mov-cr3 / segment reloads / ltr
 * sequence using these pre-built structures.
 *---------------------------------------------------------------------------*/

void mmu_build_and_activate(void) {
    UINT32 gdt_kseg0 = (UINT32)(KSEG0_BASE | g_phys_gdt);
    UINT32 tss_kseg0 = (UINT32)(KSEG0_BASE | g_phys_tss);
    UINT32 tss_limit = 3 * 4096 - 1;

    /* UEFI leaves interrupts enabled. Disable now — our IDT is zero
     * and the transition stub runs with IF=0 through the mode drop.
     * HalpInitializePICs re-enables once the kernel's IDT is filled. */
    __asm__ volatile("cli");

    build_page_tables();
    build_gdt((gdt_entry_t *)(UINTN)g_phys_gdt, gdt_kseg0, tss_kseg0, tss_limit);

    /* IDT: 256 zeroed entries. Kernel's KiSwapIDT fills them in. */
    {
        UINT8 *p = (UINT8 *)(UINTN)g_phys_idt;
        for (int i = 0; i < 4096; i++) p[i] = 0;
    }

    build_tss((void *)(UINTN)g_phys_tss);
}

/* Accessors for transition.S: it needs the phys bases of the PD, GDT,
 * and IDT to install them post-mode-drop. */
EFI_PHYSICAL_ADDRESS mmu_gdt_base(void) { return g_phys_gdt; }
EFI_PHYSICAL_ADDRESS mmu_idt_base(void) { return g_phys_idt; }
