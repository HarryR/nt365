        title  "Processor type and stepping detection"
;++
;
; Copyright (c) 1989  Microsoft Corporation
;
; Module Name:
;
;    cpu.asm
;
; Abstract:
;
;    This module implements the assembly code necessary to determine
;    cpu type and stepping information and to enable the NPX.
;
;    MicroNT: stock NT 3.5 carried Shie-Lin Tzong's (shielint, Oct 1991)
;    pre-CPUID stepping archaeology — 80386 B0/B1/D1 discriminator via
;    the XBTS illegal-opcode probe, REP MOVSB + TRACE-trap races, and
;    a 32-bit MUL stress test; 80486 A/B/C stepping discriminators via
;    CR0.ET hardwire test, DR4/5 aliasing #UD probes, and an FPU FSCALE
;    pseudo-denormal errata test.  Temporary local IDT-trap6 handlers
;    let the kernel survive #UD from these test instructions.  All of
;    that scaffolding existed to disambiguate steppings that no
;    MicroNT-runnable hardware has.  We collapse to the CPUID branch
;    only — Pentium+ class CPUs (QEMU TCG / KVM presenting as
;    GenuineIntel or AuthenticAMD) always have CPUID, always have an
;    on-chip FPU, and never need the 386/486 stepping discriminator.
;
; Environment:
;
;    80x86 protected mode, INIT-time only.
;
;--

        .xlist
include ks386.inc
include callconv.inc
        .list

.386p

INIT    SEGMENT DWORD PUBLIC 'CODE'
        ASSUME  DS:FLAT, ES:FLAT, SS:NOTHING, FS:NOTHING, GS:NOTHING


;++
;
; USHORT
; KiSetProcessorType (
;    VOID
;    )
;
; Routine Description:
;
;    Records CPU type and stepping in the current PRCB.  Uses CPUID
;    unconditionally — Pentium+ class CPUs all have it.
;
; Arguments:
;
;    None.
;
; Return Value:
;
;    Prcb->CpuType
;       Family masked to the low 3 bits ((family & 7)) — stock NT 3.5
;       encoding.  Pentium family 5 -> 5; P6 family 6 -> 6; NetBurst
;       family 15 -> 7 (the top family bit is intentionally dropped
;       so downstream readers see `>= 5` semantics).
;
;    Prcb->CpuStep is encoded as follows:
;       upper byte = CPUID model nibble
;       lower byte = CPUID stepping nibble
;
;    (ax) = CpuStep, for caller's convenience.
;
;--
cPublicProc _KiSetProcessorType,0

        push    ebx
        push    esi

        mov     eax, 0                  ; CPUID level 0 -> max supported
.586p
        cpuid
.386p
        cmp     eax, 1                  ; level 1 supported?
        jb      cpu_no_level1           ; should not happen on Pentium+

        mov     eax, 1                  ; get family/model/stepping
.586p
        cpuid
.386p

        ; eax layout: [Type:Family:Model:Stepping] in 4-bit nibbles
        ;   bits 13:12 = Type      (ignored)
        ;   bits 11:8  = Family
        ;   bits 7:4   = Model
        ;   bits 3:0   = Stepping
        ;
        ; PbCpuStep encoding: model in upper byte, stepping in lower.
        ; PbCpuType encoding: (family & 7), preserving stock NT 3.5
        ; bit-truncation behaviour so downstream "CpuType >= 5" checks
        ; keep their semantics.

        mov     ebx, eax                ; save raw level-1 eax

        and     eax, 0F0h               ; isolate Model nibble
        shl     eax, 4                  ; -> bits 15:8
        mov     al, bl
        and     eax, 0F0Fh              ; ah = Model, al = Stepping

        and     ebx, 0700h              ; bh = Family & 7

        mov     byte ptr fs:PcPrcbData.PbCpuID, 1
        mov     byte ptr fs:PcPrcbData.PbCpuType, bh
        mov     word ptr fs:PcPrcbData.PbCpuStep, ax

        pop     esi
        pop     ebx
        stdRET  _KiSetProcessorType

cpu_no_level1:
        ; CPUID level 1 not supported.  We can't run anywhere this
        ; happens — every Pentium-class+ CPU has it.  Halt rather
        ; than pretend.  UD2 is the canonical "I shouldn't be here"
        ; instruction; ML 6.11d doesn't know the mnemonic, so emit
        ; the raw two-byte encoding (0F 0B).
        db      0Fh, 0Bh

stdENDP _KiSetProcessorType


;++
;
; BOOLEAN
; KiIsNpxPresent(
;     VOID
;     );
;
; Routine Description:
;
;     Probes for an on-chip / co-processor FPU and configures CR0
;     accordingly.  On Pentium+ the FPU is hardwired; the probe is
;     defensive (FNINIT writes a known status word, FNSTSW reads it
;     back — if the bus reads zero the FPU is there).
;
;     MicroNT: stock NT 3.5 skipped setting CR0.NE on CPU type <= 3
;     (i386, no Native-Mode error reporting).  Pentium+ supports
;     native error reporting, so we always enable it.
;
; Arguments:
;
;     None.
;
; Return:
;
;     TRUE if NPX is present, FALSE otherwise.  Programs CR0 with
;     ET / NE / EM / TS bits before returning.  EM+TS are set so the
;     first FPU access in any thread context traps to KiTrap07 for
;     lazy save/restore; ET marks the on-chip FPU; NE picks native
;     error reporting over the legacy IRQ13 path.
;
;--

cPublicProc _KiIsNpxPresent,0

        push    ebp
        mov     eax, cr0
        and     eax, NOT (CR0_ET+CR0_MP+CR0_TS+CR0_EM)
        mov     cr0, eax
        xor     edx, edx
.287
        fninit                          ; reset FPU status word
        mov     ecx, 5A5A5A5Ah          ; non-zero sentinel in scratch
        push    ecx
        mov     ebp, esp
        fnstsw  word ptr [ebp]          ; FPU writes 0 here if present
        cmp     byte ptr [ebp], 0
        jne     Inp10                   ; nonzero -> no FPU

        or      eax, CR0_ET             ; FPU is on-chip (387-class+)
        or      eax, CR0_NE             ; native error reporting
        mov     edx, 1                  ; return TRUE

Inp10:
        or      eax, CR0_EM+CR0_TS      ; lazy-FPU bits during init
        mov     cr0, eax
        pop     eax                     ; clear scratch
        pop     ebp
        mov     eax, edx
        stdRet  _KiIsNpxPresent


stdENDP _KiIsNpxPresent

;++
;
; VOID
; Ke386CpuID (
;     ULONG   InEax,
;     PULONG  OutEax,
;     PULONG  OutEbx,
;     PULONG  OutEcx,
;     PULONG  OutEdx
;     );
;
; Routine Description:
;
;   Executes the CPUID instruction and returns the registers from it.
;   Only available at INIT time.
;
;--
cPublicProc _Ke386CpuID,5

    push    ebx
    push    esi

    mov     eax, [esp+12]
    db      0fh, 0a2h       ; CPUID

    mov     esi, [esp+16]   ; return EAX
    mov     [esi], eax

    mov     esi, [esp+20]   ; return EBX
    mov     [esi], ebx

    mov     esi, [esp+24]   ; return ECX
    mov     [esi], ecx

    mov     esi, [esp+28]   ; return EDX
    mov     [esi], edx

    pop     esi
    pop     ebx

    stdRET  _Ke386CpuID

stdENDP _Ke386CpuID


INIT    ENDS
        END
