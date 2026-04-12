/*
 * halinit.c - MicroNT HAL initialization
 *
 * HalInitSystem Phase 0: Program PICs, mask all interrupts, sti
 * HalInitSystem Phase 1: Connect clock interrupt, unmask timer
 * HalInitializeProcessor: Per-CPU init, stall calibration
 */

#include "halp.h"

ULONG HalpBusType = 0;  /* ISA */

/*
 * HalInitializeProcessor - called once per CPU before HalInitSystem
 */
VOID
HalInitializeProcessor(
    IN ULONG Processor
    )
{
    PKPCR Pcr = KeGetPcr();

    /* Enable slave PIC cascade (IRQ 2) in IDR */
    Pcr->IDR = ~(1 << 2);

    /* Set initial stall factor - will be calibrated later */
    Pcr->StallScaleFactor = 50;

}


/*
 * HalInitSystem - main HAL initialization
 */
BOOLEAN
HalInitSystem(
    IN ULONG Phase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    PKPRCB Prcb;

    Prcb = KeGetCurrentPrcb();

    if (Phase == 0) {

        HalpBusType = LoaderBlock->u.I386.MachineType & 0xFF;

        /*
         * Verify kernel/HAL build type match
         */
#ifndef NT_UP
        if (Prcb->BuildType & PRCB_BUILD_UNIPROCESSOR) {
            KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, 0, 0);
        }
#endif
        if (Prcb->MajorVersion != PRCB_MAJOR_VERSION) {
            KeBugCheckEx(MISMATCHED_HAL, 1, Prcb->MajorVersion, PRCB_MAJOR_VERSION, 0);
        }

        /*
         * Program 8259 PICs
         * Master: ICW1=0x11 (edge, cascade, ICW4), ICW2=0x30 (vector base),
         *         ICW3=0x04 (slave on IRQ2), ICW4=0x01 (8086 mode)
         * Slave:  ICW1=0x11, ICW2=0x38, ICW3=0x02 (cascade ID), ICW4=0x01
         */
        HalpWritePort(PIC1_CMD,  0x11);     /* ICW1 master */
        HalpWritePort(PIC1_DATA, PRIMARY_VECTOR_BASE); /* ICW2 */
        HalpWritePort(PIC1_DATA, 0x04);     /* ICW3: slave on IRQ2 */
        HalpWritePort(PIC1_DATA, 0x01);     /* ICW4: 8086 mode */

        HalpWritePort(PIC2_CMD,  0x11);     /* ICW1 slave */
        HalpWritePort(PIC2_DATA, PRIMARY_VECTOR_BASE + 8); /* ICW2 */
        HalpWritePort(PIC2_DATA, 0x02);     /* ICW3: cascade identity */
        HalpWritePort(PIC2_DATA, 0x01);     /* ICW4: 8086 mode */

        /* Mask ALL interrupts */
        HalpWritePort(PIC1_DATA, 0xFF);
        HalpWritePort(PIC2_DATA, 0xFF);

        /* Do NOT sti here — the kernel enables interrupts itself after
         * KiInitializeKernel completes (NEWSYSBG.ASM line 607).
         * Enabling interrupts too early causes exception 16 (#MF) to fire
         * before the interrupt dispatch table is initialized. */

        /* Clear any pending FPU exceptions. The kernel sets CR0.NE=1 during
         * KiIsNpxPresent, enabling native FPU error reporting via exception 16.
         * If a pending FPU error exists when interrupts are later enabled
         * (KiRestoreInterrupts), exception 16 fires before the interrupt
         * dispatch table is initialized, causing BugCheck 0x50. */
        /* Clear pending FPU exceptions to prevent exception 16 (#MF) from
         * firing before the kernel's interrupt dispatch table is initialized.
         * Must temporarily clear EM+TS in CR0 since fnclex checks these on 486. */
        _asm {
            mov eax, cr0
            push eax                     ; save original CR0
            and eax, NOT (CR0_TS+CR0_EM) ; clear TS (bit 3) + EM (bit 2)
            mov cr0, eax
            fnclex                       ; now safe to clear FPU exceptions
            pop eax                      ; restore original CR0
            mov cr0, eax
        }

        HalpSerialPrint("HAL: Phase 0 complete (PICs programmed, all masked)\r\n");

        return TRUE;
    }

    if (Phase == 1) {

        HalpSerialPrint("HAL: Phase 1 - connecting clock...\r\n");

        /*
         * Program 8254 PIT Channel 0 for ~10ms tick (100 Hz)
         * Divisor = 1193182 / 100 = 11932 = 0x2E9C
         */
        {
            USHORT divisor = PIT_FREQ / 100;
            HalpWritePort(PIT_CMD, 0x36);   /* Channel 0, mode 3, lo/hi */
            HalpWritePort(PIT_CH0, (UCHAR)(divisor & 0xFF));
            HalpWritePort(PIT_CH0, (UCHAR)(divisor >> 8));
        }

        /*
         * Connect the clock interrupt
         * The kernel's IDT entry for CLOCK_VECTOR (0x30) should point to
         * a KiUnexpectedInterrupt stub. We need to use HalpEnableInterruptHandler
         * or KeConnectInterrupt to properly wire it up.
         *
         * For now, unmask IRQ 0 (timer) on the PIC.
         * The IDT entry for vector 0x30 should dispatch through the kernel's
         * interrupt dispatch mechanism.
         */

        /* Unmask IRQ 0 (timer) and IRQ 2 (cascade) on master PIC */
        HalpWritePort(PIC1_DATA, 0xFA);  /* mask = 11111010 = all masked except IRQ0 + IRQ2 */

        HalpSerialPrint("HAL: Phase 1 complete (clock enabled)\r\n");

        return TRUE;
    }

    return TRUE;
}
