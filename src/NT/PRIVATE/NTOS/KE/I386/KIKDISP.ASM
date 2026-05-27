        TITLE  "Kernel-Mode Zw* Direct-Dispatch Helper"
;++
;
; Copyright (c) 1989  Microsoft Corporation
;
; Module Name:
;
;    kikdisp.asm
;
; Abstract:
;
;    This module implements _KiKernelDispatch, the direct-dispatch helper
;    for kernel-mode Zw* callers.  The Zw stubs in sysstubs.asm (generated
;    by gensrv from SERVICES.STB) tail-jmp here with the service number in
;    EAX; we copy args, call the service, and return to the Zw caller.
;
;    Lives in its own file so gensrv can freely regenerate sysstubs.asm
;    when SERVICES.TAB changes — adding a new syscall must not require
;    re-pasting this helper.  The companion change in SERVICES.STB adds an
;    `extrn _KiKernelDispatch:near` to the generated prologue so gensrv
;    output references this symbol cleanly.
;
; Environment:
;
;    Kernel mode only.
;
;--

.386p

include ks386.inc
include callconv.inc

;
; Service-table externals (defined in systable.asm) plus constants for the
; two services that need a real KTRAP_FRAME at [ebp+0] — see kkd_with_trap_frame
; below.  If you reorder SERVICES.TAB and these service numbers change, update
; here.
;
        extrn   _KiArgumentTable:dword
        extrn   _KiServiceTable:dword
        extrn   _KiServiceLimit:dword

NTCONTINUE_SVC          equ     14
NTRAISEEXCEPTION_SVC    equ     113

_TEXT   SEGMENT DWORD PUBLIC 'CODE'

