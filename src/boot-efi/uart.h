/*
 * Legacy ISA UART detection.
 *
 * Scoped purely to probing — "is there a UART answering at this I/O
 * base?" — so hwtree can emit accurate SerialController nodes. Does
 * not own any UART for output; that remains com1's job (and stays
 * independent so the loader's own debug prints don't depend on probe
 * results).
 */
#ifndef _BOOT_EFI_UART_H_
#define _BOOT_EFI_UART_H_

#include <efi.h>

/* Scratch-register round-trip test at `base+7`. Works on 16450 and later
 * (i.e. anything built since 1987 — 8250 lacks a scratch register).
 * Preserves the original scratch value. Returns 1 if a UART responds,
 * 0 otherwise. */
int uart_probe(UINT16 base);

#endif
