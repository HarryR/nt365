#include "memmap.h"
#include "log.h"
#include "mmu.h"

/*
 * Iteration on a UEFI memory map walks byte-by-byte using the
 * DescriptorSize returned by GetMemoryMap, NOT sizeof(EFI_MEMORY_DESCRIPTOR).
 * The spec lets UEFI extend descriptors with trailing fields; naive
 * array indexing will silently skip or duplicate entries on extended maps.
 */

static EFI_MEMORY_DESCRIPTOR *g_map       = NULL;
static UINTN                  g_map_bytes = 0;
static UINTN                  g_desc_size = 0;
static UINT32                 g_desc_ver  = 0;

/* How much slack to add when sizing the buffer: the moment we
 * AllocatePool, the map gets an extra entry (the pool allocation
 * itself), and ExitBootServices's MapKey has to match the *latest*
 * map.  Add enough headroom that we can GetMemoryMap twice without
 * re-allocating. */
#define MAP_SLACK_BYTES 1024

EFI_STATUS memmap_capture(UINTN *out_map_key) {
    UINTN      size     = 0;
    UINTN      map_key  = 0;
    UINTN      ds       = 0;
    UINT32     dv       = 0;
    EFI_STATUS status;

    /* First call: 0-sized buffer to learn the required size. */
    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
                               &size, NULL, &map_key, &ds, &dv);
    if (status != EFI_BUFFER_TOO_SMALL) {
        BXLOG(L"unexpected GetMemoryMap (sizing) status=0x%lx", (UINT64)status);
        return status;
    }

    size += MAP_SLACK_BYTES;
    status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, size, (void **)&g_map);
    if (EFI_ERROR(status)) {
        BXLOG(L"AllocatePool failed");
        return status;
    }

    /* Second call: actual fill. Must be the last allocation before
     * ExitBootServices, otherwise map_key is stale. */
    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
                               &size, g_map, &map_key, &ds, &dv);
    if (EFI_ERROR(status)) {
        BXLOG(L"GetMemoryMap (fill) failed status=0x%lx", (UINT64)status);
        uefi_call_wrapper(BS->FreePool, 1, g_map);
        g_map = NULL;
        return status;
    }

    g_map_bytes = size;
    g_desc_size = ds;
    g_desc_ver  = dv;

    if (out_map_key) *out_map_key = map_key;

    /* DO NOT BXLOG past this point — Print() calls AllocatePool
     * internally, which invalidates the MapKey we just captured and
     * causes ExitBootServices to return EFI_INVALID_PARAMETER. The
     * error path above is fine because we return failure anyway. */
    return EFI_SUCCESS;
}

/* memmap_dump / memmap_dump_registry were verbose diagnostic helpers
 * used during UEFI bring-up. Removed in the Print() refactor — the per-
 * descriptor dump was ~35 lines of noise on a 128 MB guest and every
 * info it surfaced is already implied by the mmu_alloc trace lines
 * plus the `N NT descriptors, highest_end=...` summary at memmap_to_nt. */

/*
 * UEFI -> NT memory type mapping. The NT kernel's MmInitSystem is
 * picky about which types are "free" and which are "reserved" — the
 * MBR/page-table triple-fault bug in boot/loader.c taught us to mark
 * pages carrying our own data (page tables, PCR, TSS) as something
 * MmFreeLoaderBlock won't reclaim.
 *
 * For the UEFI path:
 *   Our ntoskrnl/hal/hive/NLS/kernel32/ntdll live in AllocatePages'd
 *     EfiLoaderData pages. Kernel needs them — mark LoaderSystemCode /
 *     LoaderHalCode / LoaderMemoryData / LoaderNlsData / LoaderRegistryData
 *     depending on *which* file, which memmap.c can't know. So the
 *     mapping here is coarse (LoaderMemoryData fallback for LoaderData)
 *     and the precise typing is layered on top by loaderblock.c as it
 *     builds per-image descriptors.
 *
 *   EfiConventionalMemory  -> LoaderFree
 *   EfiLoaderCode/Data     -> LoaderMemoryData  (placeholder — see above)
 *   EfiBootServicesCode/Dat-> LoaderFree        (reclaimable after ExitBS)
 *   EfiRuntimeServicesCode -> LoaderFirmwarePermanent
 *   EfiRuntimeServicesData -> LoaderFirmwarePermanent
 *   EfiACPIReclaim/NVS     -> LoaderFirmwarePermanent
 *   EfiMemoryMappedIO*     -> LoaderFirmwarePermanent
 *   EfiReserved/Unusable   -> LoaderFirmwarePermanent
 *   EfiPalCode             -> LoaderFirmwarePermanent
 */
