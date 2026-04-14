#include "mmu.h"
#include "com1.h"
#include <efilib.h>

/*
 * Pre-ExitBootServices we AllocatePages() via UEFI. Post-exit, UEFI is
 * gone; all allocation must be done by now. The registry lets other
 * modules (fs, loaderblock, memmap) find out what we grabbed and why.
 */

#define MMU_REGISTRY_MAX 64

static AllocEntry g_reg[MMU_REGISTRY_MAX];
static UINTN      g_reg_n = 0;

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
        com1_puts("[mmu] registry full\n");
        return EFI_OUT_OF_RESOURCES;
    }

    status = uefi_call_wrapper(BS->AllocatePages, 4,
                               mode, EfiLoaderData,
                               pages, &phys);
    if (EFI_ERROR(status)) {
        com1_puts("[mmu] AllocatePages(");
        com1_put_dec((unsigned long)pages);
        com1_puts(") for ");
        com1_puts(kind_name(kind));
        com1_puts(" failed: ");
        com1_put_hex((unsigned long)status);
        com1_puts("\n");
        return status;
    }

    g_reg[g_reg_n].phys  = phys;
    g_reg[g_reg_n].pages = pages;
    g_reg[g_reg_n].kind  = kind;
    g_reg_n++;

    com1_puts("[mmu] alloc ");
    com1_put_dec((unsigned long)pages);
    com1_puts(" pages at ");
    com1_put_hex((unsigned long)phys);
    com1_puts(" for ");
    com1_puts(kind_name(kind));
    com1_puts("\n");

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

/*============================================================================
 * Page-table + GDT/IDT layout
 *
 * Identity mapping: phys 0..IDENTITY_MB covers our loader code/stack/allocs
 *                   so the CR3 swap is survivable.
 * KSEG0 mirror:     virt 0x80000000.. maps the same phys range.
 * Shared PTs:       identity and KSEG0 point at the *same* PT pages.
 * Self-map:         PD[768] = PD itself (kernel's MiGetPteAddress).
 * HAL page:         PD[1023] = hal_pt.
 *                     hal_pt[511] = PCR       (KIP0PCRADDRESS  = 0xFFDFF000)
 *                     hal_pt[496] = SharedUD  (KI_USER_SHARED_DATA = 0xFFDF0000)
 *===========================================================================*/

#define IDENTITY_MB       256
#define PTS_PER_ALIAS     (IDENTITY_MB / 4)   /* each PT = 4 MB, per alias */
/* Two aliases (identity + KSEG0) get their OWN PT pages. Matches the
 * multiboot loader; prevents kernel self-map edits on KSEG0 PTs from
 * bleeding into identity view. +1 for the HAL PT at PDE[1023]. */
