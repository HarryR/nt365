#include "arena.h"
#include "log.h"
#include "mmu.h"

static EFI_PHYSICAL_ADDRESS g_phys     = 0;
static UINTN                g_capacity = 0;   /* bytes */
static UINTN                g_used     = 0;   /* bytes */

EFI_STATUS arena_init(UINTN pages) {
    /* < 16 MiB is the hard constraint: PDE[512..515] is the only KSEG0
     * range MmCreateProcessAddressSpace (NTOS/MM/PROCSUP.C:297-305) copies
     * into new process PDs. Under OVMF64 with large-RAM guests UEFI tends
     * to place EfiLoaderData allocations high, which puts a high arena
     * out of reach the moment MM init swaps address spaces. */
    EFI_STATUS s = mmu_alloc_below(pages, PK_MEMORY_DATA, 0x01000000, &g_phys);
    if (EFI_ERROR(s)) return s;
    g_capacity = pages << 12;
    g_used     = 0;
    return EFI_SUCCESS;
}

void *arena_alloc(UINTN size, UINTN align) {
    UINTN base = (UINTN)g_phys + g_used;
    UINTN pad  = (align - (base & (align - 1))) & (align - 1);
    void *p;
    g_used += pad;
    p = (void *)(UINTN)(g_phys + g_used);
    g_used += size;
    if (g_used > g_capacity) {
        BXLOG(L"OOM (need %lu / %lu)", (UINT64)g_used, (UINT64)g_capacity);
        return 0;
    }
    /* Zero the allocation for determinism. */
    {
        UINT8 *u = p;
        for (UINTN i = 0; i < size; i++) u[i] = 0;
    }
    return p;
}

EFI_PHYSICAL_ADDRESS arena_phys(void)     { return g_phys; }
UINTN                arena_used(void)     { return g_used; }
UINTN                arena_capacity(void) { return g_capacity; }

static UINTN ascii_len(const char *s) {
    UINTN n = 0; while (s[n]) n++; return n;
}

char *arena_dup_ascii(const char *s) {
    UINTN n = ascii_len(s);
    char *d = arena_alloc(n + 1, 1);
    if (!d) return 0;
    for (UINTN i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
    return d;
}

UINT16 *arena_dup_utf16_from_ascii(const char *s, UINT16 *out_len_bytes) {
    UINTN n = ascii_len(s);
    UINT16 *d = arena_alloc((n + 1) * 2, 2);
    if (!d) return 0;
    for (UINTN i = 0; i < n; i++) d[i] = (UINT16)(unsigned char)s[i];
    d[n] = 0;
    if (out_len_bytes) *out_len_bytes = (UINT16)(n * 2);
    return d;
}
