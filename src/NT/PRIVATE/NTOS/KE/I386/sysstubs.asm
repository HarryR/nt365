;++
;
; Copyright (c) 1989  Microsoft Corporation
;
; Module Name:
;
;    sysstubs.asm
;
; Abstract:
;
;    This module implements the system service dispatch stub procedures.
;
; Author:
;
;    Shie-Lin Tzong (shielint) 6-Feb-1990
;
; Environment:
;
;    User or kernel mode.
;
; Revision History:
;
;--

include ks386.inc
include callconv.inc

.386

;
; Externs for the direct-dispatch helper near the top of the _TEXT segment
; (defined just before the SYSSTUBS_ENTRY1 invocations below).
;
        extrn   _KiArgumentTable:dword
        extrn   _KiServiceTable:dword
        extrn   _KiServiceLimit:dword

;
; Service numbers for state-modifying services that require a real
; KTRAP_FRAME at [ebp+0] (see kkd_with_trap_frame in _KiKernelDispatch).
; If the SYSSTUBS_ENTRY1 invocations below are reordered, update these.
;
NTCONTINUE_SVC          equ     14
NTRAISEEXCEPTION_SVC    equ     113

STUBS_BEGIN1 macro t
    TITLE t
endm
STUBS_BEGIN2 macro t
endm
STUBS_BEGIN3 macro t
_TEXT	SEGMENT DWORD PUBLIC 'CODE'
endm
STUBS_BEGIN4 macro t
endm
STUBS_BEGIN5 macro t
    align 4
endm
STUBS_BEGIN6 macro t
endm
STUBS_BEGIN7 macro t
endm
STUBS_BEGIN8 macro t
endm

STUBS_END    macro t
_TEXT ENDS
      end
endm

SYSSTUBS_ENTRY1 macro ServiceNumber, Name, NumArgs
cPublicProc _Zw&Name,NumArgs
.FPO ( 0, NumArgs, 0, 0, 0, 0 )
IFIDN <Name>, <SetHighWaitLowThread>
        int 2Bh
ELSE
IFIDN <Name>, <SetLowWaitHighThread>
        int 2Ch
ELSE
;
; Direct-dispatch fast path for kernel-mode Zw* callers — see
; _KiKernelDispatch in KE/I386/sysstubs.asm.  Tail-jmp; no IDT round-trip,
; no trap frame.  stdRET below is unreachable on the fast path; preserved
; for FPO/proc framing only.
;
        mov     eax, ServiceNumber      ; (eax) = service number
        jmp     _KiKernelDispatch       ; tail-jmp; helper returns to caller
ENDIF
ENDIF
        stdRET  _Zw&Name
stdENDP _Zw&Name
endm

SYSSTUBS_ENTRY2 macro ServiceNumber, Name, NumArgs
endm
SYSSTUBS_ENTRY3 macro ServiceNumber, Name, NumArgs
endm
SYSSTUBS_ENTRY4 macro ServiceNumber, Name, NumArgs
endm
SYSSTUBS_ENTRY5 macro ServiceNumber, Name, NumArgs
endm
SYSSTUBS_ENTRY6 macro ServiceNumber, Name, NumArgs
endm
SYSSTUBS_ENTRY7 macro ServiceNumber, Name, NumArgs
endm
SYSSTUBS_ENTRY8 macro ServiceNumber, Name, NumArgs
endm


USRSTUBS_ENTRY1 macro ServiceNumber, Name, NumArgs
local   c
cPublicProc     _Zw&Name, NumArgs
PUBLICP _Nt&Name, NumArgs
LABELP  _Nt&Name, NumArgs
.FPO ( 0, NumArgs, 0, 0, 0, 0 )
IFIDN <Name>, <SetHighWaitLowThread>
        int 2Bh
ELSE
IFIDN <Name>, <SetLowWaitHighThread>
        int 2Ch
ELSE
        mov     eax, ServiceNumber      ; (eax) = service number
        lea     edx, [esp]+4            ; (edx) -> arguments
        mov     ecx, esp                ; (ecx) = user ESP; [ecx] = ret-to-caller
                                        ; KiFastSystemCall reads [ecx] for user EIP,
                                        ; emulates the stdRET below as part of stdcall
                                        ; callee-pop, and resumes user at caller-of-Zw.
        db      0Fh, 34h                ; SYSENTER — invoke system service via SEP
                                        ; ML 6.11d predates PII; emit raw opcode.
                                        ; Phase 1: kernel returns via IRET.
                                        ; Phase 3: kernel returns via SYSEXIT.
                                        ; Either way control resumes at caller-of-Zw,
                                        ; not the stdRET below (kept as fallback).
ENDIF
ENDIF
        stdRET  _Zw&Name
stdENDP _Zw&Name
endm

USRSTUBS_ENTRY2 macro ServiceNumber, Name, NumArgs
endm
USRSTUBS_ENTRY3 macro ServiceNumber, Name, NumArgs
endm
USRSTUBS_ENTRY4 macro ServiceNumber, Name, NumArgs
endm
USRSTUBS_ENTRY5 macro ServiceNumber, Name, NumArgs
endm
USRSTUBS_ENTRY6 macro ServiceNumber, Name, NumArgs
endm
USRSTUBS_ENTRY7 macro ServiceNumber, Name, NumArgs
endm
USRSTUBS_ENTRY8 macro ServiceNumber, Name, NumArgs
endm

        STUBS_BEGIN1 <"System Service Stub Procedures">
        STUBS_BEGIN2 <"System Service Stub Procedures">
        STUBS_BEGIN3 <"System Service Stub Procedures">
        STUBS_BEGIN4 <"System Service Stub Procedures">
        STUBS_BEGIN5 <"System Service Stub Procedures">
        STUBS_BEGIN6 <"System Service Stub Procedures">
        STUBS_BEGIN7 <"System Service Stub Procedures">
        STUBS_BEGIN8 <"System Service Stub Procedures">

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
; Defined here, before the SYSSTUBS_ENTRY1 invocations, so MASM resolves
; the forward symbol in single-pass assembly.
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
;   eax = service number (16 or 118, already bounds-checked)
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

