/*
 * stubs.c - MicroNT HAL stub functions
 *
 * Correct signatures from HAL.H / NTDDK.H.
 * These return success/zero to keep the kernel happy during early boot.
 */

#define _NTSYSTEM_
#include "halp.h"
#include "ntdddisk.h"
#include "ntddft.h"

/* ===== Stall / Performance ===== */

VOID
KeStallExecutionProcessor(
    IN ULONG Microseconds
    )
{
    volatile ULONG i;
    for (i = 0; i < Microseconds; i++) {
        HalpReadPort(0x80);     /* ~1us per port read */
    }
}

/*
 * TSC-based performance counter. The x86 TSC (CPUID 0x01.EDX bit 4)
 * monotonically increments every CPU cycle on post-Pentium hardware
 * and is virtualised by QEMU. On modern CPUs it's "invariant" (CPUID
 * 0x80000007.EDX bit 8) so the rate is constant regardless of P-state
 * — we trust that since our only target is QEMU.
 *
 * Frequency is discovered on the first call via a PIT-channel-2 gated
 * busy-poll (~55ms blocking once); subsequent calls are lock-free
 * reads of the cached value.
 */

static ULONGLONG HalpTscFrequency = 0;    /* Hz; 0 = not yet calibrated */

static ULONGLONG
HalpReadTsc(VOID)
{
    ULONG lo, hi;
    /* CL 8.50's inline assembler predates the Pentium's RDTSC mnemonic,
     * so hand-emit the two opcode bytes (0F 31). Result is in EDX:EAX. */
    _asm { _emit 0x0F }
    _asm { _emit 0x31 }
    _asm { mov lo, eax }
    _asm { mov hi, edx }
    return ((ULONGLONG)hi << 32) | lo;
}

/*
 * Calibrate TSC against PIT channel 2 without disturbing channel 0
 * (our IRQ0 source). Channel 2's output gates the PC speaker via
 * port 0x61 bit 5 (OUT2), so we can poll it without wiring an IRQ.
 *
 *   1. Program channel 2, mode 0 (one-shot), count = 0xFFFF → ~55ms
 *      at 1.193182 MHz.
 *   2. Enable gate 2 (port 0x61 bit 0), keep speaker disabled (bit 1).
 *   3. Read TSC, poll port 0x61 bit 5 until OUT2 rises (count expired),
 *      read TSC again.
 *   4. freq = TSC_delta * PIT_Hz / PIT_count.
 */
static VOID
HalpCalibrateTscViaPIT(VOID)
{
    const ULONG pit_count = 0xFFFF;
    const ULONG pit_hz    = 1193182;
    UCHAR gate_saved;
    ULONGLONG start, end, delta;

    HalpWritePort(0x43, 0xB0);                      /* ch2, lo/hi, mode 0 */
    HalpWritePort(0x42, (UCHAR)(pit_count & 0xFF));
    HalpWritePort(0x42, (UCHAR)(pit_count >> 8));

    gate_saved = HalpReadPort(0x61);
    HalpWritePort(0x61, (UCHAR)((gate_saved & ~0x02) | 0x01));

    start = HalpReadTsc();
    while (!(HalpReadPort(0x61) & 0x20)) { /* spin */ }
    end = HalpReadTsc();

    HalpWritePort(0x61, gate_saved);

    delta = end - start;
    HalpTscFrequency = (delta * pit_hz) / pit_count;
}

/*
 * CPUID wrapper.  CL 8.50 doesn't recognise the cpuid mnemonic, so
 * hand-emit 0x0F 0xA2 like RDTSC above.  __stdcall preserves EBX
 * across calls, but CPUID clobbers it — push/pop locally to be
 * defensive against compiler scheduling assumptions.  Pass NULL for
 * any output you don't care about.
 */
VOID
HalpCpuid(
    IN  ULONG  Leaf,
    IN  ULONG  Subleaf,
    OUT PULONG OutEax,
    OUT PULONG OutEbx,
    OUT PULONG OutEcx,
    OUT PULONG OutEdx
    )
{
    ULONG a, b, c, d;
    _asm {
        push ebx
        mov  eax, Leaf
        mov  ecx, Subleaf
        _emit 0x0F
        _emit 0xA2
        mov  a, eax
        mov  b, ebx
        mov  c, ecx
        mov  d, edx
        pop  ebx
    }
    if (OutEax) *OutEax = a;
    if (OutEbx) *OutEbx = b;
    if (OutEcx) *OutEcx = c;
    if (OutEdx) *OutEdx = d;
}

/*
 * Wall-clock plumbing.
 *
 * boot-efi reads gRT->GetTime() pre-handoff and stashes the result in
 * an arena-allocated EFI_TIME-shaped struct, with LoaderBlock->Spare1
 * pointing at it (KSEG0 VA).  HAL doesn't include efi.h; we declare
 * our own struct with the exact UEFI-spec layout (section 8.3) and
 * cast the pointer.
 *
 * At HAL init we convert the EFI_TIME → 100-ns since 1601 (NT zero
 * point) and latch HalpBootSystemTime alongside HalpBootTsc.  Live
 * wall-clock then derives as
 *     boot_system + (rdtsc - boot_tsc) * 10^7 / tsc_freq.
 *
 * Spare1 == 0 means RT->GetTime() failed in boot-efi; HAL leaves
 * HalpBootSystemTime.QuadPart at zero and HalQueryRealTimeClock
 * returns FALSE — graceful degradation to today's "1601" wall clock.
 */

