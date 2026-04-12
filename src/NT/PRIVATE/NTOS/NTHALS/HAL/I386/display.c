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

    /* Initialize COM2 for HAL debug output (COM1 reserved for KD) */
    HalpWritePort(HAL_DEBUG_PORT + 1, 0x00);  /* Disable interrupts */
    HalpWritePort(HAL_DEBUG_PORT + 3, 0x80);  /* DLAB on */
    HalpWritePort(HAL_DEBUG_PORT + 0, 0x01);  /* 115200 baud (divisor 1) */
    HalpWritePort(HAL_DEBUG_PORT + 1, 0x00);
    HalpWritePort(HAL_DEBUG_PORT + 3, 0x03);  /* 8N1, DLAB off */
    HalpWritePort(HAL_DEBUG_PORT + 2, 0xC7);  /* Enable FIFO */
    HalpWritePort(HAL_DEBUG_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */

    SerialInitialized = TRUE;
}

VOID
HalpSerialPutChar(CHAR c)
{
    HalpInitSerial();

    /* Wait for transmit buffer empty */
    while (!(HalpReadPort(HAL_DEBUG_PORT + 5) & 0x20))
        ;
    HalpWritePort(HAL_DEBUG_PORT, (UCHAR)c);
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
