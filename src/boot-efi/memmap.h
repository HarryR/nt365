/*
 * UEFI memory map capture + NT translation.
 *
 * Two-phase flow, because ExitBootServices needs the MapKey from the
 * most recent GetMemoryMap, and any AllocatePool/AllocatePages between
 * GetMemoryMap and ExitBootServices invalidates the key:
 *
 *   pre-exit:  memmap_capture(&mapkey)    - record full UEFI map
 *              [do NOT allocate anything after this]
 *              BS->ExitBootServices(ImageHandle, mapkey)
 *   post-exit: memmap_to_nt()             - translate captured map
 *                                           into NT-style descriptors
 */
#ifndef _BOOT_EFI_MEMMAP_H_
#define _BOOT_EFI_MEMMAP_H_

#include <efi.h>
#include <efilib.h>

/* Capture the current UEFI memory map. Populates internal state and
 * returns the MapKey the caller must pass to ExitBootServices. */
EFI_STATUS memmap_capture(UINTN *out_map_key);

/*
 * NT memory descriptor emitted from translate. This mirrors the fields of
 * MEMORY_ALLOCATION_DESCRIPTOR but without the NT LIST_ENTRY — loaderblock
 * links them into the loader-block list when it stitches things together.
 */
typedef struct {
    UINT32 memory_type;    /* ARC MEMORY_TYPE value */
    UINT32 base_page;      /* phys / 4096 */
    UINT32 page_count;
} NtMemEntry;

/* Translate captured UEFI descriptors + allocation registry into
 * NT-style entries. Sorted by base_page, adjacent same-type entries
 * merged. Returns 0 on success. Does not allocate — writes into the
 * caller-supplied array. */
EFI_STATUS memmap_to_nt(NtMemEntry *out, UINTN out_cap, UINTN *out_n);

#endif