typedef struct _HAL_EFI_TIME {
    USHORT Year;        /* 1900..9999 */
    UCHAR  Month;       /* 1..12 */
    UCHAR  Day;         /* 1..31 */
    UCHAR  Hour;        /* 0..23 */
    UCHAR  Minute;      /* 0..59 */
    UCHAR  Second;      /* 0..59 */
    UCHAR  Pad1;
    ULONG  Nanosecond;  /* 0..999_999_999 */
    SHORT  TimeZone;    /* minutes east of UTC; 0x07FF = unspecified */
    UCHAR  Daylight;
    UCHAR  Pad2;
} HAL_EFI_TIME, *PHAL_EFI_TIME;

#define HAL_EFI_UNSPECIFIED_TIMEZONE  ((SHORT)0x07FF)

static LARGE_INTEGER HalpBootSystemTime;     /* 100-ns since 1601 */
static ULONGLONG     HalpBootTsc;            /* TSC at HAL init */
ULONGLONG            HalpLastTickTsc;        /* updated by clock ISR */

/*
 * Convert one HAL_EFI_TIME into NT system time (100-ns since 1601).
 * Returns FALSE when the seed is unset/invalid; caller treats that
 * as "no UEFI seed".
 */
static BOOLEAN
HalpEfiTimeToSystemTime(
    IN  PHAL_EFI_TIME EfiTime,
    OUT PLARGE_INTEGER SystemTime
    )
{
    TIME_FIELDS    tf;
    LARGE_INTEGER  local;
    LONG           tz_min;

    if (EfiTime == NULL || EfiTime->Year == 0) {
        return FALSE;
    }

    tf.Year         = (CSHORT)EfiTime->Year;
    tf.Month        = (CSHORT)EfiTime->Month;
    tf.Day          = (CSHORT)EfiTime->Day;
    tf.Hour         = (CSHORT)EfiTime->Hour;
    tf.Minute       = (CSHORT)EfiTime->Minute;
    tf.Second       = (CSHORT)EfiTime->Second;
    tf.Milliseconds = (CSHORT)(EfiTime->Nanosecond / 1000000);
    tf.Weekday      = 0;                   /* RtlTimeFieldsToTime ignores this */

    if (!RtlTimeFieldsToTime(&tf, &local)) {
        return FALSE;
    }

    /* EFI_TIME.TimeZone: minutes east of UTC.  EFI_UNSPECIFIED_TIMEZONE
     * (0x07FF) → treat as UTC; qemu / EC2 / GCE / Azure all return
     * UTC explicitly so this branch is the common case.  Otherwise
     * convert local→UTC by subtracting tz_min*60 seconds. */
    {
        LONGLONG tz_100ns;
        tz_min   = (EfiTime->TimeZone == HAL_EFI_UNSPECIFIED_TIMEZONE)
                   ? 0 : (LONG)EfiTime->TimeZone;
        tz_100ns = (LONGLONG)tz_min * 60;
        tz_100ns = tz_100ns * 10000000;
        SystemTime->QuadPart = local.QuadPart - tz_100ns;
    }
    return TRUE;
}

/*
 * Log the hypervisor identity to the serial banner.  Bare metal logs
 * "bare metal".  When CPUID 01h:ECX bit 31 (HypervisorPresent) is
 * set, we read the 12-byte ASCII vendor signature from CPUID
 * 40000000h:EBX/ECX/EDX — this matches what every hypervisor publishes:
 *   KVMKVMKVM\0\0\0   KVM (qemu, EC2 Nitro, GCE, Linode, etc.)
 *   Microsoft Hv      Hyper-V (Azure, on-prem Hyper-V)
 *   TCGTCGTCGTCG      QEMU TCG (no KVM)
 *   VMwareVMware      VMware ESXi / Workstation
 *   XenVMMXenVMM      Xen
 *   bhyve bhyve       FreeBSD bhyve
 * Diagnostic only — informs which PV-clock path we'd take if/when
 * we add one (kvmclock under KVMKVMKVM, Reference TSC page under
 * Microsoft Hv, etc.).
 */
