/*
 * Bump-allocator arena for NT-visible structures.
 *
 * Design:
 *   - One contiguous physical region, page-multiple size, allocated via
 *     mmu_alloc_below so its KSEG0 alias lands in PDE[512..515] (the
 *     range NT 3.5's MmCreateProcessAddressSpace copies into every new
 *     process PD).
 *   - Callers (lpb, hwtree, …) bump-allocate sub-buffers. Each allocation
 *     returns a physical-addressed pointer that also serves as the
 *     KSEG0 VA source when OR'd with KSEG0_BASE — no post-paging fixup.
 *   - Zero-filled on return (deterministic).
 *
 * Singleton: there is one arena per boot. arena_init() is called once
 * from main.c before any producer uses it.
 */
#ifndef _BOOT_EFI_ARENA_H_
#define _BOOT_EFI_ARENA_H_

#include <efi.h>

/* Reserve `pages` physical pages via mmu_alloc_below(< 16 MiB, PK_MEMORY_DATA)
 * and prepare them as the arena. Returns the underlying mmu_alloc status. */
EFI_STATUS arena_init(UINTN pages);

/* Bump-allocate `size` bytes at `align`-byte alignment. Returns a
 * phys-addressed pointer (equals the KSEG0 alias via `KSEG0_BASE | phys`).
 * Zero-initialized. Returns NULL on OOM (logs to com1). */
void *arena_alloc(UINTN size, UINTN align);

/* Diagnostic accessors. */
EFI_PHYSICAL_ADDRESS arena_phys(void);
UINTN                arena_used(void);
UINTN                arena_capacity(void);

/* Convenience: copy an ASCII null-terminated string into the arena.
 * Returns pointer to the copy. */
char *arena_dup_ascii(const char *s);

/* Convenience: copy an ASCII string into the arena as UTF-16 (zero-
 * extended). `out_len_bytes` returns the byte length excluding the NUL
 * terminator (matches the semantics of `UNICODE_STRING.Length`). */
UINT16 *arena_dup_utf16_from_ascii(const char *s, UINT16 *out_len_bytes);

#endif
