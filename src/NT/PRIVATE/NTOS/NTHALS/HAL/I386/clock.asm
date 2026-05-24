        title  "MicroNT HAL - PIT Clock Interrupt"
;++
;
; clock.asm - HalpClockInterrupt
;
; Minimal PIT-based clock ISR for MicroNT's HAL. Fires at 100Hz from
; IRQ0 (vector 0x30) and tail-calls the kernel's _KeUpdateSystemTime
; which maintains KiTickCount + SharedUserData->SystemTime and fires
; expired kernel timers.
;
; Stripped compared to the real NT HALX86 IXCLOCK.ASM — no profile
; interrupt, no variable rate, no MCA-specific EOI, no performance
; counter. Just enough to keep NtDelayExecution / NtWaitFor*Timeout
; working.
;
; Contract with the kernel's INTERRUPT_EXIT macro (via KeUpdateSystemTime):
;   (esp)   = PreviousIrql
;   (esp+4) = HardwareVector
;   (esp+8) = base of trap frame
;   ebp     = base of trap frame
;   eax     = TimeIncrement (100ns units)
;
; The EOI happens on exit in HalEndSystemInterrupt (called from
; INTERRUPT_EXIT), not at entry — HalBeginSystemInterrupt here only
; raises the IRQL to CLOCK2_LEVEL.
;
;--

.386p
        .xlist
include hal386.inc
include callconv.inc
include i386\ix8259.inc
include i386\kimacro.inc
include mac386.inc
        .list

        EXTRNP  _KeUpdateSystemTime,0
        EXTRNP  Kei386EoiHelper,0,IMPORT
        EXTRNP  _HalBeginSystemInterrupt,3
        EXTRNP  _HalpClockTickIncrement,0

CLOCK_VECTOR            EQU     030H    ; IRQ0 = PRIMARY_VECTOR_BASE + 0

_TEXT   SEGMENT DWORD PUBLIC 'CODE'
        ASSUME  DS:FLAT, ES:FLAT, SS:NOTHING, FS:NOTHING, GS:NOTHING

;
; ENTER_INTERRUPT needs matching Dr_/Abios_ labels supplied by ENTER_DR_ASSIST.
;
        ENTER_DR_ASSIST Hci_a, Hci_t

cPublicProc _HalpClockInterrupt     ,0

;
; Build trap frame. On exit: ebp → trap frame, esp → base of trap frame.
;
        ENTER_INTERRUPT Hci_a, Hci_t

;
; Set up args for HalBeginSystemInterrupt(NewIrql, Vector, &OldIrql):
;   push Vector (becomes (esp+4) after OldIrql slot — also the 2nd arg
;               to KeUpdateSystemTime on exit)
;   sub  esp,4 (OldIrql slot — 1st arg to KeUpdateSystemTime on exit,
;               filled by HalBeginSystemInterrupt via its 3rd arg)
;   stdCall with (CLOCK2_LEVEL, CLOCK_VECTOR, esp)
;
        push    CLOCK_VECTOR
        sub     esp, 4
        stdCall _HalBeginSystemInterrupt, <CLOCK2_LEVEL, CLOCK_VECTOR, esp>

;
; Spurious check (al=0 → bail via Kei386EoiHelper without touching time).
;
        or      al, al
        jz      Hci_spurious

;
; Stack now:
;   (esp)     = OldIrql (filled by HalBeginSystemInterrupt)
;   (esp+4)   = CLOCK_VECTOR
;   (esp+8)   = trap frame base
; ebp = trap frame
;
; Hand off to KeUpdateSystemTime with eax = tick increment. It returns
; to our caller via INTERRUPT_EXIT → HalEndSystemInterrupt → Kei386EoiHelper.
;
; Compute the increment dynamically from the TSC delta since the
; previous tick (HalpClockTickIncrement → eax in 100-ns units). This
; replaces the original fixed 100000 so SystemTime tracks invariant-
; TSC accuracy rather than PIT crystal drift.  __stdcall preserves
; ebp / ebx / esi / edi, so the trap-frame pointer in ebp survives
; the call.
;
        stdCall _HalpClockTickIncrement
        jmp     _KeUpdateSystemTime@0

Hci_spurious:
;
; Undo the push/sub we did above so Kei386EoiHelper sees (esp)=trap frame.
;
        add     esp, 8
        jmp     dword ptr [__imp_Kei386EoiHelper@0]

stdENDP _HalpClockInterrupt

;
; HalpApicSpurious - LAPIC spurious-interrupt vector (0xFF) handler.
; A spurious interrupt needs no EOI; just return.  A bare iretd preserves
; every register because we push/pop nothing.
;
cPublicProc _HalpApicSpurious  ,0
        iretd
stdENDP _HalpApicSpurious

_TEXT   ends
        end