static VOID
HalpLogHypervisorSignature(VOID)
{
    ULONG ecx_hv = 0, eax_max = 0, ebx = 0, ecx = 0, edx = 0;
    CHAR  msg[64];
    ULONG i, k;

    HalpCpuid(0x00000001, 0, NULL, NULL, &ecx_hv, NULL);
    if (!(ecx_hv & 0x80000000)) {
        HalpSerialPrint("HAL: hypervisor: bare metal\r\n");
        return;
    }

    HalpCpuid(0x40000000, 0, &eax_max, &ebx, &ecx, &edx);

    i = 0;
    msg[i++] = 'H'; msg[i++] = 'A'; msg[i++] = 'L'; msg[i++] = ':';
    msg[i++] = ' '; msg[i++] = 'h'; msg[i++] = 'y'; msg[i++] = 'p';
    msg[i++] = 'e'; msg[i++] = 'r'; msg[i++] = 'v'; msg[i++] = 'i';
    msg[i++] = 's'; msg[i++] = 'o'; msg[i++] = 'r'; msg[i++] = ':';
    msg[i++] = ' ';
    msg[i++] = (CHAR)(ebx & 0xFF);
    msg[i++] = (CHAR)((ebx >>  8) & 0xFF);
    msg[i++] = (CHAR)((ebx >> 16) & 0xFF);
    msg[i++] = (CHAR)((ebx >> 24) & 0xFF);
    msg[i++] = (CHAR)(ecx & 0xFF);
    msg[i++] = (CHAR)((ecx >>  8) & 0xFF);
    msg[i++] = (CHAR)((ecx >> 16) & 0xFF);
    msg[i++] = (CHAR)((ecx >> 24) & 0xFF);
    msg[i++] = (CHAR)(edx & 0xFF);
    msg[i++] = (CHAR)((edx >>  8) & 0xFF);
    msg[i++] = (CHAR)((edx >> 16) & 0xFF);
    msg[i++] = (CHAR)((edx >> 24) & 0xFF);

    /* Signatures like "KVMKVMKVM\0\0\0" end in NUL bytes — replace
     * with spaces so the serial line stays a printable run. */
    for (k = 17; k < i; k++) {
        if (msg[k] == 0) msg[k] = ' ';
    }

    msg[i++] = '\r';
    msg[i++] = '\n';
    msg[i]   = 0;
    HalpSerialPrint(msg);
}

/*
 * One-shot clock setup called from HalInitSystem(Phase 1).  Logs the
 * hypervisor identity, probes invariant TSC (advisory; we trust TSC
 * regardless because the bit is masked by qemu64 even on hosts that
 * support it), eagerly calibrates frequency against the PIT so the
 * clock ISR doesn't recalibrate from inside an interrupt, latches
 * the boot-time TSC + last-tick anchor, and converts the UEFI
 * wall-clock seed into HalpBootSystemTime.
 */
VOID
HalpInitTscClock(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    ULONG cpuid_edx = 0;

    HalpLogHypervisorSignature();

    /* Probe invariant-TSC bit for diagnostics only.  We don't bug-check
     * on absence: the default qemu64 model (under both TCG and KVM,
     * unless `-cpu host` is passed) doesn't advertise the bit even
     * though its emulated TSC is rate-stable in practice, and a lot
     * of low-end VPS providers run our image under a similar setup.
     * The HAL's TSC consumers just trust the calibrated frequency
     * regardless. */
    HalpCpuid(0x80000007, 0, NULL, NULL, NULL, &cpuid_edx);
    if (cpuid_edx & (1 << 8)) {
        HalpSerialPrint("HAL: invariant TSC advertised\r\n");
    } else {
        HalpSerialPrint("HAL: invariant TSC bit absent — trusting TSC anyway\r\n");
    }

    HalpCalibrateTscViaPIT();
    HalpBootTsc     = HalpReadTsc();
    HalpLastTickTsc = HalpBootTsc;

    HalpBootSystemTime.QuadPart = 0;
    if (LoaderBlock->Spare1 != 0) {
        PHAL_EFI_TIME efi = (PHAL_EFI_TIME)LoaderBlock->Spare1;
        if (HalpEfiTimeToSystemTime(efi, &HalpBootSystemTime)) {
            HalpSerialPrint("HAL: UEFI wall-clock seed: ok\r\n");
        } else {
            HalpSerialPrint("HAL: UEFI wall-clock seed: invalid\r\n");
        }
    } else {
        HalpSerialPrint("HAL: no UEFI wall-clock seed\r\n");
    }
}

LARGE_INTEGER
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL
    )
{
    LARGE_INTEGER li;
    ULONGLONG tsc;

    if (HalpTscFrequency == 0) {
        HalpCalibrateTscViaPIT();
    }

    tsc = HalpReadTsc();
    li.LowPart  = (ULONG)tsc;
    li.HighPart = (LONG)(tsc >> 32);

    if (PerformanceFrequency) {
        PerformanceFrequency->LowPart  = (ULONG)HalpTscFrequency;
        PerformanceFrequency->HighPart = (LONG)(HalpTscFrequency >> 32);
    }
    return li;
}

VOID
KeFlushWriteBuffer(VOID)
{
}

/* ===== Timer / Clock / Profile ===== */

ULONG
HalSetTimeIncrement(
    IN ULONG DesiredIncrement
    )
{
    HalpSerialPrint("HAL: SetTimeIncrement\r\n");
    return 100000;  /* 10ms in 100ns units */
}

VOID
HalCalibratePerformanceCounter(
    IN volatile PLONG Number
    )
{
}

VOID
HalStartProfileInterrupt(
    IN ULONG Reserved
    )
{
}

