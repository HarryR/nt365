/*
 * apic.c - MicroNT HAL: Local APIC + I/O APIC interrupt delivery (i386)
 *
 * Replaces the pure-8259 delivery path (ixintr.c) so one image works on
 * machines where the 8259's INTR isn't wired to the CPU — e.g. qemu
 * microvm, where the legacy PIC's interrupt never reaches the core and
 * the clock tick is lost.  The LAPIC is the delivery conduit; the IOAPIC
 * is the router.
 *
 * Discovery is hardcoded to the architectural defaults (no ACPI):
 *   LAPIC  @ phys 0xFEE00000  -> VA 0xFFFE0000  (canonical NT HAL window)
 *   IOAPIC @ phys 0xFEC00000  -> VA 0xFFFE1000
 *   ISA IRQ0 -> IOAPIC GSI 2  (the standard PC timer override)
 * CPUID.01h:EDX[9] gates presence; absent -> HalpApicPresent stays FALSE
 * and ixintr.c keeps using the 8259 (graceful fallback for non-targets).
 */

#include "halp.h"

/* Reserved kernel VAs (the HAL's 0xFFC00000.. window; PCR/SUD sit lower). */
#define LAPIC_VA    0xFFFE0000UL
#define IOAPIC_VA   0xFFFE1000UL          /* implied: 2nd HalpMapPhysicalMemory */
#define LAPIC_PA    0xFEE00000UL
#define IOAPIC_PA   0xFEC00000UL

/* LAPIC register offsets (bytes). */
#define LAPIC_TPR   0x080
#define LAPIC_EOI   0x0B0
#define LAPIC_SVR   0x0F0
#define LAPIC_LINT0 0x350
#define LAPIC_LINT1 0x360

/* IOAPIC: indexed via IOREGSEL(0x00)/IOWIN(0x10); EOIR(0x40) is direct. */
#define IOAPIC_VER      0x01
#define IOAPIC_REDIR(n) (0x10 + 2 * (n))
#define IOAPIC_EOIR     0x40

/* Redirection-entry low-dword bits. */
#define RED_ACTIVE_LOW  0x00002000
#define RED_LEVEL       0x00008000
#define RED_MASKED      0x00010000

/* x86 PTE bits (raw, written through the self-map). */
#define PTE_VALID   0x1
#define PTE_WRITE   0x2
#define PTE_PWT     0x8
#define PTE_PCD     0x10

BOOLEAN HalpApicPresent = FALSE;

static volatile ULONG *HalpLapic  = 0;
static volatile ULONG *HalpIoApic = 0;   /* [0]=IOREGSEL [4]=IOWIN [16]=EOIR */
static ULONG           HalpNextMapVa = LAPIC_VA;

/*
 * HalpMapPhysicalMemory - map NumberPages of physical MMIO into the
 * reserved HAL VA window, uncached.  Writes PTEs directly through the
 * PD/PT self-map (MiGetPteAddress) so it works in early HalInitSystem
 * without depending on MM's pool allocators.
 */
PVOID
HalpMapPhysicalMemory(
    IN PVOID PhysicalAddress,
    IN ULONG NumberPages
    )
{
    ULONG pa  = (ULONG)PhysicalAddress & 0xFFFFF000;
    ULONG ofs = (ULONG)PhysicalAddress & 0xFFF;
    ULONG va  = HalpNextMapVa;
    ULONG i;

    for (i = 0; i < NumberPages; i++) {
        *(volatile ULONG *)MiGetPteAddress(va + (i << 12)) =
            (pa + (i << 12)) | PTE_VALID | PTE_WRITE | PTE_PWT | PTE_PCD;
    }
    HalpNextMapVa += NumberPages << 12;

    /* Reload CR3 to flush the TLB so the new mappings take effect. */
    _asm {
        mov eax, cr3
        mov cr3, eax
    }

    return (PVOID)(va + ofs);
}

