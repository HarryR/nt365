/*
 * time.c - MicroNT HAL clock / wall-clock / timekeeping
 *
 * Split out of stubs.c (which is for literal stubs only): the TSC
 * performance counter, the PIT-derived clock tick, the CMOS RTC reader,
 * hypervisor detection, and wall-clock epoch source selection
 * (UEFI / kvmclock / CMOS).  Signatures from HAL.H / NTDDK.H.
 */

#define _NTSYSTEM_
#include "halp.h"

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

/* PIT-counter wall-clock state.  HalpClockTickIncrement reads PIT
 * channel 0's down-counter on each ISR, computes the delta in PIT
 * clocks (1193182 Hz), and accumulates total elapsed PIT clocks
 * since boot.  HalQueryRealTimeClock reads the accumulator to
 * derive wall time from the UEFI-seeded boot system time. */
static USHORT    HalpLastPitCount = 0;
static BOOLEAN   HalpPitClockInit = FALSE;
static ULONGLONG HalpPitTicksSinceBoot = 0;

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

LARGE_INTEGER HalpBootSystemTime;            /* 100-ns since 1601; read by random.c */
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
 * Format an NT system time (100-ns since 1601) as
 * "<label>YYYY-MM-DD HH:MM:SS UTC" on the serial banner so the seed we
 * latched is visible at boot regardless of which source produced it.
 * No printf in the HAL — hand-roll the zero-padded fields.
 */
