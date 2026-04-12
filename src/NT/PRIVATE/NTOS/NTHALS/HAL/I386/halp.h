/*
 * halp.h - MicroNT HAL private header
 */

#ifndef _HALP_H_
#define _HALP_H_

#include "nthal.h"
#include "hal.h"

/* 8259 PIC ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

/* 8254 PIT */
#define PIT_CH0     0x40
#define PIT_CMD     0x43
#define PIT_FREQ    1193182

/* Serial ports */
#define COM1_PORT   0x3F8   /* Used by KD (kernel debugger) */
#define COM2_PORT   0x2F8   /* Used by HAL debug output */
#define HAL_DEBUG_PORT  COM2_PORT

/* PIC vector base */
#define PRIMARY_VECTOR_BASE     0x30
#define CLOCK_VECTOR            (PRIMARY_VECTOR_BASE + 0)
#define PROFILE_VECTOR          (PRIMARY_VECTOR_BASE + 8)

/* IRQL levels */
#ifndef CLOCK2_LEVEL
#define CLOCK2_LEVEL    28
#endif
#ifndef PROFILE_LEVEL
#define PROFILE_LEVEL   27
#endif

/* Port I/O — simple functions using inline asm */
static UCHAR _inline HalpReadPort(USHORT port) {
    UCHAR val;
    _asm { mov dx, port }
    _asm { in al, dx }
    _asm { mov val, al }
    return val;
}

static VOID _inline HalpWritePort(USHORT port, UCHAR val) {
    _asm { mov dx, port }
    _asm { mov al, val }
    _asm { out dx, al }
}

/* Serial output for debug */
VOID HalpSerialPutChar(CHAR c);
VOID HalpSerialPrint(PCHAR s);

#endif /* _HALP_H_ */
