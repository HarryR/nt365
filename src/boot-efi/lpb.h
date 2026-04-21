/*
 * LOADER_PARAMETER_BLOCK construction.
 *
 * Thin builder for the on-wire `LOADER_PARAMETER_BLOCK` that NT's
 * `ExpInitializeExecutive` reads at Phase-0 boot. This module owns only
 * the LPB struct itself + the lists/strings it references directly:
 *
 *   LoadOrderList      : LDR_DATA_TABLE_ENTRY for ntoskrnl + hal + drivers
 *   BootDriverList     : BOOT_DRIVER_LIST_ENTRY for each driver
 *   MemoryDescriptorList: MEMORY_ALLOCATION_DESCRIPTOR chain from memmap
 *   NlsData            : pointers to the three NLS blobs
 *   RegistryBase/Length: from the registry allocation entry
 *   ArcDiskInformation : MBR signature/checksum for the boot disk
 *   Arc* strings       : "multi(0)disk(0)rdisk(0)partition(1)" etc.
 *   I386.{CommonDataArea, MachineType}
 *   KernelStack, Thread : idle stack top / placeholder
 *
 * The hardware-inventory tree (`CONFIGURATION_COMPONENT_DATA` rooted at
 * `ConfigurationRoot`) is built by the separate `hwtree` module; main.c
 * calls `hwtree_build()` and then `lpb_set_configuration_root()`.
 *
 * All arena allocation goes through the `arena` module — lpb doesn't
 * own the arena, it's a singleton shared with hwtree and any future
 * producers.
 */
#ifndef _BOOT_EFI_LPB_H_
#define _BOOT_EFI_LPB_H_

#include <efi.h>
#include "pe.h"

/* Arena sizing that covers the LPB, its strings, LDR + boot-driver
 * entries, memory descriptors, and the hwtree nodes/blobs. Current worst
 * case (headless profile, max boot drivers + 4 UARTs + memmap) fits
 * comfortably in 4 pages. Main.c passes this to arena_init. */
#define LPB_ARENA_PAGES 4

/* Must be called before `lpb_build` so NlsData pointers can be written.
 * The three code-page tables MUST live in one contiguous block —
 * Phase1Initialization (NTOS/INIT/INIT.C:392) computes
 * UnicodeCaseTableData as an offset from AnsiCodePageData. */
void lpb_set_nls(EFI_PHYSICAL_ADDRESS base_phys,
                 UINTN ansi_off, UINTN oem_off, UINTN uni_off);

/* Must be called before `lpb_build`. Carries the MBR-derived identity
 * needed to satisfy IopCreateArcNames (NTOS/IO/IOINIT.C:1355):
 *   mbr_signature : DWORD at MBR offset 0x1B8.
 *   mbr_checksum  : two's complement of the sum of the first 128 DWORDs
 *                   of the MBR (kernel adds its own sum and expects 0). */
void lpb_set_boot_disk(UINT32 mbr_signature, UINT32 mbr_checksum);

/* Store the ConfigurationRoot KSEG0 VA. Call with the return value of
 * hwtree_build() before `lpb_build`. */
void lpb_set_configuration_root(UINT32 root_kseg0);

/* Build the LPB itself. Caller must have called arena_init first.
 * Allocates the LPB, the ARC disk info block, the path strings, and the
 * NLS pointer block; initialises empty list heads for LoadOrder /
 * BootDriver / MemoryDescriptor. The ConfigurationRoot + module lists
 * are wired separately via lpb_set_configuration_root (latched before
 * or after build) and lpb_wire_modules (after staging). */
EFI_STATUS lpb_build(void);

/* After lpb_build + all images staged, wire LoadOrderList and
 * BootDriverList with KSEG0 pointers into each pe_image_t. Kernel + hal
 * are required; drivers may be zero-count. */
EFI_STATUS lpb_wire_modules(const pe_image_t *kernel,
                            const pe_image_t *hal,
                            const pe_image_t *boot_drivers,
                            UINTN n_boot_drivers);

/* After memmap_capture + wire_modules: link translated descriptors into
 * LoaderBlock.MemoryDescriptorListHead. No UEFI calls; arena-only writes. */
EFI_STATUS lpb_link_memmap(void);

/* Returns the KSEG0 VA of the LOADER_PARAMETER_BLOCK for handoff(). */
unsigned long lpb_handoff_ptr(void);

/* Returns the KSEG0 VA of KiSystemStartup (from the kernel pe_image_t). */
unsigned long lpb_kernel_entry(void);

#endif
