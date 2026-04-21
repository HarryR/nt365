/*
 * COM1 raw serial output, used only post-ExitBootServices where UEFI's
 * Print() is no longer available. Pre-exit logging goes through log.h's
 * BXLOG() macro (which wraps Print).
 *
 * Serial at 0x3F8, 115200 8N1 — matches OVMF's firmware-console setup,
 * so UEFI Print() output and these raw writes end up on the same wire.
 */
#ifndef _BOOT_EFI_COM1_H_
#define _BOOT_EFI_COM1_H_

/* Initialise the UART. Must be called once, before the first com1_puts.
 * Idempotent with OVMF's own init — writing the same settings twice is
 * harmless. */
void com1_init(void);

/* Null-terminated ASCII write to COM1. LF is translated to CRLF so the
 * output is line-terminal friendly. No formatting — use BXLOG pre-exit
 * if you need any. */
void com1_puts(const char *s);

#endif
