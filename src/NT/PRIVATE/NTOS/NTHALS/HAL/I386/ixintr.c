/*
 * ixintr.c - MicroNT HAL interrupt management (i386)
 */

#include "halp.h"

/*
 * HalBeginSystemInterrupt - called at ISR entry
 * Returns FALSE for spurious interrupts
 */
BOOLEAN
HalBeginSystemInterrupt(
    IN KIRQL Irql,
    IN ULONG Vector,
    OUT PKIRQL OldIrql
    )
{
    PKPCR Pcr = KeGetPcr();

    *OldIrql = Pcr->Irql;
    Pcr->Irql = Irql;

    /* Send EOI for PIC interrupts */
    if (Vector >= PRIMARY_VECTOR_BASE && Vector < PRIMARY_VECTOR_BASE + 16) {
        ULONG irq = Vector - PRIMARY_VECTOR_BASE;

        /* Check for spurious IRQ 7 */
        if (irq == 7) {
            HalpWritePort(PIC1_CMD, 0x0B);  /* Read ISR */
            if (!(HalpReadPort(PIC1_CMD) & 0x80)) {
                return FALSE;  /* Spurious */
            }
        }
        /* Check for spurious IRQ 15 */
        if (irq == 15) {
            HalpWritePort(PIC2_CMD, 0x0B);
            if (!(HalpReadPort(PIC2_CMD) & 0x80)) {
                /* Send EOI to master for cascade */
                HalpWritePort(PIC1_CMD, 0x20);
                return FALSE;
            }
        }
    }

    return TRUE;
}

/*
 * HalEndSystemInterrupt - called at ISR exit
 */
VOID
HalEndSystemInterrupt(
    IN KIRQL OldIrql,
    IN ULONG Vector
    )
{
    /* Send EOI */
    if (Vector >= PRIMARY_VECTOR_BASE + 8) {
        HalpWritePort(PIC2_CMD, 0x20);  /* EOI to slave */
    }
    if (Vector >= PRIMARY_VECTOR_BASE) {
        HalpWritePort(PIC1_CMD, 0x20);  /* EOI to master */
    }

    /* Restore IRQL */
    KfLowerIrql(OldIrql);
}

/*
 * HalEnableSystemInterrupt - unmask a specific IRQ.
 *
 * Drivers reach here via KeConnectInterrupt (after IoConnectInterrupt
 * or NDIS' wrapper). Vector is the SYSTEM IDT vector — already
 * translated from the bus-relative IRQ by HalGetInterruptVector.
 * If you call NdisMRegisterInterrupt: pass BUS-level vec/irql, NDIS
 * does the translation internally; passing system-level values
 * double-converts and the resulting Vector misses the mask write
 * here, leaving the IRQ blocked at PIC level for ever after.
 *
 * InterruptMode is honoured implicitly by the chipset's existing
 * ELCR config — we don't program ELCR per-IRQ (stock NT 3.5 HALX86
 * doesn't either; ELCR is treated as firmware-supplied input).
 */
BOOLEAN
HalEnableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql,
    IN KINTERRUPT_MODE InterruptMode
    )
{
    ULONG irq;

    UNREFERENCED_PARAMETER(InterruptMode);

    if (Vector < PRIMARY_VECTOR_BASE || Vector >= PRIMARY_VECTOR_BASE + 16) {
        return FALSE;
    }

    irq = Vector - PRIMARY_VECTOR_BASE;

    if (irq < 8) {
        UCHAR mask = HalpReadPort(PIC1_DATA);
        mask &= ~(1 << irq);
        HalpWritePort(PIC1_DATA, mask);
    } else {
        UCHAR mask = HalpReadPort(PIC2_DATA);
        mask &= ~(1 << (irq - 8));
        HalpWritePort(PIC2_DATA, mask);
    }

    return TRUE;
}

/*
 * HalDisableSystemInterrupt - mask a specific IRQ
 */
VOID
HalDisableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql
    )
{
    ULONG irq;

    if (Vector < PRIMARY_VECTOR_BASE || Vector >= PRIMARY_VECTOR_BASE + 16) {
        return;
    }

    irq = Vector - PRIMARY_VECTOR_BASE;

    if (irq < 8) {
        UCHAR mask = HalpReadPort(PIC1_DATA);
        mask |= (1 << irq);
        HalpWritePort(PIC1_DATA, mask);
    } else {
        UCHAR mask = HalpReadPort(PIC2_DATA);
        mask |= (1 << (irq - 8));
        HalpWritePort(PIC2_DATA, mask);
    }
}

/* HalGetInterruptVector is now provided by ixbusdat.c (bus handler dispatch) */

/*
 * HalHandleNMI
 */
VOID
HalHandleNMI(
    IN PVOID NmiInfo
    )
{
    HalpSerialPrint("HAL: *** NMI ***\r\n");
    KeBugCheck(0x80);  /* NMI_HARDWARE_FAILURE */
}