static ULONG
IoApicRead(UCHAR reg)
{
    HalpIoApic[0] = reg;
    return HalpIoApic[4];
}

static VOID
IoApicWrite(UCHAR reg, ULONG val)
{
    HalpIoApic[0] = reg;
    HalpIoApic[4] = val;
}

/*
 * HalpIoApicSetEntry - program one redirection entry: fixed delivery,
 * physical destination = LAPIC id 0.  Trigger/polarity from the caller
 * (ISA = edge/high, PCI = level/low).
 */
VOID
HalpIoApicSetEntry(
    IN UCHAR   Gsi,
    IN UCHAR   Vector,
    IN BOOLEAN Level,
    IN BOOLEAN ActiveLow,
    IN BOOLEAN Masked
    )
{
    ULONG lo = Vector;
    if (ActiveLow) lo |= RED_ACTIVE_LOW;
    if (Level)     lo |= RED_LEVEL;
    if (Masked)    lo |= RED_MASKED;

    IoApicWrite((UCHAR)(IOAPIC_REDIR(Gsi) + 1), 0);   /* high: dest APIC 0 */
    IoApicWrite((UCHAR)(IOAPIC_REDIR(Gsi)), lo);
}

VOID
HalpIoApicMask(IN UCHAR Gsi, IN BOOLEAN Masked)
{
    ULONG lo = IoApicRead((UCHAR)IOAPIC_REDIR(Gsi));
    if (Masked) lo |= RED_MASKED; else lo &= ~(ULONG)RED_MASKED;
    IoApicWrite((UCHAR)IOAPIC_REDIR(Gsi), lo);
}

VOID HalpLapicEoi(VOID)            { HalpLapic[LAPIC_EOI / 4] = 0; }
VOID HalpIoApicEoi(UCHAR Vector)   { HalpIoApic[IOAPIC_EOIR / 4] = Vector; }

/*
 * HalpInitApic - probe + bring up the LAPIC and IOAPIC.  Called from
 * HalInitSystem Phase 1.  Leaves HalpApicPresent FALSE (8259 fallback)
 * if CPUID reports no local APIC.
 */
VOID
HalpInitApic(VOID)
{
    ULONG edx = 0;
    ULONG ver, maxredir, i;

    HalpCpuid(1, 0, NULL, NULL, NULL, &edx);
    if (!((edx >> 9) & 1)) {
        HalpSerialPrint("HAL: no local APIC (CPUID) - using 8259\r\n");
        return;
    }

    HalpLapic  = (volatile ULONG *)HalpMapPhysicalMemory((PVOID)LAPIC_PA, 1);
    HalpIoApic = (volatile ULONG *)HalpMapPhysicalMemory((PVOID)IOAPIC_PA, 1);

    /* LAPIC: TPR=0 (accept all), LINT0/LINT1 masked (we route via the
     * IOAPIC, not 8259 ExtINT/NMI), then software-enable via SVR with
     * spurious vector 0xFF.  The hardware enable (IA32_APIC_BASE.EN) is
     * already set at reset on every target. */
    HalpLapic[LAPIC_TPR   / 4] = 0;
    HalpLapic[LAPIC_LINT0 / 4] = RED_MASKED;
    HalpLapic[LAPIC_LINT1 / 4] = RED_MASKED;
    HalpLapic[LAPIC_SVR   / 4] = 0x100 | 0xFF;

    /* IOAPIC: mask every redirection entry up front; the clock (GSI2) and
     * each device IRQ are unmasked on demand (halinit.c / HalEnableSystem-
     * Interrupt). */
    ver      = IoApicRead(IOAPIC_VER);
    maxredir = (ver >> 16) & 0xFF;
    for (i = 0; i <= maxredir; i++) {
        HalpIoApicSetEntry((UCHAR)i, 0xFF, FALSE, FALSE, TRUE);
    }

    HalpApicPresent = TRUE;
    HalpSerialPrint("HAL: LAPIC + IOAPIC online\r\n");
}
