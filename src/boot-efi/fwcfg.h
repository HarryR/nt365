#ifndef FWCFG_H
#define FWCFG_H

/* QEMU fw_cfg reader.  We use it to ferry the kernel boot-options
 * string from the host (`boot.sh --kernel-opts ...`) into the
 * LOADER_PARAMETER_BLOCK.LoadOptions slot.  No DMA, no UEFI protocol —
 * just legacy selector(0x510) + data(0x511) port I/O.
 *
 * Returns the number of bytes copied into `out` (NUL terminator
 * always written, included in the count).  Returns 0 if the named
 * file is absent or empty, leaving `out[0] = 0`. */
unsigned fwcfg_read_string(const char *name, char *out, unsigned out_cap);

#endif
