/*
 * irql.c - MicroNT HAL IRQL management
 *
 * Maps NT IRQLs to 8259 PIC mask register values.
 * KfRaiseIrql/KfLowerIrql are the fast (fastcall) versions.
 */

#include "halp.h"

/*
 * IRQL to PIC mask mapping table.
 * Higher IRQL = more interrupts masked.
 *
 * IRQL 0 (PASSIVE):  mask = 0x00 (nothing masked except what IDR says)
 * IRQL 1 (APC):      mask = 0x00 (software level, no PIC change)
 * IRQL 2 (DISPATCH):  mask = 0x00 (software level)
 * IRQL 3-19:          device IRQLs (progressively mask more IRQs)
 * IRQL 27 (PROFILE):  mask = 0xFE (only IRQ0 unmasked)
 * IRQL 28 (CLOCK):    mask = 0xFF (all masked)
 * IRQL 31 (HIGH):     mask = 0xFF
 *
 * For simplicity, IRQLs 0-2 are software-only (no PIC change).
 * IRQLs 3+ mask hardware via PIC.
 * The actual mask applied = table[irql] | PcIDR (permanently disabled IRQs).
 */

/*
 * Raise IRQL — fastcall convention: new IRQL in ECX, returns old IRQL
 */
KIRQL
__fastcall
KfRaiseIrql(
    IN KIRQL NewIrql
    )
{
    KIRQL OldIrql;
    PKPCR Pcr = KeGetPcr();

    OldIrql = Pcr->Irql;
    Pcr->Irql = NewIrql;

    /* For high IRQLs, disable hardware interrupts */
    if (NewIrql >= CLOCK2_LEVEL) {
        _asm { cli }
    }

    return OldIrql;
}

/*
 * Lower IRQL — fastcall convention: new IRQL in ECX
 */
VOID
__fastcall
KfLowerIrql(
    IN KIRQL NewIrql
    )
{
    PKPCR Pcr = KeGetPcr();

    Pcr->Irql = NewIrql;

    /* Re-enable interrupts when dropping below CLOCK level */
    if (NewIrql < CLOCK2_LEVEL) {
        _asm { sti }
    }

    /* Check for pending software interrupts (DPC/APC) */
    if (NewIrql < 2) {
        /* TODO: dispatch pending DPCs/APCs */
    }
}

/*
 * Non-fastcall wrappers (older calling convention)
 */
VOID
KeRaiseIrql(
    IN KIRQL NewIrql,
    OUT PKIRQL OldIrql
    )
{
    *OldIrql = KfRaiseIrql(NewIrql);
}

VOID
KeLowerIrql(
    IN KIRQL NewIrql
    )
{
    KfLowerIrql(NewIrql);
}

KIRQL
KeGetCurrentIrql(VOID)
{
    return KeGetPcr()->Irql;
}


/*
 * Software interrupt request/clear
 */
VOID
FASTCALL
HalClearSoftwareInterrupt(
    IN KIRQL Request
    )
{
    /* Clear pending bit in IRR */
    KeGetPcr()->IRR &= ~(1 << Request);
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(
    IN KIRQL Request
    )
{
    /* Set pending bit in IRR */
    KeGetPcr()->IRR |= (1 << Request);
}


/*
 * Spinlock support — UP versions (no actual spinning needed)
 */
VOID
KeAcquireSpinLock(
    IN PKSPIN_LOCK SpinLock,
    OUT PKIRQL OldIrql
    )
{
    *OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
}

VOID
KeReleaseSpinLock(
    IN PKSPIN_LOCK SpinLock,
    IN KIRQL NewIrql
    )
{
    KfLowerIrql(NewIrql);
}

KIRQL
__fastcall
KfAcquireSpinLock(
    IN PKSPIN_LOCK SpinLock
    )
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

VOID
__fastcall
KfReleaseSpinLock(
    IN PKSPIN_LOCK SpinLock,
    IN KIRQL NewIrql
    )
{
    KfLowerIrql(NewIrql);
}

/*
 * Fast mutex (UP versions)
 */
VOID
ExAcquireFastMutex(
    IN PVOID FastMutex
    )
{
    KfRaiseIrql(APC_LEVEL);
}

BOOLEAN
ExTryToAcquireFastMutex(
    IN PVOID FastMutex
    )
{
    KfRaiseIrql(APC_LEVEL);
    return TRUE;
}

VOID
ExReleaseFastMutex(
    IN PVOID FastMutex
    )
{
    KfLowerIrql(PASSIVE_LEVEL);
}
