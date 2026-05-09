#include "fwcfg.h"

/* QEMU fw_cfg legacy port interface.
 *
 *   0x510  W   16-bit selector
 *   0x511  R    8-bit data (sequential bytes from selected blob)
 *
 * Item 0x19 (FW_CFG_FILE_DIR) is a directory: 4-byte BE count followed
 * by 64-byte entries
 *
 *     uint32 size       (BE) — data size of this file
 *     uint16 select     (BE) — selector to use to read it
 *     uint16 reserved
 *     char   name[56]        (NUL-padded, at most 56 bytes)
 *
 * Once we've matched a name we set the selector and read `size` bytes
 * from the data port.
 *
 * No DMA path here — under OVMF the legacy interface is always
 * present and our payloads are small (kernel options string).
 */

static inline void outw(unsigned short port, unsigned short v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#define FWCFG_SEL  0x510
#define FWCFG_DATA 0x511

#define FWCFG_FILE_DIR 0x0019

static void fwcfg_select(unsigned short sel) { outw(FWCFG_SEL, sel); }

static void fwcfg_read_bytes(void *dst, unsigned n) {
    unsigned char *p = (unsigned char *)dst;
    for (unsigned i = 0; i < n; i++) p[i] = inb(FWCFG_DATA);
}

static unsigned name_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

unsigned fwcfg_read_string(const char *name, char *out, unsigned out_cap) {
    if (out_cap == 0) return 0;
    out[0] = 0;
    if (out_cap == 1) return 0;

    /* Walk the file directory looking for `name`. */
    fwcfg_select(FWCFG_FILE_DIR);

    unsigned char count_be[4];
    fwcfg_read_bytes(count_be, 4);
    unsigned count = ((unsigned)count_be[0] << 24)
                   | ((unsigned)count_be[1] << 16)
                   | ((unsigned)count_be[2] << 8)
                   |  (unsigned)count_be[3];

    unsigned char entry[64];
    unsigned found_size = 0, found_sel = 0;
    for (unsigned i = 0; i < count; i++) {
        fwcfg_read_bytes(entry, sizeof entry);
        unsigned size = ((unsigned)entry[0] << 24)
                      | ((unsigned)entry[1] << 16)
                      | ((unsigned)entry[2] << 8)
                      |  (unsigned)entry[3];
        unsigned sel  = ((unsigned)entry[4] << 8) | (unsigned)entry[5];
        const char *ename = (const char *)&entry[8];
        if (name_eq(ename, name)) {
            found_size = size;
            found_sel  = sel;
            /* Don't break — must keep draining or future selects can
             * fail on some firmware versions.  Cheaper to just keep
             * reading the remaining 64-byte entries. */
        }
    }
    if (found_sel == 0 || found_size == 0) return 0;

    /* Read the file data.  Cap at out_cap-1 so we always NUL-terminate. */
    unsigned to_read = found_size;
    unsigned trailing = 0;
    if (to_read > out_cap - 1) {
        trailing = to_read - (out_cap - 1);
        to_read = out_cap - 1;
    }

    fwcfg_select((unsigned short)found_sel);
    fwcfg_read_bytes(out, to_read);
    /* Drain remainder so the selector is left in a clean state. */
    for (unsigned i = 0; i < trailing; i++) (void)inb(FWCFG_DATA);

    /* Caller passes a -string fw_cfg blob, which qemu does NOT
     * NUL-terminate.  Some operators paste a CR/LF too — strip
     * trailing whitespace then NUL-terminate. */
    while (to_read > 0) {
        unsigned char c = (unsigned char)out[to_read - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            to_read--;
        } else {
            break;
        }
    }
    out[to_read] = 0;
    return to_read + 1;
}