static VOID
HalpPrintWallClock(
    IN PCHAR         Label,
    IN LARGE_INTEGER SystemTime
    )
{
    static const CHAR d[] = "0123456789";
    TIME_FIELDS tf;
    CHAR        buf[96];
    ULONG       i = 0;
    PCHAR       s;

    RtlTimeToTimeFields(&SystemTime, &tf);

    for (s = Label; *s; s++) {
        buf[i++] = *s;
    }
    buf[i++] = d[(tf.Year   / 1000) % 10];
    buf[i++] = d[(tf.Year   /  100) % 10];
    buf[i++] = d[(tf.Year   /   10) % 10];
    buf[i++] = d[ tf.Year           % 10];
    buf[i++] = '-';
    buf[i++] = d[(tf.Month  /   10) % 10];
    buf[i++] = d[ tf.Month          % 10];
    buf[i++] = '-';
    buf[i++] = d[(tf.Day    /   10) % 10];
    buf[i++] = d[ tf.Day            % 10];
    buf[i++] = ' ';
    buf[i++] = d[(tf.Hour   /   10) % 10];
    buf[i++] = d[ tf.Hour           % 10];
    buf[i++] = ':';
    buf[i++] = d[(tf.Minute /   10) % 10];
    buf[i++] = d[ tf.Minute         % 10];
    buf[i++] = ':';
    buf[i++] = d[(tf.Second /   10) % 10];
    buf[i++] = d[ tf.Second         % 10];
    buf[i++] = ' '; buf[i++] = 'U'; buf[i++] = 'T'; buf[i++] = 'C';
    buf[i++] = '\r'; buf[i++] = '\n'; buf[i] = 0;
    HalpSerialPrint(buf);
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
/* ---------------- Wall-clock / epoch source detection ----------------
 *
 * Detected once, at clock init.  HalpEpochRead points at the chosen backend
 * (NULL == no source, clock starts at 1601).  The point of detecting up front
 * is the CMOS RTC: an absent / floating RTC reads STATUS_A as 0xFF (UIP bit
 * stuck set), which is exactly what drives HalpReadCmosTime's UIP guard to its
 * million-iteration limit -- and under KVM every one of those reads is a
 * port-I/O VM-exit, so it stalls boot for seconds.  By probing presence once
 * here we only ever call into the CMOS reader when an RTC is genuinely there
 * (in which case the guard exits promptly).  kvmclock will register ahead of
 * CMOS later, so microvm-on-KVM won't even touch the RTC.
 */
typedef enum _HALP_CLOCK_SOURCE {
    HalClockNone = 0,
    HalClockEfi,
    HalClockCmos
    /* HalClockKvm -- reserved for the kvmclock backend */
} HALP_CLOCK_SOURCE;

typedef BOOLEAN (*PHALP_EPOCH_READ)(OUT PLARGE_INTEGER SystemTime);

static HALP_CLOCK_SOURCE HalpClockSource = HalClockNone;
static PHALP_EPOCH_READ  HalpEpochRead   = NULL;
static PHAL_EFI_TIME     HalpEfiTimePtr  = NULL;

static UCHAR HalpReadCmosReg(UCHAR reg);    /* defined below */

/* Cheap one-time probe: is an MC146818 RTC actually present?  A floating /
 * absent RTC reads 0xFF on STATUS_A (UIP permanently set). */
static BOOLEAN
HalpCmosPresent(VOID)
{
    return (BOOLEAN)(HalpReadCmosReg(CMOS_STATUS_A) != 0xFF);
}

static BOOLEAN
HalpEpochReadEfi(OUT PLARGE_INTEGER SystemTime)
{
    return (BOOLEAN)(HalpEfiTimePtr != NULL &&
                     HalpEfiTimeToSystemTime(HalpEfiTimePtr, SystemTime));
}

static BOOLEAN
HalpEpochReadCmos(OUT PLARGE_INTEGER SystemTime)
{
    TIME_FIELDS tf;
    HalpReadCmosTime(&tf);          /* RTC present -> UIP guard exits promptly */
    return RtlTimeFieldsToTime(&tf, SystemTime);
}

/* Pick the epoch backend once, in priority order: the UEFI runtime seed first
 * (host-synced, most trustworthy), then -- in future -- kvmclock, then the
 * CMOS RTC if one is present. */
static VOID
HalpDetectClockSource(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (LoaderBlock->Spare1 != 0) {
        HalpEfiTimePtr  = (PHAL_EFI_TIME)LoaderBlock->Spare1;
        HalpEpochRead   = HalpEpochReadEfi;
        HalpClockSource = HalClockEfi;
        HalpSerialPrint("HAL: clock source = UEFI runtime\r\n");
    } else if (HalpCmosPresent()) {
        HalpEpochRead   = HalpEpochReadCmos;
        HalpClockSource = HalClockCmos;
        HalpSerialPrint("HAL: clock source = CMOS RTC\r\n");
    } else {
        HalpEpochRead   = NULL;
        HalpClockSource = HalClockNone;
        HalpSerialPrint("HAL: clock source = none (no UEFI seed, no RTC)\r\n");
    }
}

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

    /* Choose the wall-clock backend once (see HalpDetectClockSource), then
     * read the boot seed through it.  Detection has already ruled out an
     * absent RTC, so we never enter HalpReadCmosTime's UIP spin here.  CMOS
     * holds UTC (qemu -rtc base=utc, our documented contract); the EFI path
     * uses RT->GetTime(). */
    HalpBootSystemTime.QuadPart = 0;
    HalpDetectClockSource(LoaderBlock);
    if (HalpEpochRead != NULL && HalpEpochRead(&HalpBootSystemTime)) {
        HalpPrintWallClock("HAL: wall-clock seed: ", HalpBootSystemTime);
    } else {
        HalpSerialPrint("HAL: no usable wall-clock — starts at 1601\r\n");
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

/* ===== RTC ===== */

static UCHAR
HalpBcdToBin(UCHAR v)
{
    return (UCHAR)((v & 0x0F) + ((v >> 4) * 10));
}

static UCHAR
HalpReadCmosReg(UCHAR reg)
{
    HalpWritePort(CMOS_INDEX, reg);
    return HalpReadPort(CMOS_DATA);
}

/*
 * Read the MC146818 CMOS RTC into TIME_FIELDS.  Used as the non-EFI
 * wall-clock seed (see HalpInitTscClock); on a UEFI boot we never get
 * here because RT->GetTime() already seeded HalpBootSystemTime.
 *
 * Honours Status Register B for BCD-vs-binary and 12-vs-24-hour rather
 * than assuming qemu's defaults, so the same path works on bare metal.
 * Caller validates the result via RtlTimeFieldsToTime — if the RTC is
 * absent (microvm without rtc=on) the data port floats to 0xFF and the
 * decoded fields fail that range check.
 */
VOID
HalpReadCmosTime(
    PTIME_FIELDS TimeFields
    )
{
    UCHAR   sec, min, hour, day, mon, yr, cent, regB;
    ULONG   guard;
    BOOLEAN bcd, hour12, pm;

    /* Wait out any update-in-progress so we don't latch a half-rolled
     * value.  Bounded spin: a missing RTC can leave UIP stuck set. */
    for (guard = 0; guard < 1000000; guard++) {
        if (!(HalpReadCmosReg(CMOS_STATUS_A) & CMOS_A_UIP)) {
            break;
        }
    }

    regB   = HalpReadCmosReg(CMOS_STATUS_B);
    bcd    = (regB & CMOS_B_BINARY) ? FALSE : TRUE;
    hour12 = (regB & CMOS_B_24HOUR) ? FALSE : TRUE;

    sec  = HalpReadCmosReg(CMOS_RTC_SECONDS);
    min  = HalpReadCmosReg(CMOS_RTC_MINUTES);
    hour = HalpReadCmosReg(CMOS_RTC_HOURS);
    day  = HalpReadCmosReg(CMOS_RTC_DAY);
    mon  = HalpReadCmosReg(CMOS_RTC_MONTH);
    yr   = HalpReadCmosReg(CMOS_RTC_YEAR);
    cent = HalpReadCmosReg(CMOS_RTC_CENTURY);

    /* In 12-hour mode the PM flag rides bit 7 of the hour register and
     * must be stripped before BCD decode. */
    pm    = (hour12 && (hour & 0x80)) ? TRUE : FALSE;
    hour &= 0x7F;

    if (bcd) {
        sec  = HalpBcdToBin(sec);
        min  = HalpBcdToBin(min);
        hour = HalpBcdToBin(hour);
        day  = HalpBcdToBin(day);
        mon  = HalpBcdToBin(mon);
        yr   = HalpBcdToBin(yr);
        cent = HalpBcdToBin(cent);
    }

    if (hour12) {
        hour %= 12;          /* 12 -> 0 ... */
        if (pm) {
            hour += 12;      /* ... 1pm -> 13, 12pm stays 12 */
        }
    }

    TimeFields->Second       = (CSHORT)sec;
    TimeFields->Minute       = (CSHORT)min;
    TimeFields->Hour         = (CSHORT)hour;
    TimeFields->Day          = (CSHORT)day;
    TimeFields->Month        = (CSHORT)mon;
    /* Century register is valid on qemu; if blank/garbage (absent RTC,
     * or firmware that never set it) assume 20xx since we ship well
     * after 2000. */
    if (cent >= 19 && cent <= 21) {
        TimeFields->Year = (CSHORT)((ULONG)cent * 100 + yr);
    } else {
        TimeFields->Year = (CSHORT)(2000 + yr);
    }
    TimeFields->Milliseconds = 0;
    TimeFields->Weekday      = 0;
}

/*
 * Live wall-clock query.  Returns boot system time plus the wall
 * interval elapsed since boot, derived from PIT-channel-0 counter
 * deltas accumulated in HalpPitTicksSinceBoot by HalpClockTickIncrement.
 *
 * Was TSC-derived; that broke under QEMU TCG where RDTSC tracks
 * guest-execution rate rather than wall (see HalpClockTickIncrement
 * comment).  PIT is wall-aligned under both TCG and KVM, so this
 * works on every machine type / accelerator we target.
 *
 * Returns FALSE only when there's no UEFI seed at all — graceful
 * fallback to today's 1601 zero-time behaviour.
 *
 * Racy read of HalpPitTicksSinceBoot vs the ISR's update: a torn
 * read can give a value that's high/low for one call.  At 100 Hz
 * a tear straddles a PIT tick at most ~once per ~1.8 years of
 * uptime per uncached read, and the worst-case error is 10 ms.
 * Accepting the race rather than spinlock-protecting; if we ever
 * need stricter we can copy the high-low-high retry pattern from
 * KiQueryInterruptTime.
 */
BOOLEAN
HalQueryRealTimeClock(
    OUT PTIME_FIELDS TimeFields
    )
{
    ULONGLONG     ticks, dt_100ns;
    LARGE_INTEGER live;

    if (HalpBootSystemTime.QuadPart == 0) {
        return FALSE;
    }

    ticks    = HalpPitTicksSinceBoot;
    dt_100ns = (ticks * (ULONGLONG)10000000) / PIT_FREQ;

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
 * Per-tick increment passed to KeUpdateSystemTime, derived from a
 * direct read of PIT channel 0's down-counter.
 *
 * Why not TSC: under QEMU TCG, RDTSC ticks at the guest-execution
 * rate (cpu_get_ticks → virtual time, not wall) — so a delta
 * captures whatever fraction of host wall the guest happened to
 * be scheduled for, not the wall interval between two PIT IRQs.
 * Result is InterruptTime that lags wall by 10-30x under TCG; a
 * 2-second NtWaitForSingleObject takes a minute of wall to fire.
 * Under KVM the TSC is honest, so the TSC path worked, but the
 * dev/CI/TCG path was structurally broken.
 *
 * PIT channel 0 is programmed (halinit.c) for mode 2 at 100 Hz
 * (reload N = 11932) and qemu emulates the PIT against its
 * QEMU_CLOCK_VIRTUAL_RT clock — i.e. real wall time — under both
 * TCG and KVM.  So a PIT-counter delta is the universal "what
 * happened to the wall between this ISR and the last one" answer
 * that works on every machine type / accelerator we target.
 *
 * Mode 2 counter decrements by 1 per PIT clock and reloads at 0,
 * so the legal range is 1..N; counter is monotonically decreasing
 * within a cycle.  Per-ISR delta in PIT clocks is:
 *
 *   if (current <= last)      // same cycle, no wrap
 *       delta = last - current
 *   else                      // wrapped — counter reloaded once
 *       delta = last + N - current
 *
 * Multi-wrap (delta > N) under cpulimit SIGSTOP / VM-pause is
 * possible but invisible to this code; the bounded under-report
 * is acceptable for now and will be addressed when we add HPET
 * (64-bit counter, can't wrap on us) or PV clocks.
 *
 * Runs at CLOCK2_LEVEL (we're inside the ISR), so no spinlock
 * needed even on SMP — though we're UP-only today.
 */
#define PIT_RELOAD (PIT_FREQ / 100)           /* 11932 — matches halinit */

static USHORT
HalpReadPitCounter(VOID)
{
    UCHAR lo, hi;
    /* Latch counter 0: command byte = 0000 0000.  Snapshots the
     * 16-bit counter into a read-only buffer so the lo/hi byte
     * reads are coherent without the counter ticking under us. */
    HalpWritePort(PIT_CMD, 0x00);
    lo = HalpReadPort(PIT_CH0);
    hi = HalpReadPort(PIT_CH0);
    return (USHORT)lo | ((USHORT)hi << 8);
}

ULONG
HalpClockTickIncrement(VOID)
{
    USHORT current;
    ULONG  delta_pit;
    ULONG  dt_100ns;

    current = HalpReadPitCounter();

    if (!HalpPitClockInit) {
        HalpLastPitCount = current;
        HalpPitClockInit = TRUE;
        /* First tick — no previous reading to delta against.
         * Hand back the nominal 10ms; subsequent ticks self-correct. */
        return 100000;
    }

    if (current <= HalpLastPitCount) {
        delta_pit = (ULONG)HalpLastPitCount - (ULONG)current;
    } else {
        /* Counter wrapped (reloaded to N).  Treat as one full cycle
         * plus the partial decrement since reload. */
        delta_pit = (ULONG)HalpLastPitCount + PIT_RELOAD - (ULONG)current;
    }
    HalpLastPitCount = current;

    HalpPitTicksSinceBoot += delta_pit;

    /* PIT clocks → 100ns units: delta * 10^7 / 1193182.
     * delta_pit fits in ULONG (and well under 2^31 for any sane
     * interval), so the multiply stays in ULONGLONG range. */
    dt_100ns = (ULONG)(((ULONGLONG)delta_pit * (ULONGLONG)10000000) / PIT_FREQ);

    return dt_100ns;
}