;++
;
; _KiKernelDispatch — direct-dispatch helper for kernel-mode Zw* callers.
;
; Replaces the legacy trap-via-INT-2Eh path.  No IDT round-trip, no
; ENTER_SYSCALL trap frame, no EXIT_ALL — just a small arg copy (the same
; copy KiSystemServiceCopyArguments did inside the trap path) and a direct
; stdcall to the service routine.
;
; Re-entrancy: each invocation builds its own kernel-stack frame, so a
; service that recursively issues `Zw*` (driver code inside NtCreateFile
; calling ZwOpenKey, etc.) gets a fresh dispatch via the same code without
; colliding with our state.
;
; Entry (set by SYSSTUBS_ENTRY1's `mov eax,svc; jmp` before reaching us):
;   eax       = service number (gensrv guarantees in-range)
;   [esp]     = ret-to-Zw-caller (Zw stub did jmp, didn't push)
;   [esp+4..] = caller's args
;
; Exit:
;   eax = NTSTATUS from service
;   stack popped past ret-addr + args (callee-pop stdcall semantics)
;   ebx/esi/edi/ebp restored to caller's values
;
;--

align 4
        public _KiKernelDispatch
_KiKernelDispatch proc

;
; Defensive bounds check.  gensrv only emits valid service numbers, so this
; should be unreachable in correctly-built code; bug-check loudly if hit.
;
        cmp     eax, _KiServiceLimit
        ja      kkd_bad_service

;
; Trap-frame contract for state-modifying services.  NtContinue and
; NtRaiseException read [ebp+0] (their saved EBP, set by their own
; `push ebp; mov ebp, esp` prolog) as a PKTRAP_FRAME and modify it via
; KeContextToKframes.  EXIT_ALL on exit reads TsExceptionList,
; TsPreviousPreviousMode, and the iret frame at TsEip/TsSegCs/TsEflags
; (with FRAME_EDITED iret emulation when ESP needs to be edited).
;
; The fast path below skips the trap-frame build for ~190 other services
; that don't need one.  Branch out for these two so kkd_with_trap_frame
; can synthesize what KiSystemService's ENTER_SYSCALL would have built
; for the legacy INT 2E path.
;
        cmp     eax, NTCONTINUE_SVC
        je      kkd_with_trap_frame
        cmp     eax, NTRAISEEXCEPTION_SVC
        je      kkd_with_trap_frame

;
; Build a small frame: saved non-volatiles + two locals (ArgSize and
; saved PCR.ExceptionList) so callee-pop stdcall + SEH-chain restore
; both work after the service returns.
;
;   [ebp+0]   saved_ebp
;   [ebp+4]   ret-to-caller-of-Zw
;   [ebp+8..] caller's args
;   [ebp-4]   saved_ebx
;   [ebp-8]   saved_esi
;   [ebp-12]  saved_edi
;   [ebp-16]  ArgSize local
;   [ebp-20]  saved PCR.ExceptionList
;
        push    ebp
        mov     ebp, esp
        push    ebx
        push    esi
        push    edi
        sub     esp, 8

;
; Look up dispatch info while eax still holds the service number.  ECX
; receives ArgSize; we stash it in the local for the post-call ret-pop
; (volatiles are clobbered by the service call).  EDX holds the service
; routine address; nothing between here and the call clobbers it.
;
        movzx   ecx, byte ptr _KiArgumentTable[eax]
        mov     [ebp-16], ecx
        mov     edx, _KiServiceTable[eax*4]

;
; Switch Thread.PreviousMode to KernelMode.  EBX (non-volatile per stdcall)
; carries the old value across the service call; EDI carries the thread
; pointer the same way.
;
        mov     edi, fs:[PcPrcbData+PbCurrentThread]
        movzx   ebx, byte ptr [edi+ThPreviousMode]
        mov     byte ptr [edi+ThPreviousMode], 0

;
; Break the SEH chain to match the legacy ENTER_SYSCALL contract:
; PCR.ExceptionList = EXCEPTION_CHAIN_END means RtlDispatchException
; finds no handler if the service routine raises, and the kernel
; bug-checks with KMODE_EXCEPTION_NOT_HANDLED.
;
; By design — and by NT 3.5 documented contract — kernel-mode `Zw*`
; callers must catch internally and return NTSTATUS.  A service that
; raises through us is a contract violation and we want it to fail
; loudly rather than unwind into the caller with a stale PreviousMode.
;
; Saved old chain head goes in the local so we restore it on the
; normal-return path; on the raise path the bug-check happens before
; anything observes the missing restore.
;
        mov     esi, fs:[PcExceptionList]
        mov     [ebp-20], esi
        mov     dword ptr fs:[PcExceptionList], EXCEPTION_CHAIN_END

;
; Copy args from caller's frame into a fresh slot just below esp, then call
; the service.  After the call's ret-push, the service sees [esp+4..]=args
; — exactly the layout a direct caller would have produced.
;
        sub     esp, ecx
        lea     esi, [ebp+8]                    ; src = caller's first arg
        mov     edi, esp                        ; dst = our allocation
        shr     ecx, 2
        rep     movsd

;
; rep movsd advanced edi past the dst region; reload it with the thread
; pointer so we have it back after the service returns.
;
        mov     edi, fs:[PcPrcbData+PbCurrentThread]
        call    edx

;
; Service did `ret 4*N` popping its arg copy + return address.  ESP is back
; to ebp-20 (the saved-ExceptionList local).  EAX = NTSTATUS.  EDI still =
; thread pointer, EBX still = old prev-mode (both preserved by stdcall).
;
        mov     byte ptr [edi+ThPreviousMode], bl
        mov     esi, [ebp-20]                   ; restore PCR.ExceptionList
        mov     fs:[PcExceptionList], esi
        mov     ecx, [ebp-16]                   ; ecx = ArgSize for callee-pop

;
; Tear down the frame.
;
        add     esp, 8                          ; deallocate both locals
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp

;
; Stack: [esp]=ret-to-Zw-caller, [esp+4..]=original args.  Pop ret-addr
; into a volatile, skip args (callee-pop stdcall), jmp back.
;
        pop     edx
        add     esp, ecx
        jmp     edx

kkd_bad_service:
        int 3
        jmp     kkd_bad_service

;
;------------------------------------------------------------------------
; kkd_with_trap_frame — synthetic-trap-frame dispatch for state-modifying
; services (NtContinue, NtRaiseException).
;
; Stock NT 3.5 kernel-mode INT 2E built a real KTRAP_FRAME on the kernel
; stack via ENTER_SYSCALL.  These two services read [ebp+0] (the saved
; EBP from their own prolog) as PKTRAP_FRAME and edit fields like TsEip
; (KeContextToKframes), TsExceptionList (NtRaiseException explicitly,
; NtContinue via EXIT_ALL), and TempEsp (KiEspToTrapFrame, when the
; captured CONTEXT.Esp differs from trap-frame ESP — which it always
; does for kernel-mode unwind, since we're called from RtlUnwind which
; lives much deeper on the stack than the captured _gu_return frame).
;
; EXIT_ALL on exit:
;   1. Restores PCR.ExceptionList from TsExceptionList — so the SEH
;      chain head is whatever fs:[0] was at our entry, which for
;      RtlUnwind ZwContinue is exactly TargetFrame (the unwind target).
;   2. Restores Thread.PreviousMode from TsPreviousPreviousMode.
;   3. Detects FRAME_EDITED bits cleared in TsSegCs (set by
;      KiEspToTrapFrame when it stashed CsEsp into TsTempEsp) and
;      runs the iret-emulation path: writes EIP/CS/EFLAGS to
;      [TempEsp-12..TempEsp-1], pops non-volatiles from the trap frame,
;      switches ESP to TempEsp-12, iretd → resumes at CsEip with
;      ESP = CsEsp.
;
; This is exactly the mechanism stock NT used for kernel-mode INT 2E
; unwind.  We just synthesize the trap frame at direct-call entry
; instead of relying on hardware INT 2E + ENTER_SYSCALL.
;
; Entry:
;   eax = service number (14 or 113, already bounds-checked)
;   [esp]   = retaddr (Zw stub did jmp, didn't push)
;   [esp+4..] = caller's args
;
; Exit: never returns directly; service jmps to KiServiceExit2.
;------------------------------------------------------------------------
;
align 4
kkd_with_trap_frame:

;
; Allocate trap frame on stack.  After: esp = trap_frame_base; original
; args (above the retaddr) live at trap_frame_base+KTRAP_FRAME_LENGTH+4.
;
        sub     esp, KTRAP_FRAME_LENGTH

;
; Initialize the trap-frame fields EXIT_ALL/DISPATCH_USER_APC depend on
; that KeContextToKframes will not overwrite from CONTEXT:
;
;   TsErrCode             = 0          (skipped past iret emulation)
;   TsSegCs               = KGDT_R0_CODE — has FRAME_EDITED bits set
;                            (bit 3), so KiEspToTrapFrame's first-edit
;                            branch fires; KeContextToKframes will then
;                            stash real CS in TsTempSegCs and clear the
;                            FRAME_EDITED bits, marking the frame for
;                            EXIT_ALL's iret emulation
;   TsHardwareSegSs       = KGDT_R0_DATA (defensive; intra-priv iret
;                            doesn't pop SS but EXIT_ALL touches SegCs)
;   TsDbgArgMark          = 0BADB0D00h (DBG sanity check in EXIT_ALL)
;
; Other fields (TsEip, TsEFlags, TsEbp, TsEdi, TsEsi, TsEbx, TsEax,
; TsEcx, TsEdx, segment regs, TsTempEsp, TsTempSegCs) are written by
; KeContextToKframes from the CONTEXT record the service receives.
;
        mov     dword ptr [esp]+TsErrCode, 0
        mov     dword ptr [esp]+TsSegCs, KGDT_R0_CODE
        mov     dword ptr [esp]+TsHardwareSegSs, KGDT_R0_DATA
if DBG
        mov     dword ptr [esp]+TsDbgArgMark, 0BADB0D00h
endif

;
; Capture & break the SEH chain (ENTER_SYSCALL contract).
;   - EXIT_ALL restores PCR.ExceptionList from TsExceptionList on exit.
;   - NtRaiseException explicitly restores it at its prolog
;     (TRAP.ASM:5340) so KiDispatchException sees the real chain.
;   - For NtContinue, no body re-restore happens; the chain head we save
;     here equals TargetFrame at RtlUnwind ZwContinue, which is exactly
;     what fs:[0] should be after the unwind completes.
;
        mov     ecx, fs:[PcExceptionList]
        mov     [esp]+TsExceptionList, ecx
        mov     dword ptr fs:[PcExceptionList], EXCEPTION_CHAIN_END

;
; Capture & switch Thread.PreviousMode to KernelMode (ENTER_SYSCALL
; contract).  EXIT_ALL restores from TsPreviousPreviousMode (read as a
; dword; only the low byte is used, but DBG checks the full dword
; against -1).  movzx zero-extends the byte so the dword is never -1.
;
        mov     edi, fs:[PcPrcbData+PbCurrentThread]
        movzx   ebx, byte ptr [edi+ThPreviousMode]
        mov     [esp]+TsPreviousPreviousMode, ebx
        mov     byte ptr [edi+ThPreviousMode], 0

;
; Set ebp to trap frame.  The service's `push ebp; mov ebp, esp` prolog
; will save this as [their_ebp+0]; their NcTrapFrame macro then
; dereferences it as PKTRAP_FRAME correctly.
;
        mov     ebp, esp

;
; Copy caller's args to a fresh slot below the trap frame, then call
; the service.  Args are above the retaddr at trap_frame_base+
; KTRAP_FRAME_LENGTH+4.
;
        movzx   ecx, byte ptr _KiArgumentTable[eax]
        sub     esp, ecx
        lea     esi, [ebp + KTRAP_FRAME_LENGTH + 4]
        mov     edi, esp
        shr     ecx, 2
        rep     movsd

        mov     edx, _KiServiceTable[eax*4]
        call    edx                             ; NtContinue or NtRaiseException

;
; The service jmps to _KiServiceExit2 → EXIT_ALL → iret emulation and
; never returns to us.  If we get here something is very wrong.
;
        int     3
        jmp     short kkd_with_trap_frame

_KiKernelDispatch endp

_TEXT   ENDS
        end
