/*
 * Page-table construction + physical page allocator with NT-type intent.
 *
 *   KSEG0 plan: 0x80000000 → phys 0x00000000, 1-to-1 for the first
 *   KSEG0_SIZE bytes. Every page we AllocatePages from UEFI at phys P
 *   becomes KSEG0_BASE|P at handoff; internal pointers in NT structures
 *   are written in KSEG0 space directly — no fixup pass needed.
 *
 *   All reserved-page allocation funnels through mmu_alloc() so the
 *   registry captures every allocation's intended NT memory type.
 *   memmap.c consults the registry to emit accurate NT descriptors.
 */
#ifndef _BOOT_EFI_MMU_H_
#define _BOOT_EFI_MMU_H_

#include <efi.h>

#define KSEG0_BASE 0x80000000UL
#define KSEG0(phys) ((void *)(unsigned long)((unsigned long)(phys) | KSEG0_BASE))

/*
 * NT memory type intent for each allocation we make.  Maps 1:1 to
 * MEMORY_TYPE values (defined in ARC loader headers); we keep a local
 * enum so mmu/fs code doesn't have to drag NT headers in.  The mapping
 * is applied in lpb.c when it emits MEMORY_ALLOCATION_DESCRIPTORs.
 */
typedef enum {
    PK_FREE,                /* LoaderFree — unused, caller shouldn't use this */
    PK_KERNEL_IMAGE,        /* LoaderSystemCode — ntoskrnl.exe */
    PK_HAL_IMAGE,           /* LoaderHalCode — hal.dll */
    PK_BOOT_DRIVER,         /* LoaderBootDriver — *.sys */
    PK_REGISTRY,            /* LoaderRegistryData — hive */
    PK_NLS,                 /* LoaderNlsData — c_*.nls, l_*.nls */
    PK_PCR,                 /* LoaderStartupPcrPage — PCR + shared user data */
    PK_MEMORY_DATA,         /* LoaderMemoryData — PD/PT/TSS/LoaderBlock/stacks */
    PK_FIRMWARE_PERM,       /* LoaderFirmwarePermanent — ACPI/runtime svc/MMIO */
    PK_FIRMWARE_TEMP,       /* LoaderFirmwareTemporary — reclaimable */
} PageKind;

typedef struct {
    EFI_PHYSICAL_ADDRESS phys;
    UINTN                pages;
    PageKind             kind;
} AllocEntry;

/* Allocate `pages` contiguous pages; records the allocation in the
 * registry. phys is returned; page contents are NOT zeroed — caller
 * owns that. Fails if pages can't be allocated OR registry is full. */
EFI_STATUS mmu_alloc(UINTN pages, PageKind kind,
                     EFI_PHYSICAL_ADDRESS *out_phys);

/* Allocate at a specific physical address (UEFI AllocateAddress mode).
 * Required for /FIXED PE images that can't be rebased (e.g. ntoskrnl).
 * Returns EFI_NOT_FOUND if the range is occupied. */
EFI_STATUS mmu_alloc_at(UINTN pages, PageKind kind,
                        EFI_PHYSICAL_ADDRESS preferred,
                        EFI_PHYSICAL_ADDRESS *out_phys);

/* Allocate `pages` at the highest available physical address below
 * `max_addr` (UEFI AllocateMaxAddress). Used for data that must live in
 * the first 16 MiB of phys so KSEG0 access survives a CR3 switch to a
 * new process — NT 3.5's MmCreateProcessAddressSpace only copies PDEs
 * for virt 0x80000000..0x80FFFFFF (see NTOS/MM/PROCSUP.C:52). */
EFI_STATUS mmu_alloc_below(UINTN pages, PageKind kind,
                           EFI_PHYSICAL_ADDRESS max_addr,
                           EFI_PHYSICAL_ADDRESS *out_phys);

/* Read-only registry access for memmap.c. */
UINTN             mmu_registry_count(void);
const AllocEntry *mmu_registry_entry(UINTN i);

/* Pre-exit: reserve core pages — PD, PCR, SUD, TSS, idle stack, GDT,
 * IDT. PT pool is sized separately via mmu_alloc_pt_pool() once all
 * other registrations are in, because the PT count depends on the
 * phys ranges the identity map needs to cover. */
EFI_STATUS mmu_alloc_reserved(void);

/* Register a phys range we didn't allocate (e.g. the UEFI-placed
 * loader image, our current stack) that must be identity-mapped in
 * the 32-bit PD so we survive the CR3 swap. No UEFI AllocatePages call
 * is made. The range does NOT contribute to KSEG0 mirror or memmap
 * NT-type overlay — those are reserved for mmu_alloc'd entries. */
EFI_STATUS mmu_register_image(EFI_PHYSICAL_ADDRESS phys, UINTN pages);

/* Pre-exit: allocate the exact-sized PT pool after all identity + KSEG0
 * candidates are registered. Walks both registries, counts unique PDE
 * slots, allocates that many PTs (+1 for the HAL PT at PDE[1023]) below
 * 16 MiB so the KSEG0 aliases land in PDE[512..515]. Must be the last
 * UEFI allocation before ExitBootServices. */
EFI_STATUS mmu_alloc_pt_pool(void);

/* Post-exit: build page-table content + activate paging. Runs with no
 * UEFI services available. */
void mmu_build_and_activate(void);

/* Accessors for lpb.c so it can write KSEG0 pointers into
 * LoaderBlock.KernelStack, .Thread, etc. Also used by transition.S to
 * install our 32-bit descriptor state post-mode-drop. */
EFI_PHYSICAL_ADDRESS mmu_idle_stack_base(void);
EFI_PHYSICAL_ADDRESS mmu_tss_base(void);
EFI_PHYSICAL_ADDRESS mmu_pd_base(void);
EFI_PHYSICAL_ADDRESS mmu_gdt_base(void);
EFI_PHYSICAL_ADDRESS mmu_idt_base(void);

/* Top of the idle stack as a KSEG0 virtual address — used by handoff.S
 * so the kernel runs on a stack that survives the low-2GB unmap. */
unsigned long mmu_handoff_stack_top(void);

#endif