VOID
HalStopProfileInterrupt(
    IN ULONG Reserved
    )
{
}

ULONG
HalSetProfileInterval(
    IN ULONG Interval
    )
{
    return Interval;
}

/* ===== RTC ===== */

/*
 * Live wall-clock query.  Returns boot system time plus the elapsed
 * 100-ns interval derived from the TSC (which is invariant on every
 * platform we target, see HalpInitTscClock).  Returns FALSE only
 * when there's no UEFI seed at all — graceful fallback to today's
 * 1601 zero-time behaviour.
 *
 * 64-bit arithmetic note: `dt_tsc * 10^7` overflows ULONGLONG after
 * ~1844 seconds at a 1 GHz TSC.  Split the multiply across the
 * divide to keep the intermediate in ULONGLONG range:
 *     q = dt / freq;  r = dt % freq;
 *     dt_100ns = q * 1e7 + (r * 1e7) / freq.
 */
BOOLEAN
HalQueryRealTimeClock(
    OUT PTIME_FIELDS TimeFields
    )
{
    ULONGLONG     dt_tsc, q, r, dt_100ns;
    LARGE_INTEGER live;

    if (HalpBootSystemTime.QuadPart == 0 || HalpTscFrequency == 0) {
        return FALSE;
    }

    dt_tsc   = HalpReadTsc() - HalpBootTsc;
    q        = dt_tsc / HalpTscFrequency;
    r        = dt_tsc % HalpTscFrequency;
    dt_100ns = q * (ULONGLONG)10000000
             + (r * (ULONGLONG)10000000) / HalpTscFrequency;

    live.QuadPart = HalpBootSystemTime.QuadPart + (LONGLONG)dt_100ns;
    RtlTimeToTimeFields(&live, TimeFields);
    return TRUE;
}

BOOLEAN
HalSetRealTimeClock(
    IN PTIME_FIELDS TimeFields
    )
{
    /* Read-only on virtual platforms.  UEFI runtime services are gone
     * post-handoff and we explicitly don't poke CMOS. */
    return FALSE;
}

/*
 * Called from HalpClockInterrupt (clock.asm) on every IRQ0 tick to
 * compute the per-tick increment passed to KeUpdateSystemTime.
 *
 * Returns the elapsed 100-ns interval since the previous tick, derived
 * from the TSC delta.  Replaces the original fixed 100000 (10 ms ×
 * 1e4) so KeSystemTime tracks invariant TSC accuracy (~1 ppm under
 * a hypervisor) rather than PIT crystal drift (~50 ppm).
 *
 * Capped at 0xFFFFFFFF on the (extremely unlikely) chance of a TSC
 * glitch — a hostile host could otherwise pass garbage into
 * KeUpdateSystemTime and skew SystemTime arbitrarily.
 *
 * Runs at CLOCK2_LEVEL (we're inside the ISR), so no spinlock needed
 * even on SMP — though we're UP-only today.
 */
ULONG
HalpClockTickIncrement(VOID)
{
    ULONGLONG now, dt_tsc, q, r, dt_100ns;

    /* Fall back to the original fixed increment until calibration is
     * complete (HalpInitTscClock runs in Phase 1 before we unmask
     * IRQ0, so this branch shouldn't fire in practice — defensive). */
    if (HalpTscFrequency == 0) {
        return 100000;
    }

    now              = HalpReadTsc();
    dt_tsc           = now - HalpLastTickTsc;
    HalpLastTickTsc  = now;
    q                = dt_tsc / HalpTscFrequency;
    r                = dt_tsc % HalpTscFrequency;
    dt_100ns = q * (ULONGLONG)10000000
             + (r * (ULONGLONG)10000000) / HalpTscFrequency;

    if (dt_100ns > (ULONGLONG)0xFFFFFFFF) {
        return 0xFFFFFFFF;
    }
    return (ULONG)dt_100ns;
}

/* ===== Bus / Resources ===== */

/* HalTranslateBusAddress, HalGetBusData, HalSetBusData,
 * HalAdjustResourceList, HalAssignSlotResources are now
 * provided by ixbusdat.c (bus handler dispatch). */

/* IDT usage tracking (referenced by ixsysbus.c) — stub for now */
IDTUsage HalpIDTUsage[256] = {0};

/* Resource list limits (referenced by ixpciint.c) — no ISA limits */
NTSTATUS
HalpAdjustResourceListLimits(
    IN PBUSHANDLER BusHandler,
    IN PBUSHANDLER RootHandler,
    IN OUT PIO_RESOURCE_REQUIREMENTS_LIST *pResourceList,
    IN ULONG MinimumMemoryAddress,
    IN ULONG MaximumMemoryAddress,
    IN ULONG MinimumPrefetchMemoryAddress,
    IN ULONG MaximumPrefetchMemoryAddress,
    IN BOOLEAN LimitedIO,
    IN ULONG MinimumIoAddress,
    IN ULONG MaximumIoAddress,
    IN PUCHAR IrqTable,
    IN ULONG IrqTableSize,
    IN ULONG MinimumDmaChannel,
    IN ULONG MaximumDmaChannel
    )
{
    return STATUS_SUCCESS;
}