#define PTS_TOTAL         (PTS_PER_ALIAS * 2 + 1)
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
static EFI_PHYSICAL_ADDRESS g_phys_pts       = 0;  /* PTS_FOR_RANGE + 1 (HAL) pages */
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
    s = mmu_alloc_below(PTS_TOTAL, PK_MEMORY_DATA, LOW16M, &g_phys_pts);
    if (EFI_ERROR(s)) return s;

    com1_puts("[mmu] reserved core pages (all < 16 MiB)\n");
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
    UINT32 *pd  = (UINT32 *)(UINTN)g_phys_pd;
    UINT32 *pts = (UINT32 *)(UINTN)g_phys_pts;
    UINT32 *id_pts   = pts;                            /* identity PTs */
    UINT32 *kseg_pts = pts + PTS_PER_ALIAS * 1024;     /* KSEG0 PTs */
    UINT32 *hal_pt   = pts + 2 * PTS_PER_ALIAS * 1024; /* HAL PT */
    int kseg0_pd_idx = (int)(KSEG0_BASE >> 22);

    /* Zero PD + full PT pool. */
    for (int i = 0; i < 1024; i++) pd[i] = 0;
    for (int i = 0; i < PTS_TOTAL * 1024; i++) pts[i] = 0;

    /* Identity PTs: blanket-map 0..IDENTITY_MB (minus page 0). Only needed
     * briefly so we survive the CR3 swap; kernel tears these PDEs down in
     * MiInitMachineDependent. */
    for (int pt_idx = 0; pt_idx < PTS_PER_ALIAS; pt_idx++) {
        UINT32 *id_pt = id_pts + pt_idx * 1024;
        for (int j = 0; j < 1024; j++) {
            UINT32 page_phys = ((UINT32)pt_idx << 22) | ((UINT32)j << 12);
            if (pt_idx == 0 && j == 0) {
                id_pt[j] = 0;  /* NULL-pointer detection */
            } else {
                id_pt[j] = page_phys | PAGE_PRESENT | PAGE_RW;
            }
        }
    }

    /* KSEG0 PTs: map ONLY pages we registered (kernel, HAL, drivers, PCR,
     * TSS, stacks, GDT, IDT, LPB arena, PT pool itself, registry, NLS).
     * Every other phys page stays unmapped in KSEG0 so the kernel's PDE
     * walk (NT MM/I386/INIT386.C:457-498) does NOT set RefCount=1 on free
     * RAM — leaving those PFN entries at 0 so the descriptor walk (:572)
     * adds LoaderFree/LoaderFirmwareTemporary pages to the free list.
     * Blanket-mapping here was the PFN_LIST_CORRUPT root cause. */
    for (UINTN r = 0; r < g_reg_n; r++) {
        UINT32 base = (UINT32)g_reg[r].phys;
        UINT32 pages = (UINT32)g_reg[r].pages;
        for (UINT32 p = 0; p < pages; p++) {
            UINT32 page_phys = base + (p << 12);
            UINT32 pt_idx = page_phys >> 22;
            UINT32 pte_idx = (page_phys >> 12) & 0x3FF;
            if (pt_idx >= PTS_PER_ALIAS) continue;   /* out of KSEG0 window */
            kseg_pts[pt_idx * 1024 + pte_idx] =
                page_phys | PAGE_PRESENT | PAGE_RW;
        }
    }

    /* PDEs 0..PTS_PER_ALIAS-1: identity mapping (lives only long enough
     * for us to survive the CR3 load + jmp into KSEG0). */
    for (int i = 0; i < PTS_PER_ALIAS; i++) {
        UINT32 pt_phys = (UINT32)(UINTN)(id_pts + i * 1024);
        pd[i] = pt_phys | PAGE_PRESENT | PAGE_RW;
    }

    /* PDEs KSEG0..KSEG0+PTS_PER_ALIAS-1: permanent KSEG0 mirror. */
    for (int i = 0; i < PTS_PER_ALIAS; i++) {
        UINT32 pt_phys = (UINT32)(UINTN)(kseg_pts + i * 1024);
        pd[kseg0_pd_idx + i] = pt_phys | PAGE_PRESENT | PAGE_RW;
    }

    /* Self-map PDE[768] — kernel's MiGetPteAddress walks here. */
    pd[768] = (UINT32)(UINTN)g_phys_pd | PAGE_PRESENT | PAGE_RW;

    /* HAL page table at PDE[1023]; uses the dedicated PT we reserved. */
    pd[1023] = (UINT32)(UINTN)hal_pt | PAGE_PRESENT | PAGE_RW;

    /* HAL PT: PCR at VA 0xFFDFF000 (PTE 511), SharedUserData at 0xFFDF0000
     * (PTE 496). PTE index = (VA - 0xFFC00000) >> 12. */
    hal_pt[511] = (UINT32)(UINTN)g_phys_pcr | PAGE_PRESENT | PAGE_RW;
    hal_pt[496] = (UINT32)(UINTN)g_phys_sud | PAGE_PRESENT | PAGE_RW;

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
 * Runs post-ExitBootServices. No UEFI services; we stand entirely on
 * our AllocatePages'd pages. Build the page tables, build GDT/IDT in
 * their dedicated pages, then lgdt/lidt/mov-cr3 to switch to our world.
 *
 * Paging is already on (UEFI's doing); we are SWITCHING tables via CR3.
 * Since our PD identity-maps the low IDENTITY_MB, execution continues at
 * the same linear address after CR3 is loaded. Code, stack, and all
 * allocated pages must live within this identity window — mmu_alloc on
 * EfiLoaderData satisfies that in practice for QEMU/OVMF.
 *---------------------------------------------------------------------------*/

void mmu_build_and_activate(void) {
    UINT32 gdt_kseg0 = (UINT32)(KSEG0_BASE | g_phys_gdt);
    UINT32 idt_kseg0 = (UINT32)(KSEG0_BASE | g_phys_idt);
    UINT32 tss_kseg0 = (UINT32)(KSEG0_BASE | g_phys_tss);
    /* TSS limit covers the full 3-page allocation (enough for NT's KTSS
     * with its I/O bitmap and interrupt redirection map). */
    UINT32 tss_limit = 3 * 4096 - 1;

    /* UEFI leaves interrupts enabled. Our IDT will be all zeros (non-
     * present gates) post-lidt, so any interrupt would triple-fault us.
     * HalpInitializePICs re-enables after the kernel's IDT is filled. */
    __asm__ volatile("cli");

    com1_puts("[mmu] building page tables\n");
    build_page_tables();

    com1_puts("[mmu] building GDT\n");
    build_gdt((gdt_entry_t *)(UINTN)g_phys_gdt, gdt_kseg0, tss_kseg0, tss_limit);

    /* IDT: 256 zeroed entries. Kernel's KiSwapIDT fills them in. */
    {
        UINT8 *p = (UINT8 *)(UINTN)g_phys_idt;
        for (int i = 0; i < 4096; i++) p[i] = 0;
    }

    com1_puts("[mmu] building TSS\n");
    build_tss((void *)(UINTN)g_phys_tss);

    /* OVMF-ia32 runs with paging DISABLED — flat 32-bit protected mode.
     * We load CR3 (harmless while PG=0), make sure CR4.PAE=0 (our PD is
     * 2-level, not PDPT), then flip CR0.PG=1 to activate paging.
     * Identity map covers current EIP so the fetch after CR0.PG survives. */
    {
        UINT32 cr0, cr4;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        com1_puts("[mmu] entry CR0=");
        com1_put_hex(cr0);
        com1_puts(" CR4=");
        com1_put_hex(cr4);
        com1_puts("\n");

        if (cr4 & (1u << 5)) {
            cr4 &= ~(1u << 5);
            __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
        }
        __asm__ volatile("mov %0, %%cr3"
                         : : "r"((UINT32)(UINTN)g_phys_pd) : "memory");
        cr0 |= (1u << 31);                             /* CR0.PG = 1 */
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
        com1_puts("[mmu] paging enabled\n");
    }

    /* GDT and IDT still accessed via phys (identity-mapped in our PD).
     * Kernel will eventually use the KSEG0 view via lgdt/lidt at its base;
     * we install with KSEG0 base here so when segments reload (via ltr etc.)
     * they target the canonical high-memory descriptors. */
    com1_puts("[mmu] installing GDT+IDT at KSEG0 bases\n");
    {
        struct __attribute__((packed)) {
            UINT16 limit;
            UINT32 base;
        } gdtr = { 32 * 8 - 1, gdt_kseg0 }, idtr = { 256 * 8 - 1, idt_kseg0 };

        /* Dump descriptor 1 (KGDT_R0_CODE) so we can verify via serial. */
        {
            UINT32 *d = (UINT32 *)(UINTN)(g_phys_gdt + 8);
            com1_puts("  GDT[1]=");
            com1_put_hex(d[1]);
            com1_puts(":");
            com1_put_hex(d[0]);
            com1_puts("\n");
        }
        /* And from the KSEG0 view — should read the same via paging. */
        {
            UINT32 *d = (UINT32 *)(UINTN)gdt_kseg0 + 2;  /* +8 bytes = +2 dwords */
            com1_puts("  via KSEG0 GDT[1]=");
            com1_put_hex(d[1]);
            com1_puts(":");
            com1_put_hex(d[0]);
            com1_puts("\n");
        }

        __asm__ volatile("lgdt %0" : : "m"(gdtr));
        __asm__ volatile("lidt %0" : : "m"(idtr));
    }

    /* Reload CS via push+lret. We avoid `ljmp $sel, $label` because the
     * immediate label address needs a PE base-relocation that gnu-efi's
     * linker script isn't guaranteed to emit for inline-asm immediates.
     * call/pop gets EIP at runtime; add (end-start) offset; push selector;
     * push target EIP; lret switches CS and jumps. */
    com1_puts("[mmu] switch CS\n");
    __asm__ volatile(
        "   call 1f\n"
        "1: popl %%eax\n"
        "   addl $(2f - 1b), %%eax\n"   /* %eax = addr of label 2 */
        "   pushl $0x08\n"              /* new CS = KGDT_R0_CODE */
        "   pushl %%eax\n"              /* new EIP */
        "   lretl\n"
        "2:\n"
        ::: "eax", "memory"
    );
    com1_puts("[mmu] ds/es/ss\n");
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        ::: "eax", "memory"
    );
    com1_puts("[mmu] fs=pcr\n");
    __asm__ volatile(
        "mov $0x30, %%ax\n"       /* KGDT_R0_PCR */
        "mov %%ax, %%fs\n"
        "xor %%ax, %%ax\n"
        "mov %%ax, %%gs\n"
        ::: "eax"
    );

    com1_puts("[mmu] ltr\n");
    __asm__ volatile(
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        ::: "eax"
    );

    com1_puts("[mmu] active\n");
}