SYSSTUBS_ENTRY1  0, AcceptConnectPort, 6
SYSSTUBS_ENTRY2  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY3  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY4  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY5  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY6  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY7  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY8  0, AcceptConnectPort, 6 
SYSSTUBS_ENTRY1  1, AccessCheck, 8 
SYSSTUBS_ENTRY2  1, AccessCheck, 8 
SYSSTUBS_ENTRY3  1, AccessCheck, 8 
SYSSTUBS_ENTRY4  1, AccessCheck, 8 
SYSSTUBS_ENTRY5  1, AccessCheck, 8 
SYSSTUBS_ENTRY6  1, AccessCheck, 8 
SYSSTUBS_ENTRY7  1, AccessCheck, 8 
SYSSTUBS_ENTRY8  1, AccessCheck, 8 
SYSSTUBS_ENTRY1  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY2  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY3  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY4  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY5  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY6  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY7  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY8  2, AdjustGroupsToken, 6 
SYSSTUBS_ENTRY1  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY2  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY3  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY4  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY5  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY6  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY7  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY8  3, AdjustPrivilegesToken, 6 
SYSSTUBS_ENTRY1  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY2  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY3  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY4  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY5  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY6  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY7  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY8  4, AlertResumeThread, 2 
SYSSTUBS_ENTRY1  5, AlertThread, 1 
SYSSTUBS_ENTRY2  5, AlertThread, 1 
SYSSTUBS_ENTRY3  5, AlertThread, 1 
SYSSTUBS_ENTRY4  5, AlertThread, 1 
SYSSTUBS_ENTRY5  5, AlertThread, 1 
SYSSTUBS_ENTRY6  5, AlertThread, 1 
SYSSTUBS_ENTRY7  5, AlertThread, 1 
SYSSTUBS_ENTRY8  5, AlertThread, 1 
SYSSTUBS_ENTRY1  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY2  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY3  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY4  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY5  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY6  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY7  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY8  6, AllocateLocallyUniqueId, 1 
SYSSTUBS_ENTRY1  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY2  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY3  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY4  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY5  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY6  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY7  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY8  7, AllocateVirtualMemory, 6 
SYSSTUBS_ENTRY1  8, CancelIoFile, 2 
SYSSTUBS_ENTRY2  8, CancelIoFile, 2 
SYSSTUBS_ENTRY3  8, CancelIoFile, 2 
SYSSTUBS_ENTRY4  8, CancelIoFile, 2 
SYSSTUBS_ENTRY5  8, CancelIoFile, 2 
SYSSTUBS_ENTRY6  8, CancelIoFile, 2 
SYSSTUBS_ENTRY7  8, CancelIoFile, 2 
SYSSTUBS_ENTRY8  8, CancelIoFile, 2 
SYSSTUBS_ENTRY1  9, CancelTimer, 2 
SYSSTUBS_ENTRY2  9, CancelTimer, 2 
SYSSTUBS_ENTRY3  9, CancelTimer, 2 
SYSSTUBS_ENTRY4  9, CancelTimer, 2 
SYSSTUBS_ENTRY5  9, CancelTimer, 2 
SYSSTUBS_ENTRY6  9, CancelTimer, 2 
SYSSTUBS_ENTRY7  9, CancelTimer, 2 
SYSSTUBS_ENTRY8  9, CancelTimer, 2 
SYSSTUBS_ENTRY1  10, ClearEvent, 1 
SYSSTUBS_ENTRY2  10, ClearEvent, 1 
SYSSTUBS_ENTRY3  10, ClearEvent, 1 
SYSSTUBS_ENTRY4  10, ClearEvent, 1 
SYSSTUBS_ENTRY5  10, ClearEvent, 1 
SYSSTUBS_ENTRY6  10, ClearEvent, 1 
SYSSTUBS_ENTRY7  10, ClearEvent, 1 
SYSSTUBS_ENTRY8  10, ClearEvent, 1 
SYSSTUBS_ENTRY1  11, Close, 1 
SYSSTUBS_ENTRY2  11, Close, 1 
SYSSTUBS_ENTRY3  11, Close, 1 
SYSSTUBS_ENTRY4  11, Close, 1 
SYSSTUBS_ENTRY5  11, Close, 1 
SYSSTUBS_ENTRY6  11, Close, 1 
SYSSTUBS_ENTRY7  11, Close, 1 
SYSSTUBS_ENTRY8  11, Close, 1 
SYSSTUBS_ENTRY1  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY2  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY3  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY4  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY5  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY6  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY7  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY8  12, CompleteConnectPort, 1 
SYSSTUBS_ENTRY1  13, ConnectPort, 8 
SYSSTUBS_ENTRY2  13, ConnectPort, 8 
SYSSTUBS_ENTRY3  13, ConnectPort, 8 
SYSSTUBS_ENTRY4  13, ConnectPort, 8 
SYSSTUBS_ENTRY5  13, ConnectPort, 8 
SYSSTUBS_ENTRY6  13, ConnectPort, 8 
SYSSTUBS_ENTRY7  13, ConnectPort, 8 
SYSSTUBS_ENTRY8  13, ConnectPort, 8 
SYSSTUBS_ENTRY1  14, Continue, 2 
SYSSTUBS_ENTRY2  14, Continue, 2 
SYSSTUBS_ENTRY3  14, Continue, 2 
SYSSTUBS_ENTRY4  14, Continue, 2 
SYSSTUBS_ENTRY5  14, Continue, 2 
SYSSTUBS_ENTRY6  14, Continue, 2 
SYSSTUBS_ENTRY7  14, Continue, 2 
SYSSTUBS_ENTRY8  14, Continue, 2 
SYSSTUBS_ENTRY1  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY2  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY3  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY4  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY5  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY6  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY7  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY8  15, CreateDirectoryObject, 3 
SYSSTUBS_ENTRY1  16, CreateEvent, 5 
SYSSTUBS_ENTRY2  16, CreateEvent, 5 
SYSSTUBS_ENTRY3  16, CreateEvent, 5 
SYSSTUBS_ENTRY4  16, CreateEvent, 5 
SYSSTUBS_ENTRY5  16, CreateEvent, 5 
SYSSTUBS_ENTRY6  16, CreateEvent, 5 
SYSSTUBS_ENTRY7  16, CreateEvent, 5 
SYSSTUBS_ENTRY8  16, CreateEvent, 5 
SYSSTUBS_ENTRY1  17, CreateEventPair, 3 
SYSSTUBS_ENTRY2  17, CreateEventPair, 3 
SYSSTUBS_ENTRY3  17, CreateEventPair, 3 
SYSSTUBS_ENTRY4  17, CreateEventPair, 3 
SYSSTUBS_ENTRY5  17, CreateEventPair, 3 
SYSSTUBS_ENTRY6  17, CreateEventPair, 3 
SYSSTUBS_ENTRY7  17, CreateEventPair, 3 
SYSSTUBS_ENTRY8  17, CreateEventPair, 3 
SYSSTUBS_ENTRY1  18, CreateFile, 11 
SYSSTUBS_ENTRY2  18, CreateFile, 11 
SYSSTUBS_ENTRY3  18, CreateFile, 11 
SYSSTUBS_ENTRY4  18, CreateFile, 11 
SYSSTUBS_ENTRY5  18, CreateFile, 11 
SYSSTUBS_ENTRY6  18, CreateFile, 11 
SYSSTUBS_ENTRY7  18, CreateFile, 11 
SYSSTUBS_ENTRY8  18, CreateFile, 11 
SYSSTUBS_ENTRY1  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY2  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY3  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY4  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY5  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY6  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY7  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY8  19, CreateIoCompletion, 4 
SYSSTUBS_ENTRY1  20, CreateKey, 7 
SYSSTUBS_ENTRY2  20, CreateKey, 7 
SYSSTUBS_ENTRY3  20, CreateKey, 7 
SYSSTUBS_ENTRY4  20, CreateKey, 7 
SYSSTUBS_ENTRY5  20, CreateKey, 7 
SYSSTUBS_ENTRY6  20, CreateKey, 7 
SYSSTUBS_ENTRY7  20, CreateKey, 7 
SYSSTUBS_ENTRY8  20, CreateKey, 7 
SYSSTUBS_ENTRY1  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY2  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY3  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY4  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY5  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY6  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY7  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY8  21, CreateMailslotFile, 8 
SYSSTUBS_ENTRY1  22, CreateMutant, 4 
SYSSTUBS_ENTRY2  22, CreateMutant, 4 
SYSSTUBS_ENTRY3  22, CreateMutant, 4 
SYSSTUBS_ENTRY4  22, CreateMutant, 4 
SYSSTUBS_ENTRY5  22, CreateMutant, 4 
SYSSTUBS_ENTRY6  22, CreateMutant, 4 
SYSSTUBS_ENTRY7  22, CreateMutant, 4 
SYSSTUBS_ENTRY8  22, CreateMutant, 4 
SYSSTUBS_ENTRY1  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY2  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY3  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY4  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY5  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY6  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY7  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY8  23, CreateNamedPipeFile, 14 
SYSSTUBS_ENTRY1  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY2  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY3  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY4  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY5  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY6  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY7  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY8  24, CreatePagingFile, 4 
SYSSTUBS_ENTRY1  25, CreatePort, 5 
SYSSTUBS_ENTRY2  25, CreatePort, 5 
SYSSTUBS_ENTRY3  25, CreatePort, 5 
SYSSTUBS_ENTRY4  25, CreatePort, 5 
SYSSTUBS_ENTRY5  25, CreatePort, 5 
SYSSTUBS_ENTRY6  25, CreatePort, 5 
SYSSTUBS_ENTRY7  25, CreatePort, 5 
SYSSTUBS_ENTRY8  25, CreatePort, 5 
SYSSTUBS_ENTRY1  26, CreateProcess, 8 
SYSSTUBS_ENTRY2  26, CreateProcess, 8 
SYSSTUBS_ENTRY3  26, CreateProcess, 8 
SYSSTUBS_ENTRY4  26, CreateProcess, 8 
SYSSTUBS_ENTRY5  26, CreateProcess, 8 
SYSSTUBS_ENTRY6  26, CreateProcess, 8 
SYSSTUBS_ENTRY7  26, CreateProcess, 8 
SYSSTUBS_ENTRY8  26, CreateProcess, 8 
SYSSTUBS_ENTRY1  27, CreateProfile, 7 
SYSSTUBS_ENTRY2  27, CreateProfile, 7 
SYSSTUBS_ENTRY3  27, CreateProfile, 7 
SYSSTUBS_ENTRY4  27, CreateProfile, 7 
SYSSTUBS_ENTRY5  27, CreateProfile, 7 
SYSSTUBS_ENTRY6  27, CreateProfile, 7 
SYSSTUBS_ENTRY7  27, CreateProfile, 7 
SYSSTUBS_ENTRY8  27, CreateProfile, 7 
SYSSTUBS_ENTRY1  28, CreateSection, 7 
SYSSTUBS_ENTRY2  28, CreateSection, 7 
SYSSTUBS_ENTRY3  28, CreateSection, 7 
SYSSTUBS_ENTRY4  28, CreateSection, 7 
SYSSTUBS_ENTRY5  28, CreateSection, 7 
SYSSTUBS_ENTRY6  28, CreateSection, 7 
SYSSTUBS_ENTRY7  28, CreateSection, 7 
SYSSTUBS_ENTRY8  28, CreateSection, 7 
SYSSTUBS_ENTRY1  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY2  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY3  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY4  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY5  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY6  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY7  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY8  29, CreateSemaphore, 5 
SYSSTUBS_ENTRY1  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY2  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY3  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY4  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY5  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY6  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY7  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY8  30, CreateSymbolicLinkObject, 4 
SYSSTUBS_ENTRY1  31, CreateThread, 8 
SYSSTUBS_ENTRY2  31, CreateThread, 8 
SYSSTUBS_ENTRY3  31, CreateThread, 8 
SYSSTUBS_ENTRY4  31, CreateThread, 8 
SYSSTUBS_ENTRY5  31, CreateThread, 8 
SYSSTUBS_ENTRY6  31, CreateThread, 8 
SYSSTUBS_ENTRY7  31, CreateThread, 8 
SYSSTUBS_ENTRY8  31, CreateThread, 8 
SYSSTUBS_ENTRY1  32, CreateTimer, 3 
SYSSTUBS_ENTRY2  32, CreateTimer, 3 
SYSSTUBS_ENTRY3  32, CreateTimer, 3 
SYSSTUBS_ENTRY4  32, CreateTimer, 3 
SYSSTUBS_ENTRY5  32, CreateTimer, 3 
SYSSTUBS_ENTRY6  32, CreateTimer, 3 
SYSSTUBS_ENTRY7  32, CreateTimer, 3 
SYSSTUBS_ENTRY8  32, CreateTimer, 3 
SYSSTUBS_ENTRY1  33, CreateToken, 13 
SYSSTUBS_ENTRY2  33, CreateToken, 13 
SYSSTUBS_ENTRY3  33, CreateToken, 13 
SYSSTUBS_ENTRY4  33, CreateToken, 13 
SYSSTUBS_ENTRY5  33, CreateToken, 13 
SYSSTUBS_ENTRY6  33, CreateToken, 13 
SYSSTUBS_ENTRY7  33, CreateToken, 13 
SYSSTUBS_ENTRY8  33, CreateToken, 13 
SYSSTUBS_ENTRY1  34, DelayExecution, 2 
SYSSTUBS_ENTRY2  34, DelayExecution, 2 
SYSSTUBS_ENTRY3  34, DelayExecution, 2 
SYSSTUBS_ENTRY4  34, DelayExecution, 2 
SYSSTUBS_ENTRY5  34, DelayExecution, 2 
SYSSTUBS_ENTRY6  34, DelayExecution, 2 
SYSSTUBS_ENTRY7  34, DelayExecution, 2 
SYSSTUBS_ENTRY8  34, DelayExecution, 2 
SYSSTUBS_ENTRY1  35, DeleteFile, 1 
SYSSTUBS_ENTRY2  35, DeleteFile, 1 
SYSSTUBS_ENTRY3  35, DeleteFile, 1 
SYSSTUBS_ENTRY4  35, DeleteFile, 1 
SYSSTUBS_ENTRY5  35, DeleteFile, 1 
SYSSTUBS_ENTRY6  35, DeleteFile, 1 
SYSSTUBS_ENTRY7  35, DeleteFile, 1 
SYSSTUBS_ENTRY8  35, DeleteFile, 1 
SYSSTUBS_ENTRY1  36, DeleteKey, 1 
SYSSTUBS_ENTRY2  36, DeleteKey, 1 
SYSSTUBS_ENTRY3  36, DeleteKey, 1 
SYSSTUBS_ENTRY4  36, DeleteKey, 1 
SYSSTUBS_ENTRY5  36, DeleteKey, 1 
SYSSTUBS_ENTRY6  36, DeleteKey, 1 
SYSSTUBS_ENTRY7  36, DeleteKey, 1 
SYSSTUBS_ENTRY8  36, DeleteKey, 1 
SYSSTUBS_ENTRY1  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY2  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY3  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY4  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY5  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY6  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY7  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY8  37, DeleteValueKey, 2 
SYSSTUBS_ENTRY1  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY2  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY3  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY4  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY5  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY6  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY7  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY8  38, DeviceIoControlFile, 10 
SYSSTUBS_ENTRY1  39, DisplayString, 1 
SYSSTUBS_ENTRY2  39, DisplayString, 1 
SYSSTUBS_ENTRY3  39, DisplayString, 1 
SYSSTUBS_ENTRY4  39, DisplayString, 1 
SYSSTUBS_ENTRY5  39, DisplayString, 1 
SYSSTUBS_ENTRY6  39, DisplayString, 1 
SYSSTUBS_ENTRY7  39, DisplayString, 1 
SYSSTUBS_ENTRY8  39, DisplayString, 1 
SYSSTUBS_ENTRY1  40, DuplicateObject, 7 
SYSSTUBS_ENTRY2  40, DuplicateObject, 7 
SYSSTUBS_ENTRY3  40, DuplicateObject, 7 
SYSSTUBS_ENTRY4  40, DuplicateObject, 7 
SYSSTUBS_ENTRY5  40, DuplicateObject, 7 
SYSSTUBS_ENTRY6  40, DuplicateObject, 7 
SYSSTUBS_ENTRY7  40, DuplicateObject, 7 
SYSSTUBS_ENTRY8  40, DuplicateObject, 7 
SYSSTUBS_ENTRY1  41, DuplicateToken, 6 
SYSSTUBS_ENTRY2  41, DuplicateToken, 6 
SYSSTUBS_ENTRY3  41, DuplicateToken, 6 
SYSSTUBS_ENTRY4  41, DuplicateToken, 6 
SYSSTUBS_ENTRY5  41, DuplicateToken, 6 
SYSSTUBS_ENTRY6  41, DuplicateToken, 6 
SYSSTUBS_ENTRY7  41, DuplicateToken, 6 
SYSSTUBS_ENTRY8  41, DuplicateToken, 6 
SYSSTUBS_ENTRY1  42, EnumerateKey, 6 
SYSSTUBS_ENTRY2  42, EnumerateKey, 6 
SYSSTUBS_ENTRY3  42, EnumerateKey, 6 
SYSSTUBS_ENTRY4  42, EnumerateKey, 6 
SYSSTUBS_ENTRY5  42, EnumerateKey, 6 
SYSSTUBS_ENTRY6  42, EnumerateKey, 6 
SYSSTUBS_ENTRY7  42, EnumerateKey, 6 
SYSSTUBS_ENTRY8  42, EnumerateKey, 6 
SYSSTUBS_ENTRY1  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY2  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY3  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY4  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY5  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY6  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY7  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY8  43, EnumerateValueKey, 6 
SYSSTUBS_ENTRY1  44, ExtendSection, 2 
SYSSTUBS_ENTRY2  44, ExtendSection, 2 
SYSSTUBS_ENTRY3  44, ExtendSection, 2 
SYSSTUBS_ENTRY4  44, ExtendSection, 2 
SYSSTUBS_ENTRY5  44, ExtendSection, 2 
SYSSTUBS_ENTRY6  44, ExtendSection, 2 
SYSSTUBS_ENTRY7  44, ExtendSection, 2 
SYSSTUBS_ENTRY8  44, ExtendSection, 2 
SYSSTUBS_ENTRY1  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY2  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY3  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY4  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY5  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY6  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY7  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY8  45, FlushBuffersFile, 2 
SYSSTUBS_ENTRY1  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY2  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY3  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY4  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY5  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY6  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY7  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY8  46, FlushInstructionCache, 3 
SYSSTUBS_ENTRY1  47, FlushKey, 1 
SYSSTUBS_ENTRY2  47, FlushKey, 1 
SYSSTUBS_ENTRY3  47, FlushKey, 1 
SYSSTUBS_ENTRY4  47, FlushKey, 1 
SYSSTUBS_ENTRY5  47, FlushKey, 1 
SYSSTUBS_ENTRY6  47, FlushKey, 1 
SYSSTUBS_ENTRY7  47, FlushKey, 1 
SYSSTUBS_ENTRY8  47, FlushKey, 1 
SYSSTUBS_ENTRY1  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY2  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY3  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY4  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY5  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY6  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY7  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY8  48, FlushVirtualMemory, 4 
SYSSTUBS_ENTRY1  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY2  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY3  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY4  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY5  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY6  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY7  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY8  49, FlushWriteBuffer, 0 
SYSSTUBS_ENTRY1  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY2  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY3  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY4  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY5  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY6  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY7  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY8  50, FreeVirtualMemory, 4 
SYSSTUBS_ENTRY1  51, FsControlFile, 10 
SYSSTUBS_ENTRY2  51, FsControlFile, 10 
SYSSTUBS_ENTRY3  51, FsControlFile, 10 
SYSSTUBS_ENTRY4  51, FsControlFile, 10 
SYSSTUBS_ENTRY5  51, FsControlFile, 10 
SYSSTUBS_ENTRY6  51, FsControlFile, 10 
SYSSTUBS_ENTRY7  51, FsControlFile, 10 
SYSSTUBS_ENTRY8  51, FsControlFile, 10 
SYSSTUBS_ENTRY1  52, GetContextThread, 2 
SYSSTUBS_ENTRY2  52, GetContextThread, 2 
SYSSTUBS_ENTRY3  52, GetContextThread, 2 
SYSSTUBS_ENTRY4  52, GetContextThread, 2 
SYSSTUBS_ENTRY5  52, GetContextThread, 2 
SYSSTUBS_ENTRY6  52, GetContextThread, 2 
SYSSTUBS_ENTRY7  52, GetContextThread, 2 
SYSSTUBS_ENTRY8  52, GetContextThread, 2 
SYSSTUBS_ENTRY1  53, GetTickCount, 0 
SYSSTUBS_ENTRY2  53, GetTickCount, 0 
SYSSTUBS_ENTRY3  53, GetTickCount, 0 
SYSSTUBS_ENTRY4  53, GetTickCount, 0 
SYSSTUBS_ENTRY5  53, GetTickCount, 0 
SYSSTUBS_ENTRY6  53, GetTickCount, 0 
SYSSTUBS_ENTRY7  53, GetTickCount, 0 
SYSSTUBS_ENTRY8  53, GetTickCount, 0 
SYSSTUBS_ENTRY1  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY2  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY3  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY4  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY5  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY6  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY7  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY8  54, ImpersonateClientOfPort, 2 
SYSSTUBS_ENTRY1  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY2  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY3  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY4  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY5  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY6  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY7  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY8  55, ImpersonateThread, 3 
SYSSTUBS_ENTRY1  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY2  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY3  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY4  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY5  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY6  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY7  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY8  56, InitializeRegistry, 1 
SYSSTUBS_ENTRY1  57, ListenPort, 2 
SYSSTUBS_ENTRY2  57, ListenPort, 2 
SYSSTUBS_ENTRY3  57, ListenPort, 2 
SYSSTUBS_ENTRY4  57, ListenPort, 2 
SYSSTUBS_ENTRY5  57, ListenPort, 2 
SYSSTUBS_ENTRY6  57, ListenPort, 2 
SYSSTUBS_ENTRY7  57, ListenPort, 2 
SYSSTUBS_ENTRY8  57, ListenPort, 2 
SYSSTUBS_ENTRY1  58, LoadDriver, 1 
SYSSTUBS_ENTRY2  58, LoadDriver, 1 
SYSSTUBS_ENTRY3  58, LoadDriver, 1 
SYSSTUBS_ENTRY4  58, LoadDriver, 1 
SYSSTUBS_ENTRY5  58, LoadDriver, 1 
SYSSTUBS_ENTRY6  58, LoadDriver, 1 
SYSSTUBS_ENTRY7  58, LoadDriver, 1 
SYSSTUBS_ENTRY8  58, LoadDriver, 1 
SYSSTUBS_ENTRY1  59, LoadKey, 2 
SYSSTUBS_ENTRY2  59, LoadKey, 2 
SYSSTUBS_ENTRY3  59, LoadKey, 2 
SYSSTUBS_ENTRY4  59, LoadKey, 2 
SYSSTUBS_ENTRY5  59, LoadKey, 2 
SYSSTUBS_ENTRY6  59, LoadKey, 2 
SYSSTUBS_ENTRY7  59, LoadKey, 2 
SYSSTUBS_ENTRY8  59, LoadKey, 2 
SYSSTUBS_ENTRY1  60, LockFile, 10 
SYSSTUBS_ENTRY2  60, LockFile, 10 
SYSSTUBS_ENTRY3  60, LockFile, 10 
SYSSTUBS_ENTRY4  60, LockFile, 10 
SYSSTUBS_ENTRY5  60, LockFile, 10 
SYSSTUBS_ENTRY6  60, LockFile, 10 
SYSSTUBS_ENTRY7  60, LockFile, 10 
SYSSTUBS_ENTRY8  60, LockFile, 10 
SYSSTUBS_ENTRY1  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY2  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY3  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY4  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY5  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY6  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY7  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY8  61, LockVirtualMemory, 4 
SYSSTUBS_ENTRY1  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY2  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY3  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY4  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY5  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY6  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY7  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY8  62, MakeTemporaryObject, 1 
SYSSTUBS_ENTRY1  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY2  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY3  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY4  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY5  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY6  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY7  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY8  63, MapViewOfSection, 10 
SYSSTUBS_ENTRY1  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY2  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY3  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY4  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY5  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY6  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY7  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY8  64, NotifyChangeDirectoryFile, 9 
SYSSTUBS_ENTRY1  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY2  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY3  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY4  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY5  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY6  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY7  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY8  65, NotifyChangeKey, 10 
SYSSTUBS_ENTRY1  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY2  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY3  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY4  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY5  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY6  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY7  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY8  66, OpenDirectoryObject, 3 
SYSSTUBS_ENTRY1  67, OpenEvent, 3 
SYSSTUBS_ENTRY2  67, OpenEvent, 3 
SYSSTUBS_ENTRY3  67, OpenEvent, 3 
SYSSTUBS_ENTRY4  67, OpenEvent, 3 
SYSSTUBS_ENTRY5  67, OpenEvent, 3 
SYSSTUBS_ENTRY6  67, OpenEvent, 3 
SYSSTUBS_ENTRY7  67, OpenEvent, 3 
SYSSTUBS_ENTRY8  67, OpenEvent, 3 
SYSSTUBS_ENTRY1  68, OpenEventPair, 3 
SYSSTUBS_ENTRY2  68, OpenEventPair, 3 
SYSSTUBS_ENTRY3  68, OpenEventPair, 3 
SYSSTUBS_ENTRY4  68, OpenEventPair, 3 
SYSSTUBS_ENTRY5  68, OpenEventPair, 3 
SYSSTUBS_ENTRY6  68, OpenEventPair, 3 
SYSSTUBS_ENTRY7  68, OpenEventPair, 3 
SYSSTUBS_ENTRY8  68, OpenEventPair, 3 
SYSSTUBS_ENTRY1  69, OpenFile, 6 
SYSSTUBS_ENTRY2  69, OpenFile, 6 
SYSSTUBS_ENTRY3  69, OpenFile, 6 
SYSSTUBS_ENTRY4  69, OpenFile, 6 
SYSSTUBS_ENTRY5  69, OpenFile, 6 
SYSSTUBS_ENTRY6  69, OpenFile, 6 
SYSSTUBS_ENTRY7  69, OpenFile, 6 
SYSSTUBS_ENTRY8  69, OpenFile, 6 
SYSSTUBS_ENTRY1  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY2  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY3  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY4  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY5  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY6  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY7  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY8  70, OpenIoCompletion, 3 
SYSSTUBS_ENTRY1  71, OpenKey, 3 
SYSSTUBS_ENTRY2  71, OpenKey, 3 
SYSSTUBS_ENTRY3  71, OpenKey, 3 
SYSSTUBS_ENTRY4  71, OpenKey, 3 
SYSSTUBS_ENTRY5  71, OpenKey, 3 
SYSSTUBS_ENTRY6  71, OpenKey, 3 
SYSSTUBS_ENTRY7  71, OpenKey, 3 
SYSSTUBS_ENTRY8  71, OpenKey, 3 
SYSSTUBS_ENTRY1  72, OpenMutant, 3 
SYSSTUBS_ENTRY2  72, OpenMutant, 3 
SYSSTUBS_ENTRY3  72, OpenMutant, 3 
SYSSTUBS_ENTRY4  72, OpenMutant, 3 
SYSSTUBS_ENTRY5  72, OpenMutant, 3 
SYSSTUBS_ENTRY6  72, OpenMutant, 3 
SYSSTUBS_ENTRY7  72, OpenMutant, 3 
SYSSTUBS_ENTRY8  72, OpenMutant, 3 
SYSSTUBS_ENTRY1  73, OpenProcess, 4 
SYSSTUBS_ENTRY2  73, OpenProcess, 4 
SYSSTUBS_ENTRY3  73, OpenProcess, 4 
SYSSTUBS_ENTRY4  73, OpenProcess, 4 
SYSSTUBS_ENTRY5  73, OpenProcess, 4 
SYSSTUBS_ENTRY6  73, OpenProcess, 4 
SYSSTUBS_ENTRY7  73, OpenProcess, 4 
SYSSTUBS_ENTRY8  73, OpenProcess, 4 
SYSSTUBS_ENTRY1  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY2  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY3  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY4  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY5  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY6  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY7  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY8  74, OpenProcessToken, 3 
SYSSTUBS_ENTRY1  75, OpenSection, 3 
SYSSTUBS_ENTRY2  75, OpenSection, 3 
SYSSTUBS_ENTRY3  75, OpenSection, 3 
SYSSTUBS_ENTRY4  75, OpenSection, 3 
SYSSTUBS_ENTRY5  75, OpenSection, 3 
SYSSTUBS_ENTRY6  75, OpenSection, 3 
SYSSTUBS_ENTRY7  75, OpenSection, 3 
SYSSTUBS_ENTRY8  75, OpenSection, 3 
SYSSTUBS_ENTRY1  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY2  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY3  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY4  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY5  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY6  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY7  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY8  76, OpenSemaphore, 3 
SYSSTUBS_ENTRY1  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY2  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY3  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY4  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY5  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY6  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY7  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY8  77, OpenSymbolicLinkObject, 3 
SYSSTUBS_ENTRY1  78, OpenThread, 4 
SYSSTUBS_ENTRY2  78, OpenThread, 4 
SYSSTUBS_ENTRY3  78, OpenThread, 4 
SYSSTUBS_ENTRY4  78, OpenThread, 4 
SYSSTUBS_ENTRY5  78, OpenThread, 4 
SYSSTUBS_ENTRY6  78, OpenThread, 4 
SYSSTUBS_ENTRY7  78, OpenThread, 4 
SYSSTUBS_ENTRY8  78, OpenThread, 4 
SYSSTUBS_ENTRY1  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY2  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY3  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY4  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY5  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY6  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY7  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY8  79, OpenThreadToken, 4 
SYSSTUBS_ENTRY1  80, OpenTimer, 3 
SYSSTUBS_ENTRY2  80, OpenTimer, 3 
SYSSTUBS_ENTRY3  80, OpenTimer, 3 
SYSSTUBS_ENTRY4  80, OpenTimer, 3 
SYSSTUBS_ENTRY5  80, OpenTimer, 3 
SYSSTUBS_ENTRY6  80, OpenTimer, 3 
SYSSTUBS_ENTRY7  80, OpenTimer, 3 
SYSSTUBS_ENTRY8  80, OpenTimer, 3 
SYSSTUBS_ENTRY1  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY2  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY3  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY4  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY5  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY6  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY7  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY8  81, PrivilegeCheck, 3 
SYSSTUBS_ENTRY1  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY2  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY3  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY4  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY5  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY6  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY7  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY8  82, ProtectVirtualMemory, 5 
SYSSTUBS_ENTRY1  83, PulseEvent, 2 
SYSSTUBS_ENTRY2  83, PulseEvent, 2 
SYSSTUBS_ENTRY3  83, PulseEvent, 2 
SYSSTUBS_ENTRY4  83, PulseEvent, 2 
SYSSTUBS_ENTRY5  83, PulseEvent, 2 
SYSSTUBS_ENTRY6  83, PulseEvent, 2 
SYSSTUBS_ENTRY7  83, PulseEvent, 2 
SYSSTUBS_ENTRY8  83, PulseEvent, 2 
SYSSTUBS_ENTRY1  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY2  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY3  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY4  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY5  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY6  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY7  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY8  84, QueryAttributesFile, 2 
SYSSTUBS_ENTRY1  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY2  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY3  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY4  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY5  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY6  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY7  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY8  85, QueryDefaultLocale, 2 
SYSSTUBS_ENTRY1  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY2  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY3  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY4  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY5  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY6  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY7  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY8  86, QueryDirectoryFile, 11 
SYSSTUBS_ENTRY1  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY2  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY3  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY4  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY5  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY6  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY7  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY8  87, QueryDirectoryObject, 7 
SYSSTUBS_ENTRY1  88, QueryEaFile, 9 
SYSSTUBS_ENTRY2  88, QueryEaFile, 9 
SYSSTUBS_ENTRY3  88, QueryEaFile, 9 
SYSSTUBS_ENTRY4  88, QueryEaFile, 9 
SYSSTUBS_ENTRY5  88, QueryEaFile, 9 
SYSSTUBS_ENTRY6  88, QueryEaFile, 9 
SYSSTUBS_ENTRY7  88, QueryEaFile, 9 
SYSSTUBS_ENTRY8  88, QueryEaFile, 9 
SYSSTUBS_ENTRY1  89, QueryEvent, 5 
SYSSTUBS_ENTRY2  89, QueryEvent, 5 
SYSSTUBS_ENTRY3  89, QueryEvent, 5 
SYSSTUBS_ENTRY4  89, QueryEvent, 5 
SYSSTUBS_ENTRY5  89, QueryEvent, 5 
SYSSTUBS_ENTRY6  89, QueryEvent, 5 
SYSSTUBS_ENTRY7  89, QueryEvent, 5 
SYSSTUBS_ENTRY8  89, QueryEvent, 5 
SYSSTUBS_ENTRY1  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY2  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY3  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY4  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY5  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY6  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY7  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY8  90, QueryInformationFile, 5 
SYSSTUBS_ENTRY1  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY2  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY3  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY4  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY5  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY6  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY7  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY8  91, QueryIoCompletion, 5 
SYSSTUBS_ENTRY1  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY2  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY3  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY4  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY5  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY6  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY7  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY8  92, QueryInformationPort, 5 
SYSSTUBS_ENTRY1  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY2  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY3  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY4  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY5  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY6  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY7  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY8  93, QueryInformationProcess, 5 
SYSSTUBS_ENTRY1  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY2  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY3  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY4  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY5  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY6  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY7  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY8  94, QueryInformationThread, 5 
SYSSTUBS_ENTRY1  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY2  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY3  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY4  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY5  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY6  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY7  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY8  95, QueryInformationToken, 5 
SYSSTUBS_ENTRY1  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY2  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY3  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY4  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY5  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY6  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY7  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY8  96, QueryIntervalProfile, 1 
SYSSTUBS_ENTRY1  97, QueryKey, 5 
SYSSTUBS_ENTRY2  97, QueryKey, 5 
SYSSTUBS_ENTRY3  97, QueryKey, 5 
SYSSTUBS_ENTRY4  97, QueryKey, 5 
SYSSTUBS_ENTRY5  97, QueryKey, 5 
SYSSTUBS_ENTRY6  97, QueryKey, 5 
SYSSTUBS_ENTRY7  97, QueryKey, 5 
SYSSTUBS_ENTRY8  97, QueryKey, 5 
SYSSTUBS_ENTRY1  98, QueryMutant, 5 
SYSSTUBS_ENTRY2  98, QueryMutant, 5 
SYSSTUBS_ENTRY3  98, QueryMutant, 5 
SYSSTUBS_ENTRY4  98, QueryMutant, 5 
SYSSTUBS_ENTRY5  98, QueryMutant, 5 
SYSSTUBS_ENTRY6  98, QueryMutant, 5 
SYSSTUBS_ENTRY7  98, QueryMutant, 5 
SYSSTUBS_ENTRY8  98, QueryMutant, 5 
SYSSTUBS_ENTRY1  99, QueryObject, 5 
SYSSTUBS_ENTRY2  99, QueryObject, 5 
SYSSTUBS_ENTRY3  99, QueryObject, 5 
SYSSTUBS_ENTRY4  99, QueryObject, 5 
SYSSTUBS_ENTRY5  99, QueryObject, 5 
SYSSTUBS_ENTRY6  99, QueryObject, 5 
SYSSTUBS_ENTRY7  99, QueryObject, 5 
SYSSTUBS_ENTRY8  99, QueryObject, 5 
SYSSTUBS_ENTRY1  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY2  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY3  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY4  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY5  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY6  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY7  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY8  100, QueryPerformanceCounter, 2 
SYSSTUBS_ENTRY1  101, QuerySection, 5 
SYSSTUBS_ENTRY2  101, QuerySection, 5 
SYSSTUBS_ENTRY3  101, QuerySection, 5 
SYSSTUBS_ENTRY4  101, QuerySection, 5 
SYSSTUBS_ENTRY5  101, QuerySection, 5 
SYSSTUBS_ENTRY6  101, QuerySection, 5 
SYSSTUBS_ENTRY7  101, QuerySection, 5 
SYSSTUBS_ENTRY8  101, QuerySection, 5 
SYSSTUBS_ENTRY1  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY2  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY3  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY4  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY5  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY6  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY7  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY8  102, QuerySecurityObject, 5 
SYSSTUBS_ENTRY1  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY2  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY3  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY4  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY5  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY6  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY7  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY8  103, QuerySemaphore, 5 
SYSSTUBS_ENTRY1  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY2  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY3  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY4  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY5  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY6  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY7  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY8  104, QuerySymbolicLinkObject, 3 
SYSSTUBS_ENTRY1  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY2  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY3  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY4  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY5  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY6  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY7  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY8  105, QuerySystemEnvironmentValue, 4 
SYSSTUBS_ENTRY1  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY2  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY3  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY4  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY5  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY6  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY7  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY8  106, QuerySystemInformation, 4 
SYSSTUBS_ENTRY1  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY2  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY3  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY4  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY5  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY6  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY7  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY8  107, QuerySystemTime, 1 
SYSSTUBS_ENTRY1  108, QueryTimer, 5 
SYSSTUBS_ENTRY2  108, QueryTimer, 5 
SYSSTUBS_ENTRY3  108, QueryTimer, 5 
SYSSTUBS_ENTRY4  108, QueryTimer, 5 
SYSSTUBS_ENTRY5  108, QueryTimer, 5 
SYSSTUBS_ENTRY6  108, QueryTimer, 5 
SYSSTUBS_ENTRY7  108, QueryTimer, 5 
SYSSTUBS_ENTRY8  108, QueryTimer, 5 
SYSSTUBS_ENTRY1  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY2  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY3  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY4  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY5  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY6  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY7  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY8  109, QueryTimerResolution, 3 
SYSSTUBS_ENTRY1  110, QueryValueKey, 6 
SYSSTUBS_ENTRY2  110, QueryValueKey, 6 
SYSSTUBS_ENTRY3  110, QueryValueKey, 6 
SYSSTUBS_ENTRY4  110, QueryValueKey, 6 
SYSSTUBS_ENTRY5  110, QueryValueKey, 6 
SYSSTUBS_ENTRY6  110, QueryValueKey, 6 
SYSSTUBS_ENTRY7  110, QueryValueKey, 6 
SYSSTUBS_ENTRY8  110, QueryValueKey, 6 
SYSSTUBS_ENTRY1  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY2  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY3  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY4  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY5  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY6  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY7  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY8  111, QueryVirtualMemory, 6 
SYSSTUBS_ENTRY1  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY2  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY3  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY4  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY5  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY6  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY7  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY8  112, QueryVolumeInformationFile, 5 
SYSSTUBS_ENTRY1  113, RaiseException, 3 
SYSSTUBS_ENTRY2  113, RaiseException, 3 
SYSSTUBS_ENTRY3  113, RaiseException, 3 
SYSSTUBS_ENTRY4  113, RaiseException, 3 
SYSSTUBS_ENTRY5  113, RaiseException, 3 
SYSSTUBS_ENTRY6  113, RaiseException, 3 
SYSSTUBS_ENTRY7  113, RaiseException, 3 
SYSSTUBS_ENTRY8  113, RaiseException, 3 
SYSSTUBS_ENTRY1  114, RaiseHardError, 6 
SYSSTUBS_ENTRY2  114, RaiseHardError, 6 
SYSSTUBS_ENTRY3  114, RaiseHardError, 6 
SYSSTUBS_ENTRY4  114, RaiseHardError, 6 
SYSSTUBS_ENTRY5  114, RaiseHardError, 6 
SYSSTUBS_ENTRY6  114, RaiseHardError, 6 
SYSSTUBS_ENTRY7  114, RaiseHardError, 6 
SYSSTUBS_ENTRY8  114, RaiseHardError, 6 
SYSSTUBS_ENTRY1  115, ReadFile, 9 
SYSSTUBS_ENTRY2  115, ReadFile, 9 
SYSSTUBS_ENTRY3  115, ReadFile, 9 
SYSSTUBS_ENTRY4  115, ReadFile, 9 
SYSSTUBS_ENTRY5  115, ReadFile, 9 
SYSSTUBS_ENTRY6  115, ReadFile, 9 
SYSSTUBS_ENTRY7  115, ReadFile, 9 
SYSSTUBS_ENTRY8  115, ReadFile, 9 
SYSSTUBS_ENTRY1  116, ReadRequestData, 6 
SYSSTUBS_ENTRY2  116, ReadRequestData, 6 
SYSSTUBS_ENTRY3  116, ReadRequestData, 6 
SYSSTUBS_ENTRY4  116, ReadRequestData, 6 
SYSSTUBS_ENTRY5  116, ReadRequestData, 6 
SYSSTUBS_ENTRY6  116, ReadRequestData, 6 
SYSSTUBS_ENTRY7  116, ReadRequestData, 6 
SYSSTUBS_ENTRY8  116, ReadRequestData, 6 
SYSSTUBS_ENTRY1  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY2  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY3  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY4  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY5  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY6  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY7  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY8  117, ReadVirtualMemory, 5 
SYSSTUBS_ENTRY1  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY2  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY3  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY4  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY5  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY6  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY7  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY8  118, RegisterThreadTerminatePort, 1 
SYSSTUBS_ENTRY1  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY2  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY3  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY4  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY5  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY6  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY7  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY8  119, ReleaseMutant, 2 
SYSSTUBS_ENTRY1  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY2  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY3  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY4  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY5  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY6  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY7  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY8  120, ReleaseProcessMutant, 0 
SYSSTUBS_ENTRY1  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY2  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY3  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY4  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY5  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY6  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY7  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY8  121, ReleaseSemaphore, 3 
SYSSTUBS_ENTRY1  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY2  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY3  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY4  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY5  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY6  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY7  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY8  122, RemoveIoCompletion, 5 
SYSSTUBS_ENTRY1  123, ReplaceKey, 3 
SYSSTUBS_ENTRY2  123, ReplaceKey, 3 
SYSSTUBS_ENTRY3  123, ReplaceKey, 3 
SYSSTUBS_ENTRY4  123, ReplaceKey, 3 
SYSSTUBS_ENTRY5  123, ReplaceKey, 3 
SYSSTUBS_ENTRY6  123, ReplaceKey, 3 
SYSSTUBS_ENTRY7  123, ReplaceKey, 3 
SYSSTUBS_ENTRY8  123, ReplaceKey, 3 
SYSSTUBS_ENTRY1  124, ReplyPort, 2 
SYSSTUBS_ENTRY2  124, ReplyPort, 2 
SYSSTUBS_ENTRY3  124, ReplyPort, 2 
SYSSTUBS_ENTRY4  124, ReplyPort, 2 
SYSSTUBS_ENTRY5  124, ReplyPort, 2 
SYSSTUBS_ENTRY6  124, ReplyPort, 2 
SYSSTUBS_ENTRY7  124, ReplyPort, 2 
SYSSTUBS_ENTRY8  124, ReplyPort, 2 
SYSSTUBS_ENTRY1  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY2  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY3  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY4  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY5  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY6  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY7  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY8  125, ReplyWaitReceivePort, 4 
SYSSTUBS_ENTRY1  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY2  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY3  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY4  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY5  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY6  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY7  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY8  126, ReplyWaitReplyPort, 2 
SYSSTUBS_ENTRY1  127, RequestPort, 2 
SYSSTUBS_ENTRY2  127, RequestPort, 2 
SYSSTUBS_ENTRY3  127, RequestPort, 2 
SYSSTUBS_ENTRY4  127, RequestPort, 2 
SYSSTUBS_ENTRY5  127, RequestPort, 2 
SYSSTUBS_ENTRY6  127, RequestPort, 2 
SYSSTUBS_ENTRY7  127, RequestPort, 2 
SYSSTUBS_ENTRY8  127, RequestPort, 2 
SYSSTUBS_ENTRY1  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY2  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY3  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY4  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY5  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY6  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY7  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY8  128, RequestWaitReplyPort, 3 
SYSSTUBS_ENTRY1  129, ResetEvent, 2 
SYSSTUBS_ENTRY2  129, ResetEvent, 2 
SYSSTUBS_ENTRY3  129, ResetEvent, 2 
SYSSTUBS_ENTRY4  129, ResetEvent, 2 
SYSSTUBS_ENTRY5  129, ResetEvent, 2 
SYSSTUBS_ENTRY6  129, ResetEvent, 2 
SYSSTUBS_ENTRY7  129, ResetEvent, 2 
SYSSTUBS_ENTRY8  129, ResetEvent, 2 
SYSSTUBS_ENTRY1  130, RestoreKey, 3 
SYSSTUBS_ENTRY2  130, RestoreKey, 3 
SYSSTUBS_ENTRY3  130, RestoreKey, 3 
SYSSTUBS_ENTRY4  130, RestoreKey, 3 
SYSSTUBS_ENTRY5  130, RestoreKey, 3 
SYSSTUBS_ENTRY6  130, RestoreKey, 3 
SYSSTUBS_ENTRY7  130, RestoreKey, 3 
SYSSTUBS_ENTRY8  130, RestoreKey, 3 
SYSSTUBS_ENTRY1  131, ResumeThread, 2 
SYSSTUBS_ENTRY2  131, ResumeThread, 2 
SYSSTUBS_ENTRY3  131, ResumeThread, 2 
SYSSTUBS_ENTRY4  131, ResumeThread, 2 
SYSSTUBS_ENTRY5  131, ResumeThread, 2 
SYSSTUBS_ENTRY6  131, ResumeThread, 2 
SYSSTUBS_ENTRY7  131, ResumeThread, 2 
SYSSTUBS_ENTRY8  131, ResumeThread, 2 
SYSSTUBS_ENTRY1  132, SaveKey, 2 
SYSSTUBS_ENTRY2  132, SaveKey, 2 
SYSSTUBS_ENTRY3  132, SaveKey, 2 
SYSSTUBS_ENTRY4  132, SaveKey, 2 
SYSSTUBS_ENTRY5  132, SaveKey, 2 
SYSSTUBS_ENTRY6  132, SaveKey, 2 
SYSSTUBS_ENTRY7  132, SaveKey, 2 
SYSSTUBS_ENTRY8  132, SaveKey, 2 
SYSSTUBS_ENTRY1  133, SetContextThread, 2 
SYSSTUBS_ENTRY2  133, SetContextThread, 2 
SYSSTUBS_ENTRY3  133, SetContextThread, 2 
SYSSTUBS_ENTRY4  133, SetContextThread, 2 
SYSSTUBS_ENTRY5  133, SetContextThread, 2 
SYSSTUBS_ENTRY6  133, SetContextThread, 2 
SYSSTUBS_ENTRY7  133, SetContextThread, 2 
SYSSTUBS_ENTRY8  133, SetContextThread, 2 
SYSSTUBS_ENTRY1  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY2  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY3  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY4  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY5  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY6  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY7  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY8  134, SetDefaultHardErrorPort, 1 
SYSSTUBS_ENTRY1  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY2  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY3  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY4  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY5  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY6  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY7  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY8  135, SetDefaultLocale, 2 
SYSSTUBS_ENTRY1  136, SetEaFile, 4 
SYSSTUBS_ENTRY2  136, SetEaFile, 4 
SYSSTUBS_ENTRY3  136, SetEaFile, 4 
SYSSTUBS_ENTRY4  136, SetEaFile, 4 
SYSSTUBS_ENTRY5  136, SetEaFile, 4 
SYSSTUBS_ENTRY6  136, SetEaFile, 4 
SYSSTUBS_ENTRY7  136, SetEaFile, 4 
SYSSTUBS_ENTRY8  136, SetEaFile, 4 
SYSSTUBS_ENTRY1  137, SetEvent, 2 
SYSSTUBS_ENTRY2  137, SetEvent, 2 
SYSSTUBS_ENTRY3  137, SetEvent, 2 
SYSSTUBS_ENTRY4  137, SetEvent, 2 
SYSSTUBS_ENTRY5  137, SetEvent, 2 
SYSSTUBS_ENTRY6  137, SetEvent, 2 
SYSSTUBS_ENTRY7  137, SetEvent, 2 
SYSSTUBS_ENTRY8  137, SetEvent, 2 
SYSSTUBS_ENTRY1  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY2  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY3  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY4  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY5  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY6  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY7  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY8  138, SetHighEventPair, 1 
SYSSTUBS_ENTRY1  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY2  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY3  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY4  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY5  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY6  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY7  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY8  139, SetHighWaitLowEventPair, 1 
SYSSTUBS_ENTRY1  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY2  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY3  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY4  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY5  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY6  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY7  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY8  140, SetHighWaitLowThread, 0 
SYSSTUBS_ENTRY1  141, SetInformationFile, 5 
SYSSTUBS_ENTRY2  141, SetInformationFile, 5 
SYSSTUBS_ENTRY3  141, SetInformationFile, 5 
SYSSTUBS_ENTRY4  141, SetInformationFile, 5 
SYSSTUBS_ENTRY5  141, SetInformationFile, 5 
SYSSTUBS_ENTRY6  141, SetInformationFile, 5 
SYSSTUBS_ENTRY7  141, SetInformationFile, 5 
SYSSTUBS_ENTRY8  141, SetInformationFile, 5 
SYSSTUBS_ENTRY1  142, SetInformationKey, 4 
SYSSTUBS_ENTRY2  142, SetInformationKey, 4 
SYSSTUBS_ENTRY3  142, SetInformationKey, 4 
SYSSTUBS_ENTRY4  142, SetInformationKey, 4 
SYSSTUBS_ENTRY5  142, SetInformationKey, 4 
SYSSTUBS_ENTRY6  142, SetInformationKey, 4 
SYSSTUBS_ENTRY7  142, SetInformationKey, 4 
SYSSTUBS_ENTRY8  142, SetInformationKey, 4 
SYSSTUBS_ENTRY1  143, SetInformationObject, 4 
SYSSTUBS_ENTRY2  143, SetInformationObject, 4 
SYSSTUBS_ENTRY3  143, SetInformationObject, 4 
SYSSTUBS_ENTRY4  143, SetInformationObject, 4 
SYSSTUBS_ENTRY5  143, SetInformationObject, 4 
SYSSTUBS_ENTRY6  143, SetInformationObject, 4 
SYSSTUBS_ENTRY7  143, SetInformationObject, 4 
SYSSTUBS_ENTRY8  143, SetInformationObject, 4 
SYSSTUBS_ENTRY1  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY2  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY3  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY4  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY5  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY6  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY7  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY8  144, SetInformationProcess, 4 
SYSSTUBS_ENTRY1  145, SetInformationThread, 4 
SYSSTUBS_ENTRY2  145, SetInformationThread, 4 
SYSSTUBS_ENTRY3  145, SetInformationThread, 4 
SYSSTUBS_ENTRY4  145, SetInformationThread, 4 
SYSSTUBS_ENTRY5  145, SetInformationThread, 4 
SYSSTUBS_ENTRY6  145, SetInformationThread, 4 
SYSSTUBS_ENTRY7  145, SetInformationThread, 4 
SYSSTUBS_ENTRY8  145, SetInformationThread, 4 
SYSSTUBS_ENTRY1  146, SetInformationToken, 4 
SYSSTUBS_ENTRY2  146, SetInformationToken, 4 
SYSSTUBS_ENTRY3  146, SetInformationToken, 4 
SYSSTUBS_ENTRY4  146, SetInformationToken, 4 
SYSSTUBS_ENTRY5  146, SetInformationToken, 4 
SYSSTUBS_ENTRY6  146, SetInformationToken, 4 
SYSSTUBS_ENTRY7  146, SetInformationToken, 4 
SYSSTUBS_ENTRY8  146, SetInformationToken, 4 
SYSSTUBS_ENTRY1  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY2  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY3  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY4  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY5  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY6  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY7  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY8  147, SetIntervalProfile, 1 
SYSSTUBS_ENTRY1  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY2  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY3  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY4  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY5  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY6  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY7  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY8  148, SetLdtEntries, 6 
SYSSTUBS_ENTRY1  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY2  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY3  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY4  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY5  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY6  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY7  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY8  149, SetLowEventPair, 1 
SYSSTUBS_ENTRY1  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY2  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY3  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY4  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY5  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY6  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY7  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY8  150, SetLowWaitHighEventPair, 1 
SYSSTUBS_ENTRY1  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY2  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY3  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY4  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY5  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY6  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY7  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY8  151, SetLowWaitHighThread, 0 
SYSSTUBS_ENTRY1  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY2  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY3  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY4  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY5  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY6  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY7  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY8  152, SetSecurityObject, 3 
SYSSTUBS_ENTRY1  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY2  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY3  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY4  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY5  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY6  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY7  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY8  153, SetSystemEnvironmentValue, 2 
SYSSTUBS_ENTRY1  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY2  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY3  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY4  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY5  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY6  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY7  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY8  154, SetSystemInformation, 3 
SYSSTUBS_ENTRY1  155, SetSystemTime, 2 
SYSSTUBS_ENTRY2  155, SetSystemTime, 2 
SYSSTUBS_ENTRY3  155, SetSystemTime, 2 
SYSSTUBS_ENTRY4  155, SetSystemTime, 2 
SYSSTUBS_ENTRY5  155, SetSystemTime, 2 
SYSSTUBS_ENTRY6  155, SetSystemTime, 2 
SYSSTUBS_ENTRY7  155, SetSystemTime, 2 
SYSSTUBS_ENTRY8  155, SetSystemTime, 2 
SYSSTUBS_ENTRY1  156, SetTimer, 5 
SYSSTUBS_ENTRY2  156, SetTimer, 5 
SYSSTUBS_ENTRY3  156, SetTimer, 5 
SYSSTUBS_ENTRY4  156, SetTimer, 5 
SYSSTUBS_ENTRY5  156, SetTimer, 5 
SYSSTUBS_ENTRY6  156, SetTimer, 5 
SYSSTUBS_ENTRY7  156, SetTimer, 5 
SYSSTUBS_ENTRY8  156, SetTimer, 5 
SYSSTUBS_ENTRY1  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY2  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY3  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY4  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY5  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY6  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY7  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY8  157, SetTimerResolution, 3 
SYSSTUBS_ENTRY1  158, SetValueKey, 6 
SYSSTUBS_ENTRY2  158, SetValueKey, 6 
SYSSTUBS_ENTRY3  158, SetValueKey, 6 
SYSSTUBS_ENTRY4  158, SetValueKey, 6 
SYSSTUBS_ENTRY5  158, SetValueKey, 6 
SYSSTUBS_ENTRY6  158, SetValueKey, 6 
SYSSTUBS_ENTRY7  158, SetValueKey, 6 
SYSSTUBS_ENTRY8  158, SetValueKey, 6 
SYSSTUBS_ENTRY1  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY2  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY3  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY4  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY5  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY6  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY7  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY8  159, SetVolumeInformationFile, 5 
SYSSTUBS_ENTRY1  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY2  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY3  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY4  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY5  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY6  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY7  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY8  160, ShutdownSystem, 1 
SYSSTUBS_ENTRY1  161, StartProfile, 1 
SYSSTUBS_ENTRY2  161, StartProfile, 1 
SYSSTUBS_ENTRY3  161, StartProfile, 1 
SYSSTUBS_ENTRY4  161, StartProfile, 1 
SYSSTUBS_ENTRY5  161, StartProfile, 1 
SYSSTUBS_ENTRY6  161, StartProfile, 1 
SYSSTUBS_ENTRY7  161, StartProfile, 1 
SYSSTUBS_ENTRY8  161, StartProfile, 1 
SYSSTUBS_ENTRY1  162, StopProfile, 1 
SYSSTUBS_ENTRY2  162, StopProfile, 1 
SYSSTUBS_ENTRY3  162, StopProfile, 1 
SYSSTUBS_ENTRY4  162, StopProfile, 1 
SYSSTUBS_ENTRY5  162, StopProfile, 1 
SYSSTUBS_ENTRY6  162, StopProfile, 1 
SYSSTUBS_ENTRY7  162, StopProfile, 1 
SYSSTUBS_ENTRY8  162, StopProfile, 1 
SYSSTUBS_ENTRY1  163, SuspendThread, 2 
SYSSTUBS_ENTRY2  163, SuspendThread, 2 
SYSSTUBS_ENTRY3  163, SuspendThread, 2 
SYSSTUBS_ENTRY4  163, SuspendThread, 2 
SYSSTUBS_ENTRY5  163, SuspendThread, 2 
SYSSTUBS_ENTRY6  163, SuspendThread, 2 
SYSSTUBS_ENTRY7  163, SuspendThread, 2 
SYSSTUBS_ENTRY8  163, SuspendThread, 2 
SYSSTUBS_ENTRY1  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY2  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY3  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY4  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY5  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY6  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY7  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY8  164, SystemDebugControl, 6 
SYSSTUBS_ENTRY1  165, TerminateProcess, 2 
SYSSTUBS_ENTRY2  165, TerminateProcess, 2 
SYSSTUBS_ENTRY3  165, TerminateProcess, 2 
SYSSTUBS_ENTRY4  165, TerminateProcess, 2 
SYSSTUBS_ENTRY5  165, TerminateProcess, 2 
SYSSTUBS_ENTRY6  165, TerminateProcess, 2 
SYSSTUBS_ENTRY7  165, TerminateProcess, 2 
SYSSTUBS_ENTRY8  165, TerminateProcess, 2 
SYSSTUBS_ENTRY1  166, TerminateThread, 2 
SYSSTUBS_ENTRY2  166, TerminateThread, 2 
SYSSTUBS_ENTRY3  166, TerminateThread, 2 
SYSSTUBS_ENTRY4  166, TerminateThread, 2 
SYSSTUBS_ENTRY5  166, TerminateThread, 2 
SYSSTUBS_ENTRY6  166, TerminateThread, 2 
SYSSTUBS_ENTRY7  166, TerminateThread, 2 
SYSSTUBS_ENTRY8  166, TerminateThread, 2 
SYSSTUBS_ENTRY1  167, TestAlert, 0 
SYSSTUBS_ENTRY2  167, TestAlert, 0 
SYSSTUBS_ENTRY3  167, TestAlert, 0 
SYSSTUBS_ENTRY4  167, TestAlert, 0 
SYSSTUBS_ENTRY5  167, TestAlert, 0 
SYSSTUBS_ENTRY6  167, TestAlert, 0 
SYSSTUBS_ENTRY7  167, TestAlert, 0 
SYSSTUBS_ENTRY8  167, TestAlert, 0 
SYSSTUBS_ENTRY1  168, UnloadDriver, 1 
SYSSTUBS_ENTRY2  168, UnloadDriver, 1 
SYSSTUBS_ENTRY3  168, UnloadDriver, 1 
SYSSTUBS_ENTRY4  168, UnloadDriver, 1 
SYSSTUBS_ENTRY5  168, UnloadDriver, 1 
SYSSTUBS_ENTRY6  168, UnloadDriver, 1 
SYSSTUBS_ENTRY7  168, UnloadDriver, 1 
SYSSTUBS_ENTRY8  168, UnloadDriver, 1 
SYSSTUBS_ENTRY1  169, UnloadKey, 1 
SYSSTUBS_ENTRY2  169, UnloadKey, 1 
SYSSTUBS_ENTRY3  169, UnloadKey, 1 
SYSSTUBS_ENTRY4  169, UnloadKey, 1 
SYSSTUBS_ENTRY5  169, UnloadKey, 1 
SYSSTUBS_ENTRY6  169, UnloadKey, 1 
SYSSTUBS_ENTRY7  169, UnloadKey, 1 
SYSSTUBS_ENTRY8  169, UnloadKey, 1 
SYSSTUBS_ENTRY1  170, UnlockFile, 5 
SYSSTUBS_ENTRY2  170, UnlockFile, 5 
SYSSTUBS_ENTRY3  170, UnlockFile, 5 
SYSSTUBS_ENTRY4  170, UnlockFile, 5 
SYSSTUBS_ENTRY5  170, UnlockFile, 5 
SYSSTUBS_ENTRY6  170, UnlockFile, 5 
SYSSTUBS_ENTRY7  170, UnlockFile, 5 
SYSSTUBS_ENTRY8  170, UnlockFile, 5 
SYSSTUBS_ENTRY1  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY2  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY3  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY4  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY5  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY6  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY7  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY8  171, UnlockVirtualMemory, 4 
SYSSTUBS_ENTRY1  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY2  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY3  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY4  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY5  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY6  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY7  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY8  172, UnmapViewOfSection, 2 
SYSSTUBS_ENTRY1  173, VdmControl, 2 
SYSSTUBS_ENTRY2  173, VdmControl, 2 
SYSSTUBS_ENTRY3  173, VdmControl, 2 
SYSSTUBS_ENTRY4  173, VdmControl, 2 
SYSSTUBS_ENTRY5  173, VdmControl, 2 
SYSSTUBS_ENTRY6  173, VdmControl, 2 
SYSSTUBS_ENTRY7  173, VdmControl, 2 
SYSSTUBS_ENTRY8  173, VdmControl, 2 
SYSSTUBS_ENTRY1  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY2  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY3  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY4  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY5  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY6  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY7  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY8  174, WaitForMultipleObjects, 5 
SYSSTUBS_ENTRY1  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY2  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY3  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY4  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY5  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY6  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY7  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY8  175, WaitForSingleObject, 3 
SYSSTUBS_ENTRY1  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY2  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY3  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY4  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY5  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY6  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY7  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY8  176, WaitForProcessMutant, 0 
SYSSTUBS_ENTRY1  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY2  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY3  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY4  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY5  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY6  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY7  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY8  177, WaitHighEventPair, 1 
SYSSTUBS_ENTRY1  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY2  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY3  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY4  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY5  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY6  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY7  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY8  178, WaitLowEventPair, 1 
SYSSTUBS_ENTRY1  179, WriteFile, 9 
SYSSTUBS_ENTRY2  179, WriteFile, 9 
SYSSTUBS_ENTRY3  179, WriteFile, 9 
SYSSTUBS_ENTRY4  179, WriteFile, 9 
SYSSTUBS_ENTRY5  179, WriteFile, 9 
SYSSTUBS_ENTRY6  179, WriteFile, 9 
SYSSTUBS_ENTRY7  179, WriteFile, 9 
SYSSTUBS_ENTRY8  179, WriteFile, 9 
SYSSTUBS_ENTRY1  180, WriteRequestData, 6 
SYSSTUBS_ENTRY2  180, WriteRequestData, 6 
SYSSTUBS_ENTRY3  180, WriteRequestData, 6 
SYSSTUBS_ENTRY4  180, WriteRequestData, 6 
SYSSTUBS_ENTRY5  180, WriteRequestData, 6 
SYSSTUBS_ENTRY6  180, WriteRequestData, 6 
SYSSTUBS_ENTRY7  180, WriteRequestData, 6 
SYSSTUBS_ENTRY8  180, WriteRequestData, 6 
SYSSTUBS_ENTRY1  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY2  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY3  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY4  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY5  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY6  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY7  181, WriteVirtualMemory, 5 
SYSSTUBS_ENTRY8  181, WriteVirtualMemory, 5 

STUBS_END
