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
 *
 * SYSENTER MSR programming lives in the kernel proper (KiInitializeKernel
 * via KiSetupSysenter) rather than here — the kernel needs to pass the
 * address of its own KiFastSystemCall handler to the MSR, and we'd prefer
 * not to expose an internal Ki* routine across the HAL/ntoskrnl import
 * boundary just to do MSR writes.
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

        HalpInitBusHandlers();

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
         * Install HalpClockInterrupt (clock.asm) at IDT vector 0x30 so
         * IRQ0 from the PIT dispatches to our clock ISR, which acks the
         * PIC and tail-calls _KeUpdateSystemTime. Without this the IDT
         * slot is KiUnexpectedInterrupt0 (from TRAP.ASM:DEFINE_EMPTY_
         * VECTORS) and every tick bugchecks.
         *
         * Post-Phase-0 the IDT layout is the native i386 interrupt-gate
         * format: { offset_lo, selector, access, offset_hi }.
         *   selector = KGDT_R0_CODE (0x08)
         *   access   = P=1, DPL=0, type=INT32 (0xE) → 0x8E00
         */
        {
            extern VOID HalpClockInterrupt(VOID);
            PKIDTENTRY idt = KeGetPcr()->IDT;
            ULONG addr = (ULONG)&HalpClockInterrupt;
            idt[CLOCK_VECTOR].Offset         = (USHORT)(addr & 0xFFFF);
            idt[CLOCK_VECTOR].Selector       = 0x08;   /* KGDT_R0_CODE */
            idt[CLOCK_VECTOR].Access         = 0x8E00;
            idt[CLOCK_VECTOR].ExtendedOffset = (USHORT)(addr >> 16);
        }

        /* Probe invariant TSC, calibrate frequency, latch boot-time
         * TSC + UEFI wall-clock seed.  Must run BEFORE we unmask IRQ0
         * below — the ISR derives KeUpdateSystemTime increments from
         * (current_tsc - HalpLastTickTsc) / HalpTscFrequency, both of
         * which HalpInitTscClock populates. */
        {
            extern VOID HalpInitTscClock(IN PLOADER_PARAMETER_BLOCK);
            HalpInitTscClock(LoaderBlock);
        }

        /* Bring up the LAPIC + IOAPIC.  Sets HalpApicPresent unless CPUID
         * reports no local APIC (then we stay on the 8259). */
        HalpInitApic();

        if (HalpApicPresent) {
            /* Install the LAPIC spurious-interrupt vector (0xFF) handler. */
            PKIDTENTRY idt = KeGetPcr()->IDT;
            ULONG sp = (ULONG)&HalpApicSpurious;
            idt[0xFF].Offset         = (USHORT)(sp & 0xFFFF);
            idt[0xFF].Selector       = 0x08;   /* KGDT_R0_CODE */
            idt[0xFF].Access         = 0x8E00;
            idt[0xFF].ExtendedOffset = (USHORT)(sp >> 16);

            /* Route the PIT clock to the CPU through the IOAPIC: on a PC the
             * timer (ISA IRQ0) is the override on GSI 2, edge/active-high.
             * The 8259 stays fully masked (from Phase 0) so nothing is
             * double-delivered. */
            HalpIoApicSetEntry(2, CLOCK_VECTOR, FALSE, FALSE, FALSE);
        } else {
            /* No APIC: drive the clock the legacy way — unmask IRQ0 (timer)
             * + IRQ2 (cascade) on the master 8259. */
            HalpWritePort(PIC1_DATA, 0xFA);  /* 11111010 = all but IRQ0+IRQ2 */
        }

        /* Tell the kernel our tick length (100000 * 100ns = 10ms).
         * Without this, KeTimeAdjustment stays 0 → KeUpdateSystemTime
         * increments SystemTime by 0 per tick → NtQuerySystemTime
         * returns a frozen 0, NtDelayExecution never wakes from its
         * timer wait, and everything timer-related silently breaks.
         * TickCountLow and InterruptTime advance fine without this —
         * they use separate counters — which is why the symptom only
         * surfaces when something reads SystemTime. */
        {
            extern VOID KeSetTimeIncrement(ULONG Max, ULONG Min);
            KeSetTimeIncrement(100000, 100000);
        }

        HalpSerialPrint("HAL: Phase 1 complete (clock enabled)\r\n");

        return TRUE;
    }

    return TRUE;
}
