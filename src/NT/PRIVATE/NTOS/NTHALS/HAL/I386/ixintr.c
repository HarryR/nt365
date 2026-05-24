/*
 * ixintr.c - MicroNT HAL interrupt management (i386)
 *
 * Two delivery backends, chosen at runtime by HalpApicPresent (set by
 * HalpInitApic in Phase 1):
 *   - LAPIC + IOAPIC (apic.c) — the path used on every modern target
 *     (qemu pc/q35/microvm, Hyper-V, Nitro, VirtualBox). EOI to the
 *     LAPIC; per-IRQ mask via the IOAPIC redirection table.
 *   - legacy 8259 PIC — fallback when no local APIC (CPUID), kept intact.
 *
 * The IRQL model, the IRQ->vector convention (0x30 + irq), and every
 * caller (KeConnectInterrupt, the ISR stubs) are identical for both.
 */

#include "halp.h"

/* Per-IRQ trigger mode, recorded at HalEnableSystemInterrupt time so the
 * EOI path knows whether a level-triggered IOAPIC entry needs the extra
 * IOAPIC EOI (to clear remote-IRR). 1 = level, 0 = edge. */
static UCHAR HalpIrqLevel[16] = { 0 };

/*
 * HalBeginSystemInterrupt - called at ISR entry.
 * Returns FALSE for spurious interrupts.
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

    /* APIC path: no 8259 spurious handshake. A genuine LAPIC spurious
     * arrives on its own vector (0xFF -> HalpApicSpurious), never here. */
    if (HalpApicPresent) {
        return TRUE;
    }

    /* 8259 path: detect spurious IRQ 7 / 15 via the in-service register. */
    if (Vector >= PRIMARY_VECTOR_BASE && Vector < PRIMARY_VECTOR_BASE + 16) {
        ULONG irq = Vector - PRIMARY_VECTOR_BASE;

        if (irq == 7) {
            HalpWritePort(PIC1_CMD, 0x0B);  /* Read ISR */
            if (!(HalpReadPort(PIC1_CMD) & 0x80)) {
                return FALSE;  /* Spurious */
            }
        }
        if (irq == 15) {
            HalpWritePort(PIC2_CMD, 0x0B);
            if (!(HalpReadPort(PIC2_CMD) & 0x80)) {
                HalpWritePort(PIC1_CMD, 0x20);  /* EOI master for cascade */
                return FALSE;
            }
        }
    }

    return TRUE;
}

/*
 * HalEndSystemInterrupt - called at ISR exit.
 */
VOID
HalEndSystemInterrupt(
    IN KIRQL OldIrql,
    IN ULONG Vector
    )
{
    if (HalpApicPresent) {
        /* LAPIC EOI for every interrupt; level-triggered IOAPIC entries
         * additionally need the IOAPIC EOI register to clear remote-IRR. */
        HalpLapicEoi();
        if (Vector >= PRIMARY_VECTOR_BASE && Vector < PRIMARY_VECTOR_BASE + 16) {
            if (HalpIrqLevel[Vector - PRIMARY_VECTOR_BASE]) {
                HalpIoApicEoi((UCHAR)Vector);
            }
        }
    } else {
        /* 8259 non-specific EOI. */
        if (Vector >= PRIMARY_VECTOR_BASE + 8) {
            HalpWritePort(PIC2_CMD, 0x20);  /* EOI to slave */
        }
        if (Vector >= PRIMARY_VECTOR_BASE) {
            HalpWritePort(PIC1_CMD, 0x20);  /* EOI to master */
        }
    }

    KfLowerIrql(OldIrql);
}

/*
 * HalEnableSystemInterrupt - unmask a specific IRQ.
 *
 * Vector is the SYSTEM IDT vector (0x30 + irq), already translated from
 * the bus-relative IRQ by HalGetInterruptVector.  On the IOAPIC path the
 * IRQ maps 1:1 to the GSI (the legacy ISA line); InterruptMode picks
 * edge/high (ISA) vs level/low (PCI).  (The timer, IRQ0/GSI2, is
 * programmed directly in HalInitSystem, not here.)
 */
BOOLEAN
HalEnableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql,
    IN KINTERRUPT_MODE InterruptMode
    )
{
    ULONG irq;

    UNREFERENCED_PARAMETER(Irql);

    if (Vector < PRIMARY_VECTOR_BASE || Vector >= PRIMARY_VECTOR_BASE + 16) {
        return FALSE;
    }
    irq = Vector - PRIMARY_VECTOR_BASE;

    if (HalpApicPresent) {
        BOOLEAN level = (InterruptMode == LevelSensitive);
        HalpIrqLevel[irq] = (UCHAR)level;
        /* ISA = edge/active-high, PCI = level/active-low: polarity tracks
         * the trigger mode, which is what InterruptMode encodes for us. */
        HalpIoApicSetEntry((UCHAR)irq, (UCHAR)Vector, level, level, FALSE);
        return TRUE;
    }

    /* 8259: clear the IRQ's mask bit. */
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
 * HalDisableSystemInterrupt - mask a specific IRQ.
 */
VOID
HalDisableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql
    )
{
    ULONG irq;

    UNREFERENCED_PARAMETER(Irql);

    if (Vector < PRIMARY_VECTOR_BASE || Vector >= PRIMARY_VECTOR_BASE + 16) {
        return;
    }
    irq = Vector - PRIMARY_VECTOR_BASE;

    if (HalpApicPresent) {
        HalpIoApicMask((UCHAR)irq, TRUE);
        return;
    }

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