/*
 * ARC MEMORY_TYPE values — must match the enum in nt.h / arc.h exactly.
 * Getting these wrong corrupts the kernel's MM bitmap logic silently.
 */
#define NT_LoaderFree                  2
#define NT_LoaderBad                   3
#define NT_LoaderLoadedProgram         4
#define NT_LoaderFirmwareTemporary     5
#define NT_LoaderFirmwarePermanent     6
#define NT_LoaderOsloaderHeap          7
#define NT_LoaderSystemCode            9
#define NT_LoaderHalCode               10
#define NT_LoaderBootDriver            11
#define NT_LoaderStartupPcrPage        17
#define NT_LoaderRegistryData          19
#define NT_LoaderMemoryData            20
#define NT_LoaderNlsData               21

static UINT32 kind_to_nt(PageKind k) {
    switch (k) {
    case PK_KERNEL_IMAGE:   return NT_LoaderSystemCode;
    case PK_HAL_IMAGE:      return NT_LoaderHalCode;
    case PK_BOOT_DRIVER:    return NT_LoaderBootDriver;
    case PK_REGISTRY:       return NT_LoaderRegistryData;
    case PK_NLS:            return NT_LoaderNlsData;
    case PK_PCR:            return NT_LoaderStartupPcrPage;
    case PK_MEMORY_DATA:    return NT_LoaderMemoryData;
    case PK_FIRMWARE_PERM:  return NT_LoaderFirmwarePermanent;
    case PK_FIRMWARE_TEMP:  return NT_LoaderFirmwareTemporary;
    default:                return NT_LoaderFree;
    }
}

/* Coarse UEFI type -> NT type for pages NOT in the registry. */
static UINT32 uefi_to_nt(UINT32 t) {
    switch (t) {
    case EfiConventionalMemory:      return NT_LoaderFree;
    /* Boot services memory is reclaimable once we ExitBootServices,
     * but NT-side we conservatively mark it firmware-temporary until
     * MmFreeLoaderBlock has run. */
    case EfiBootServicesCode:
    case EfiBootServicesData:        return NT_LoaderFirmwareTemporary;
    /* EfiLoaderData not claimed by our registry is stack/pool from
     * UEFI itself (e.g. the memory map buffer). Treat as reclaimable. */
    case EfiLoaderCode:
    case EfiLoaderData:              return NT_LoaderFirmwareTemporary;
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
    case EfiPalCode:
    case EfiReservedMemoryType:      return NT_LoaderFirmwarePermanent;
    case EfiUnusableMemory:          return NT_LoaderBad;
    default:                         return NT_LoaderFirmwarePermanent;
    }
}

static void emit_entry(NtMemEntry *out, UINTN cap, UINTN *n,
                       UINT32 type, UINT32 base_page, UINT32 pages) {
    if (pages == 0 || *n >= cap) return;
    /* Merge with previous entry if adjacent and same type. */
    if (*n > 0) {
        NtMemEntry *prev = &out[*n - 1];
        if (prev->memory_type == type &&
            prev->base_page + prev->page_count == base_page) {
            prev->page_count += pages;
            return;
        }
    }
    out[*n].memory_type = type;
    out[*n].base_page   = base_page;
    out[*n].page_count  = pages;
    (*n)++;
}

