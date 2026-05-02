        title   "Software Interrupt Support Tables"
;++
;
; Module Name:
;
;    swinttab.asm
;
; Abstract:
;
;    Support tables and helpers for software-interrupt dispatch in the
;    MicroNT HAL. The real handlers HalpApcInterrupt / HalpDispatchInterrupt
;    live in swint.asm (copied from NT 3.5's IXSWINT.ASM).
;
;    Unlike the original HAL, we don't expose hardware-interrupt entries
;    through this table; our HAL wires hardware IRQs directly via
;    ixintr.c/HalBeginSystemInterrupt. Only the software IRQLs (APC=1,
;    DISPATCH=2) dispatch through SWInterruptHandlerTable.
;
;--

.386p
        .xlist
include hal386.inc
include callconv.inc
include i386\kimacro.inc
        .list

        extrn   _KiUnexpectedInterrupt:near
        extrn   _HalpApcInterrupt:near
        extrn   _HalpDispatchInterrupt:near
        extrn   _HalpApcInterrupt2ndEntry:near
        extrn   _HalpDispatchInterrupt2ndEntry:near

_DATA   SEGMENT DWORD PUBLIC 'DATA'

        public  SWInterruptHandlerTable
SWInterruptHandlerTable label dword
        dd      offset FLAT:_KiUnexpectedInterrupt      ; irql 0 (passive)
        dd      offset FLAT:_HalpApcInterrupt           ; irql 1 (APC)
        dd      offset FLAT:_HalpDispatchInterrupt      ; irql 2 (DISPATCH)
        dd      offset FLAT:_KiUnexpectedInterrupt      ; irql 3 (unused)

        public  SWInterruptHandlerTable2
SWInterruptHandlerTable2 label dword
        dd      offset FLAT:_KiUnexpectedInterrupt      ; irql 0
        dd      offset FLAT:_HalpApcInterrupt2ndEntry   ; irql 1
        dd      offset FLAT:_HalpDispatchInterrupt2ndEntry ; irql 2

;
; FindHigherIrqlMask: indexed by current IRQL, yields a bitmask of IRR
; bits that represent SW-interrupt levels strictly HIGHER than current.
; Only IRQLs 0 (passive), 1 (APC), 2 (DISPATCH) are relevant for SW
; interrupts — entries for higher levels mask to 0.
;

        public FindHigherIrqlMask
FindHigherIrqlMask label dword
        dd      00000006h           ; current=0 (passive)   -> APC|DISPATCH
        dd      00000004h           ; current=1 (APC)       -> DISPATCH
        dd      00000000h           ; current=2 (DISPATCH)  -> none
        dd      00000000h           ; 3..31: never pending in SW
        dd      00000000h, 00000000h, 00000000h, 00000000h
        dd      00000000h, 00000000h, 00000000h, 00000000h
        dd      00000000h, 00000000h, 00000000h, 00000000h
        dd      00000000h, 00000000h, 00000000h, 00000000h
        dd      00000000h, 00000000h, 00000000h, 00000000h
        dd      00000000h, 00000000h, 00000000h, 00000000h
        dd      00000000h, 00000000h, 00000000h, 00000000h

;
; SWInterruptLookUpTable: indexed by the 3 low bits of IRR (APC+DISPATCH),
; yields the highest pending SW IRQL.
;

        public  SWInterruptLookUpTable
SWInterruptLookUpTable label byte
        db      0       ; SWIRR=0 (none)
        db      0       ; SWIRR=1 (bit 0)
        db      1       ; SWIRR=2 (APC)
        db      1       ; SWIRR=3 (bit0|APC)
        db      2       ; SWIRR=4 (DISPATCH)
        db      2       ; SWIRR=5
        db      2       ; SWIRR=6
        db      2       ; SWIRR=7

_DATA   ends

_TEXT$02   SEGMENT DWORD PUBLIC 'CODE'
        ASSUME  DS:FLAT, ES:FLAT, SS:FLAT, FS:NOTHING, GS:NOTHING

;++
;
; VOID
; HalpEndSoftwareInterrupt (
;    IN KIRQL NewIrql
;    )
;
; Restore Pcr->Irql to NewIrql and, if any higher-priority software
; interrupt is pending, dispatch it through SWInterruptHandlerTable2.
; Called from the SOFT_INTERRUPT_EXIT macro in ix8259.inc, which passes
; the saved previous IRQL on the stack.
;
;--

        public  _HalpEndSoftwareInterrupt@4
_HalpEndSoftwareInterrupt@4 proc near

        movzx   ecx, byte ptr [esp+4]           ; NewIrql
        mov     edx, PCR[PcIRR]
        and     edx, FindHigherIrqlMask[ecx*4]
        mov     byte ptr PCR[PcIrql], cl        ; byte write — don't clobber IRR!
        jz      short hesi_ret

;
; Highest pending SW IRQL > NewIrql. Find it and jump (NOT call) to the
; "2nd entry" handler which expects an existing trap frame on stack.
;

        mov     cl, SWInterruptLookUpTable[edx]
        add     esp, 8                          ; discard retaddr + NewIrql
        jmp     SWInterruptHandlerTable2[ecx*4]

hesi_ret:
        ret     4

_HalpEndSoftwareInterrupt@4 endp

_TEXT$02   ends

        end