extern VOID HalpReportPs2Devices(VOID);

VOID
HalReportResourceUsage(VOID)
{
    HalpSerialPrint("HAL: ReportResourceUsage\r\n");
    HalpInitializePciBus();
    HalpReportPs2Devices();
}

/* HalGetInterruptVector is in ixintr.c */

/* DMA primitives moved to dma.c (HalGetAdapter / HalAllocateCommonBuffer
 * / HalAllocateAdapterChannel / IoMapTransfer / IoFlushAdapterBuffers /
 * IoFreeAdapterChannel / IoFreeMapRegisters / HalReadDmaCounter /
 * HalAllocateCrashDumpRegisters / HalFlushCommonBuffer / HalFreeCommonBuffer). */

/* ===== IO Manager partition stubs ===== */

/*
 * IoAssignDriveLetters - minimal MicroNT implementation.
 *
 * The standard NT 3.5 routine walks LoaderBlock->ArcDiskInformation,
 * reads partition tables, and assigns letters across multiple disks.
 * MicroNT boots from a single FAT16 partition (the IDE volume QEMU
 * exposes), and the only consumer of drive-letter resolution today
 * is the toolchain we run under self-host.  So we just create
 *
 *     \DosDevices\C: -> <NtDeviceName>
 *
 * where NtDeviceName is the boot device the kernel passes us
 * (typically "\Device\Harddisk0\Partition1").  Without this symlink,
 * Win32 children's CreateFile("C:\...") - which goes through
 * RtlDosPathNameToNtPathName_U + the "\DosDevices\" prefix - cannot
 * resolve, breaking fopen / GetCurrentDirectory / GetModuleFileName
 * for any Win32 toolchain process.
 *
 * NtSystemPath / NtSystemPathString are already populated by INIT.C
 * (which sprintf's "C:%s" + LoaderBlock->NtBootPathName before we
 * run); leaving the OUT params untouched is correct.
 */
VOID
IoAssignDriveLetters(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PSTRING NtDeviceName,
    OUT PUCHAR NtSystemPath,
    OUT PSTRING NtSystemPathString
    )
{
    UNICODE_STRING linkName;
    UNICODE_STRING targetName;
    ANSI_STRING    ansiTarget;
    NTSTATUS       status;

    /* NtDeviceName is ANSI (PSTRING).  Convert to UNICODE_STRING for
     * IoCreateSymbolicLink.  RtlAnsiStringToUnicodeString allocates
     * the wide buffer when the third arg is TRUE; we free it below. */
    ansiTarget.Buffer        = NtDeviceName->Buffer;
    ansiTarget.Length        = NtDeviceName->Length;
    ansiTarget.MaximumLength = NtDeviceName->MaximumLength;

    status = RtlAnsiStringToUnicodeString(&targetName, &ansiTarget, TRUE);
    if (!NT_SUCCESS(status)) {
        return;
    }

    /* \DosDevices\C: -> NtDeviceName.  Errors (already-exists,
     * out-of-pool) are ignored — there's no recovery path here, and a
     * missing symlink will surface as a CreateFile failure later. */
    RtlInitUnicodeString(&linkName, L"\\DosDevices\\C:");
    (VOID) IoCreateSymbolicLink(&linkName, &targetName);

    RtlFreeUnicodeString(&targetName);
}

NTSTATUS
IoReadPartitionTable(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN BOOLEAN ReturnRecognizedPartitions,
    OUT PDRIVE_LAYOUT_INFORMATION *PartitionBuffer
    )
{
#define GET_STARTING_SECTOR( p ) (                  \
    RtlConvertUlongToLargeInteger(                  \
        (ULONG) (p->StartingSectorLsb0) +           \
        (ULONG) (p->StartingSectorLsb1 << 8) +      \
        (ULONG) (p->StartingSectorMsb0 << 16) +     \
        (ULONG) (p->StartingSectorMsb1 << 24) ) )

#define GET_PARTITION_LENGTH( p ) (                 \
    RtlConvertUlongToLargeInteger(                  \
        (ULONG) (p->PartitionLengthLsb0) +          \
        (ULONG) (p->PartitionLengthLsb1 << 8) +     \
        (ULONG) (p->PartitionLengthMsb0 << 16) +    \
        (ULONG) (p->PartitionLengthMsb1 << 24) ) )

