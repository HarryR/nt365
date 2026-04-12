/*
 * display.c - MicroNT HAL display output
 *
 * Routes HalDisplayString to COM1 serial port.
 * This gives us debug output from the kernel on the QEMU serial console.
 */

#include "halp.h"

static BOOLEAN SerialInitialized = FALSE;

static VOID
HalpInitSerial(VOID)
{
    if (SerialInitialized) return;

    HalpWritePort(COM1_PORT + 1, 0x00);  /* Disable interrupts */
    HalpWritePort(COM1_PORT + 3, 0x80);  /* DLAB on */
    HalpWritePort(COM1_PORT + 0, 0x01);  /* 115200 baud (divisor 1) */
    HalpWritePort(COM1_PORT + 1, 0x00);
    HalpWritePort(COM1_PORT + 3, 0x03);  /* 8N1, DLAB off */
    HalpWritePort(COM1_PORT + 2, 0xC7);  /* Enable FIFO */
    HalpWritePort(COM1_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */

    SerialInitialized = TRUE;
}

VOID
HalpSerialPutChar(CHAR c)
{
    HalpInitSerial();

    /* Wait for transmit buffer empty */
    while (!(HalpReadPort(COM1_PORT + 5) & 0x20))
        ;
    HalpWritePort(COM1_PORT, (UCHAR)c);
}

VOID
HalpSerialPrint(PCHAR s)
{
    while (*s) {
        if (*s == '\n')
            HalpSerialPutChar('\r');
        HalpSerialPutChar(*s++);
    }
}

/*
 * HalDisplayString - kernel calls this for early boot messages and BSODs
 */
VOID
HalDisplayString(
    IN PUCHAR String
    )
{
    HalpSerialPrint((PCHAR)String);
}

VOID
HalQueryDisplayParameters(
    OUT PULONG WidthInCharacters,
    OUT PULONG HeightInLines,
    OUT PULONG CursorColumn,
    OUT PULONG CursorRow
    )
{
    *WidthInCharacters = 80;
    *HeightInLines = 25;
    *CursorColumn = 0;
    *CursorRow = 0;
}

VOID
HalSetDisplayParameters(
    IN ULONG CursorColumn,
    IN ULONG CursorRow
    )
{
    /* Nothing to do for serial */
}

VOID
HalAcquireDisplayOwnership(
    IN PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters
    )
{
    /* Nothing to do */
}
