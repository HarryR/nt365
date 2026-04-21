/*
 * Structured diagnostic output for the UEFI loader (pre-ExitBootServices).
 *
 * One macro — BXLOG — wraps gnu-efi's Print() and automatically prefixes
 * every line with "bootx64.efi!<function>: " so the source of each
 * message is self-evident when reading the serial log.
 *
 * Format strings are UEFI-style wide strings:
 *
 *   BXLOG(L"MBR signature=0x%x sum=0x%x", sig, sum);
 *   BXLOG(L"alloc %u pages at 0x%lx for %a", pages, phys, kind_name);
 *
 * Specifiers match gnu-efi Print: %a for ASCII, %s for CHAR16, %x / %lx
 * for hex, %u / %lu for unsigned decimal. See /usr/include/efi/efilib.h.
 *
 * ==== Where BXLOG is safe vs. not ====
 *
 * Two boundaries matter:
 *
 *   1. memmap_capture. Any UEFI service that allocates from the pool
 *      (AllocatePool, Print's internal format buffer, pretty much
 *      anything under BootServices) invalidates the MapKey the caller
 *      captured — ExitBootServices then returns EFI_INVALID_PARAMETER.
 *      Code paths that run between memmap_capture and ExitBootServices
 *      (lpb_link_memmap, memmap_to_nt's post-translation logging) must
 *      NOT call BXLOG; stay purely on arena memory.
 *
 *   2. ExitBootServices. UEFI is gone entirely after this. Print is
 *      undefined; BXLOG would hang or fault. Post-exit call sites
 *      (mmu_build_and_activate, the handoff marker, kernel-returned
 *      sentinel) use com1_puts directly with a literal prefix.
 */
#ifndef _BOOT_EFI_LOG_H_
#define _BOOT_EFI_LOG_H_

#include <efi.h>
#include <efilib.h>

#define BXLOG(fmt, ...) \
    Print(L"boot!%a: " fmt L"\n", __func__, ##__VA_ARGS__)

#endif