#define ADD( a, b ) ( RtlLargeIntegerAdd( a, b ) )
#define SUBTRACT( a, b ) ( RtlLargeIntegerSubtract( a, b ) )
#define MULTIPLY( a, b ) ( RtlExtendedIntegerMultiply( a, (LONG) b ) )

    ULONG partitionBufferSize = PARTITION_BUFFER_SIZE;
    PDRIVE_LAYOUT_INFORMATION newPartitionBuffer = NULL;
    LARGE_INTEGER partitionTableOffset;
    LARGE_INTEGER volumeStartOffset;
    LARGE_INTEGER tempSize;
    BOOLEAN primaryPartitionTable;
    LONG partitionNumber;
    PUCHAR readBuffer = (PUCHAR) NULL;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIRP irp;
    PPARTITION_DESCRIPTOR partitionTableEntry;
    CCHAR partitionEntry;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG readSize;
    PPARTITION_INFORMATION partitionInfo;

    *PartitionBuffer = ExAllocatePool( NonPagedPool, partitionBufferSize );
    if (*PartitionBuffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    readSize = (SectorSize >= 512) ? SectorSize : 512;
    partitionTableOffset = RtlConvertUlongToLargeInteger( 0 );
    primaryPartitionTable = TRUE;
    volumeStartOffset = partitionTableOffset;
    partitionNumber = -1;

    readBuffer = ExAllocatePool( NonPagedPoolCacheAligned, PAGE_SIZE );
    if (readBuffer == NULL) {
        ExFreePool( *PartitionBuffer );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    do {
        DbgPrint("IoReadPartitionTable: read offset=0x%x\n",
                 partitionTableOffset.LowPart);
        KeInitializeEvent( &event, NotificationEvent, FALSE );

        irp = IoBuildSynchronousFsdRequest( IRP_MJ_READ,
                                            DeviceObject,
                                            readBuffer,
                                            readSize,
                                            &partitionTableOffset,
                                            &event,
                                            &ioStatus );

        status = IoCallDriver( DeviceObject, irp );
        DbgPrint("IoReadPartitionTable: IoCallDriver rs=0x%x\n", status);

        if (status == STATUS_PENDING) {
            (VOID) KeWaitForSingleObject( &event, Executive, KernelMode,
                                          FALSE, (PLARGE_INTEGER) NULL );
            status = ioStatus.Status;
            DbgPrint("IoReadPartitionTable: wait done rs=0x%x\n", status);
        }

        if (!NT_SUCCESS( status )) {
            break;
        }

        if (((PUSHORT) readBuffer)[BOOT_SIGNATURE_OFFSET] != BOOT_RECORD_SIGNATURE) {
            break;
        }

        if (RtlLargeIntegerEqualToZero( partitionTableOffset )) {
            (*PartitionBuffer)->Signature =
                ((PULONG) readBuffer)[PARTITION_TABLE_OFFSET/2-1];
        }

        partitionTableEntry = (PPARTITION_DESCRIPTOR)
            &(((PUSHORT) readBuffer)[PARTITION_TABLE_OFFSET]);

        for (partitionEntry = 1;
             partitionEntry <= NUM_PARTITION_TABLE_ENTRIES;
             partitionEntry++, partitionTableEntry++) {

            if (ReturnRecognizedPartitions) {
                if ((partitionTableEntry->PartitionType == PARTITION_ENTRY_UNUSED) ||
                    (partitionTableEntry->PartitionType == PARTITION_EXTENDED)) {
                    continue;
                }
            }

            partitionNumber++;

            if (((partitionNumber * sizeof( PARTITION_INFORMATION )) +
                 sizeof( DRIVE_LAYOUT_INFORMATION )) > (ULONG) partitionBufferSize) {
                newPartitionBuffer = ExAllocatePool( NonPagedPool,
                                                     partitionBufferSize << 1 );
                if (newPartitionBuffer == NULL) {
                    --partitionNumber;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                RtlCopyMemory( newPartitionBuffer, *PartitionBuffer,
                               partitionBufferSize );
                ExFreePool( *PartitionBuffer );
                *PartitionBuffer = newPartitionBuffer;
                partitionBufferSize <<= 1;
            }

            partitionInfo = &(*PartitionBuffer)->PartitionEntry[partitionNumber];
            partitionInfo->PartitionType = partitionTableEntry->PartitionType;
            partitionInfo->RewritePartition = FALSE;

            if (partitionTableEntry->PartitionType != PARTITION_ENTRY_UNUSED) {
                partitionInfo->BootIndicator =
                    partitionTableEntry->ActiveFlag & PARTITION_ACTIVE_FLAG ?
                        (BOOLEAN) TRUE : (BOOLEAN) FALSE;

                if (partitionTableEntry->PartitionType == PARTITION_EXTENDED) {
                    partitionInfo->RecognizedPartition = FALSE;
                } else {
                    partitionInfo->RecognizedPartition = TRUE;
                }

                tempSize = ADD( MULTIPLY( GET_STARTING_SECTOR( partitionTableEntry ),
                                          SectorSize ),
                                partitionTableOffset );
                partitionInfo->StartingOffset = tempSize;

                tempSize = MULTIPLY( GET_PARTITION_LENGTH( partitionTableEntry ),
                                     SectorSize );
                partitionInfo->PartitionLength = tempSize;

                tempSize = SUBTRACT( partitionInfo->StartingOffset,
                                     partitionTableOffset );
                partitionInfo->HiddenSectors =
                    LiDiv( tempSize, LiFromUlong( SectorSize ) ).LowPart;
            } else {
                partitionInfo->BootIndicator = FALSE;
                partitionInfo->RecognizedPartition = FALSE;
                partitionInfo->StartingOffset = RtlConvertLongToLargeInteger( 0 );
                partitionInfo->PartitionLength = RtlConvertLongToLargeInteger( 0 );
                partitionInfo->HiddenSectors = 0;
            }
        }

        if (!NT_SUCCESS( status )) {
            break;
        }

        partitionTableEntry = (PPARTITION_DESCRIPTOR)
            &(((PUSHORT) readBuffer)[PARTITION_TABLE_OFFSET]);
        partitionTableOffset = RtlConvertUlongToLargeInteger( 0 );

        for (partitionEntry = 1;
             partitionEntry <= NUM_PARTITION_TABLE_ENTRIES;
             partitionEntry++, partitionTableEntry++) {

            if (partitionTableEntry->PartitionType == PARTITION_EXTENDED) {
                partitionTableOffset = ADD( volumeStartOffset,
                    MULTIPLY( GET_STARTING_SECTOR( partitionTableEntry ),
                              SectorSize ) );
                if (primaryPartitionTable) {
                    volumeStartOffset = partitionTableOffset;
                }
                break;
            }
        }

        primaryPartitionTable = FALSE;

    } while (partitionTableOffset.HighPart | partitionTableOffset.LowPart);

    DbgPrint("IoReadPartitionTable: loop exit, partitions=%d\n", partitionNumber + 1);
    (*PartitionBuffer)->PartitionCount = ++partitionNumber;

    if (!partitionNumber) {
        (*PartitionBuffer)->Signature = 0;
    }

    if (readBuffer != NULL) {
        ExFreePool( readBuffer );
    }

    return status;

#undef GET_STARTING_SECTOR
#undef GET_PARTITION_LENGTH
#undef ADD
#undef SUBTRACT
#undef MULTIPLY
}

NTSTATUS
IoSetPartitionInformation(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN ULONG PartitionNumber,
    IN ULONG PartitionType
    )
{
    return 0xC0000001;
}

NTSTATUS
IoWritePartitionTable(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    IN ULONG SectorsPerTrack,
    IN ULONG NumberOfHeads,
    IN struct _DRIVE_LAYOUT_INFORMATION *PartitionBuffer
    )
{
    return 0xC0000001;
}

/* ===== Misc ===== */

BOOLEAN
HalAllProcessorsStarted(VOID)
{
    HalpSerialPrint("HAL: AllProcessorsStarted\r\n");
    return TRUE;
}

BOOLEAN
HalStartNextProcessor(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PKPROCESSOR_STATE ProcessorState
    )
{
    return FALSE;
}

VOID
HalReturnToFirmware(
    IN FIRMWARE_REENTRY Routine
    )
{
    /* A 6-byte IDTR image (limit 0, base 0): loading it makes every
     * exception undeliverable, so the next fault escalates straight to a
     * triple fault -> CPU reset. */
    static UCHAR NullIdt[6] = { 0, 0, 0, 0, 0, 0 };

    HalpSerialPrint("HAL: ReturnToFirmware!\r\n");

    /* Legacy + modern I/O reset paths: the 8042 pulse resets pc/q35 (and
     * real PCs) immediately, so we never fall past it there.  microvm has
     * neither an 8042 nor a 0xCF9 reset register, so both are no-ops and we
     * fall through to the triple fault below. */
    HalpWritePort(0x64,  0xFE);    /* 8042 keyboard-controller CPU reset */
    HalpWritePort(0xCF9, 0x0E);    /* ICH/PCH reset control register */

    /* Universal fallback (incl. microvm): zero-length IDT + an exception
     * -> triple fault -> reset.  qemu with -no-reboot exits here. */
    _asm {
        cli
        lidt    fword ptr NullIdt
        int     3
    }

    _asm { cli }
    _asm { hlt }
}

BOOLEAN
HalMakeBeep(
    IN ULONG Frequency
    )
{
    return FALSE;
}

VOID
HalProcessorIdle(VOID)
{
    _asm { sti }
    _asm { hlt }
}

ARC_STATUS
HalGetEnvironmentVariable(
    IN PCHAR Variable,
    IN USHORT Length,
    OUT PCHAR Buffer
    )
{
    return 1;  /* ENOMEM */
}

ARC_STATUS
HalSetEnvironmentVariable(
    IN PCHAR Variable,
    IN PCHAR Value
    )
{
    return 1;  /* ENOMEM */
}

VOID
HalRequestIpi(
    IN ULONG Mask
    )
{
}

/* HalHandleNMI is in ixintr.c */

/* ===== KD port (kernel debugger serial) ===== */

ULONG KdComPortInUse = 0;

BOOLEAN
KdPortInitialize(
    IN PDEBUG_PARAMETERS DebugParameters,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN BOOLEAN Initialize
    )
{
    HalpSerialPrint(Initialize ? "HAL: KdPortInitialize(TRUE)\r\n"
                               : "HAL: KdPortInitialize(FALSE)\r\n");
    if (Initialize) {
        /* Initialize COM1 for KD communication */
        HalpWritePort(COM1_PORT + 1, 0x00);  /* Disable interrupts */
        HalpWritePort(COM1_PORT + 3, 0x80);  /* DLAB on */
        HalpWritePort(COM1_PORT + 0, 0x01);  /* 115200 baud */
        HalpWritePort(COM1_PORT + 1, 0x00);
        HalpWritePort(COM1_PORT + 3, 0x03);  /* 8N1, DLAB off */
        HalpWritePort(COM1_PORT + 2, 0xC7);  /* Enable FIFO */
        HalpWritePort(COM1_PORT + 4, 0x0B);  /* DTR + RTS + OUT2 */
        HalpSerialPrint("HAL: COM1 initialized for KD\r\n");
    }
    return Initialize;
}

static ULONG KdpGetByteCount = 0;

ULONG
KdPortGetByte(
    OUT PUCHAR Input
    )
{
    /* Poll COM1 for up to ~1 second (matching kernel's timeout expectation).
     * The kernel's KdpReceivePacketLeader assumes each KdPortGetByte call
     * waits before returning NODATA. Without polling, ACK packets from
     * the debugger arrive after the kernel has already timed out. */
    ULONG i;
    for (i = 0; i < 1000000; i++) {
        if (HalpReadPort(COM1_PORT + 5) & 0x01) {
            *Input = HalpReadPort(COM1_PORT);
            KdpGetByteCount++;
            if (KdpGetByteCount <= 64) {
                /* Trace received bytes to COM2 as hex */
                CHAR hex[8];
                UCHAR nibh = (*Input >> 4) & 0x0F;
                UCHAR nibl = *Input & 0x0F;
                hex[0] = nibh < 10 ? '0'+nibh : 'A'+nibh-10;
                hex[1] = nibl < 10 ? '0'+nibl : 'A'+nibl-10;
                hex[2] = ' ';
                hex[3] = '\0';
                HalpSerialPrint(hex);
            }
            return 1;  /* CP_GET_SUCCESS */
        }
        HalpReadPort(0x80);  /* ~1us delay */
    }
    return 0;  /* CP_GET_NODATA */
}

ULONG
KdPortPollByte(
    OUT PUCHAR Input
    )
{
    return KdPortGetByte(Input);
}

VOID
KdPortPutByte(
    IN UCHAR Output
    )
{
    /* KD uses COM1 directly — not HalpSerialPutChar which is on COM2 */
    while (!(HalpReadPort(COM1_PORT + 5) & 0x20))
        ;
    HalpWritePort(COM1_PORT, Output);
}

VOID KdPortRestore(VOID) {}
VOID KdPortSave(VOID) {}

/* ===== Port I/O ===== */

UCHAR
READ_PORT_UCHAR(
    IN PUCHAR Port
    )
{
    return HalpReadPort((USHORT)(ULONG)Port);
}

USHORT
READ_PORT_USHORT(
    IN PUSHORT Port
    )
{
    USHORT val; USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { in ax, dx }
    _asm { mov val, ax }
    return val;
}

ULONG
READ_PORT_ULONG(
    IN PULONG Port
    )
{
    ULONG val; USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { in eax, dx }
    _asm { mov val, eax }
    return val;
}

VOID
WRITE_PORT_UCHAR(
    IN PUCHAR Port,
    IN UCHAR Value
    )
{
    HalpWritePort((USHORT)(ULONG)Port, Value);
}

VOID
WRITE_PORT_USHORT(
    IN PUSHORT Port,
    IN USHORT Value
    )
{
    USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { mov ax, Value }
    _asm { out dx, ax }
}

VOID
WRITE_PORT_ULONG(
    IN PULONG Port,
    IN ULONG Value
    )
{
    USHORT p = (USHORT)(ULONG)Port;
    _asm { mov dx, p }
    _asm { mov eax, Value }
    _asm { out dx, eax }
}

/* Buffer versions */
VOID READ_PORT_BUFFER_UCHAR(IN PUCHAR Port, IN PUCHAR Buffer, IN ULONG Count) {
    while (Count--) *Buffer++ = READ_PORT_UCHAR(Port);
}
VOID READ_PORT_BUFFER_USHORT(IN PUSHORT Port, IN PUSHORT Buffer, IN ULONG Count) {
    while (Count--) *Buffer++ = READ_PORT_USHORT(Port);
}
VOID READ_PORT_BUFFER_ULONG(IN PULONG Port, IN PULONG Buffer, IN ULONG Count) {
    while (Count--) *Buffer++ = READ_PORT_ULONG(Port);
}
VOID WRITE_PORT_BUFFER_UCHAR(IN PUCHAR Port, IN PUCHAR Buffer, IN ULONG Count) {
    while (Count--) WRITE_PORT_UCHAR(Port, *Buffer++);
}
VOID WRITE_PORT_BUFFER_USHORT(IN PUSHORT Port, IN PUSHORT Buffer, IN ULONG Count) {
    while (Count--) WRITE_PORT_USHORT(Port, *Buffer++);
}
VOID WRITE_PORT_BUFFER_ULONG(IN PULONG Port, IN PULONG Buffer, IN ULONG Count) {
    while (Count--) WRITE_PORT_ULONG(Port, *Buffer++);
}