/*
 * For a UEFI descriptor [desc_base, desc_base + desc_pages), walk the
 * registry and emit NT entries that either use the registered PageKind
 * (for overlapping ranges) or the coarse UEFI->NT mapping (for gaps).
 *
 * The registry is tiny (≤ 14 in current flow), so an O(UEFI × registry)
 * pass is cheap and simple. We emit pieces in address order.
 */
static void split_by_registry(UINT32 desc_base, UINT32 desc_pages,
                              UINT32 uefi_type,
                              NtMemEntry *out, UINTN cap, UINTN *n) {
    UINT32 cursor = desc_base;
    UINT32 end    = desc_base + desc_pages;
    UINT32 coarse_nt = uefi_to_nt(uefi_type);

    while (cursor < end) {
        UINTN i;
        UINT32 best_overlap_start = end;
        UINT32 best_overlap_end   = end;
        UINT32 best_nt            = 0;
        int    found              = 0;

        /* Find the registry entry whose overlap with [cursor, end)
         * starts earliest. */
        for (i = 0; i < mmu_registry_count(); i++) {
            const AllocEntry *e = mmu_registry_entry(i);
            UINT32 rb = (UINT32)(e->phys >> 12);
            UINT32 re = rb + (UINT32)e->pages;
            UINT32 os = rb > cursor ? rb : cursor;
            UINT32 oe = re < end    ? re : end;
            if (os >= oe) continue;     /* no overlap */
            if (os < best_overlap_start) {
                best_overlap_start = os;
                best_overlap_end   = oe;
                best_nt            = kind_to_nt(e->kind);
                found              = 1;
            }
        }

        if (!found) {
            /* Rest of the UEFI descriptor is uncovered — emit as coarse. */
            emit_entry(out, cap, n, coarse_nt, cursor, end - cursor);
            return;
        }

        /* Gap before the overlap: coarse type */
        if (best_overlap_start > cursor) {
            emit_entry(out, cap, n, coarse_nt,
                       cursor, best_overlap_start - cursor);
        }
        /* The overlap itself: registry type */
        emit_entry(out, cap, n, best_nt,
                   best_overlap_start, best_overlap_end - best_overlap_start);
        cursor = best_overlap_end;
    }
}

EFI_STATUS memmap_to_nt(NtMemEntry *out, UINTN out_cap, UINTN *out_n) {
    UINT8 *p, *end;

    if (!g_map) return EFI_NOT_READY;
    if (!out || !out_n || out_cap == 0) return EFI_INVALID_PARAMETER;

    *out_n = 0;
    p   = (UINT8 *)g_map;
    end = p + g_map_bytes;

    for (; p < end; p += g_desc_size) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)p;
        UINT32 base_page = (UINT32)(d->PhysicalStart >> 12);
        UINT32 pages     = (UINT32)d->NumberOfPages;
        if (pages == 0) continue;
        /* Skip non-RAM regions entirely: NT's MM uses the highest
         * descriptor (BasePage + PageCount) to size its PFN bitmap.
         * Including MMIO/Reserved/IOPort/PalCode (which sit at phys
         * addresses well beyond real RAM — e.g. LAPIC at 0xFEC00000)
         * would cause the bitmap to cover ~4 GB and RtlSetBits to
         * walk off the end into unmapped KSEG0. */
        switch (d->Type) {
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
        case EfiPalCode:
        case EfiReservedMemoryType:
        case EfiUnusableMemory:
            continue;
        default:
            break;
        }
        split_by_registry(base_page, pages, d->Type, out, out_cap, out_n);
    }

    /* No BXLOG here: memmap_to_nt is called from lpb_link_memmap AFTER
     * memmap_capture, and Print() allocates internally via UEFI, which
     * invalidates the MapKey we need for ExitBootServices. */
    return EFI_SUCCESS;
}
