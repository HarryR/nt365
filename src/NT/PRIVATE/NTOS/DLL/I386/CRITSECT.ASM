        title   "Critical Section Support"
;++
;
;  Copyright (c) 1991  Microsoft Corporation
;
;  Module Name:
;
;     critsect.asm
;
;  Abstract:
;
;     This module implements functions to support user mode critical sections.
;
;  Author:
;
;     Bryan M. Willman (bryanwi) 2-Oct-91
;
;  Environment:
;
;     Any mode.
;
;  Revision History:
;
;
;
;   WARNING!!!!!!!!!! This code is duplicated in
;   windows\base\client\i386\critsect.asm
;
;   Some day we should put it in a .inc file that both include.
;
;--

.386p
        .xlist
include ks386.inc
include callconv.inc                    ; calling convention macros
        .list

_DATA   SEGMENT DWORD PUBLIC 'DATA'
    public _LdrpLockPrefixTable
_LdrpLockPrefixTable    label dword
        dd offset FLAT:Lock1
        dd offset FLAT:Lock2
        dd offset FLAT:Lock3
        dd offset FLAT:Lock4
        dd offset FLAT:Lock5
        dd 0
_DATA   ENDS

_TEXT   SEGMENT PARA PUBLIC 'CODE'
        ASSUME  DS:FLAT, ES:FLAT, SS:NOTHING, FS:NOTHING, GS:NOTHING

        EXTRNP  _RtlpWaitForCriticalSection,1
        EXTRNP  _RtlpUnWaitCriticalSection,1
if DEVL
        EXTRNP  _RtlpNotOwnerCriticalSection,1
        EXTRNP  _DbgBreakPoint,0
endif

CriticalSection equ     [esp + 4]

        page , 132
        subttl  "RtlEnterCriticalSection"

;++
;
; NTSTATUS
; RtlEnterCriticalSection(
;    IN PRTL_CRITICAL_SECTION CriticalSection
;    )
;
; Routine Description:
;
;    This function enters a critical section.
;
; Arguments:
;
;    CriticalSection - supplies a pointer to a critical section.
;
; Return Value:
;
;   STATUS_SUCCESS or raises an exception if an error occured.
;
;--

        align   16
cPublicProc _RtlEnterCriticalSection,1
cPublicFpo 1,0

        mov     ecx,fs:PcTeb                ; (ecx) == NtCurrentTeb()
        mov     edx,CriticalSection         ; interlocked inc of
        mov     eax,TbClientId+4[ecx]       ; (eax) == NtCurrentTeb()->ClientId.UniqueThread

if DBG
        cmp     dword ptr TbSpare1[ecx],0
        jz      @f
        stdCall _DbgBreakPoint
@@:
endif ; DBG
Lock1:
   lock inc     dword ptr CsLockCount[edx]  ; ... CriticalSection->LockCount
        jnz     @F

setowner:
        mov     CsOwningThread[edx],eax
        mov     dword ptr CsRecursionCount[edx],1

if DBG
        inc     dword ptr TbCountOfOwnedCriticalSections[ecx]
        push    edi
        mov     edi,CsDebugInfo[edx]
        inc     dword ptr CsEntryCount[edi]
        pop     edi
endif ; DBG

        xor     eax,eax
        stdRET  _RtlEnterCriticalSection

        align   16
@@:
        cmp     CsOwningThread[edx],eax
        jne     @F
        inc     dword ptr CsRecursionCount[edx]
if DEVL
        mov     eax,CsDebugInfo[edx]
        inc     dword ptr CsEntryCount[eax]
endif
        xor     eax,eax
        stdRET  _RtlEnterCriticalSection

@@:
        stdCall _RtlpWaitForCriticalSection, <edx>
        mov     ecx,fs:PcTeb                ; (ecx) == NtCurrentTeb()
        mov     eax,TbClientId+4[ecx]       ; (eax) == NtCurrentTeb()->ClientId.UniqueThread
        mov     edx,CriticalSection
        jmp     setowner

stdENDP _RtlEnterCriticalSection

        page , 132
        subttl  "RtlLeaveCriticalSection"
;++
;
; NTSTATUS
; RtlLeaveCriticalSection(
;    IN PRTL_CRITICAL_SECTION CriticalSection
;    )
;
; Routine Description:
;
;    This function leaves a critical section.
;
; Arguments:
;
;    CriticalSection - supplies a pointer to a critical section.
;
; Return Value:
;
;   STATUS_SUCCESS or raises an exception if an error occured.
;
;--

        align   16
cPublicProc _RtlLeaveCriticalSection,1
cPublicFpo 1,0

        mov     edx,CriticalSection
if DEVL
        mov     ecx,fs:PcTeb                ; (ecx) == NtCurrentTeb()
        mov     eax,TbClientId+4[ecx]       ; (eax) == NtCurrentTeb()->ClientId.UniqueThread
        cmp     eax,CsOwningThread[edx]
        je      @F
        stdCall _RtlpNotOwnerCriticalSection, <edx>
        mov     eax,STATUS_INVALID_OWNER
        stdRET  _RtlLeaveCriticalSection
@@:
endif
        xor     eax,eax                     ; Assume STATUS_SUCCESS
        dec     dword ptr CsRecursionCount[edx]
        jnz     leave_recurs                ; skip if only leaving recursion

        mov     CsOwningThread[edx],eax     ; clear owning thread id

if DBG
        mov     ecx,CsDebugInfo[edx]
        mov     CsDepth[ecx],eax            ; clear
        mov     ecx,fs:PcTeb                ; (ecx) == NtCurrentTeb()
        dec     dword ptr TbCountOfOwnedCriticalSections[ecx]
endif ; DBG

Lock2:
   lock dec     dword ptr CsLockCount[edx]  ; interlocked dec of
                                            ; ... CriticalSection->LockCount
        jge     @F
        stdRET  _RtlLeaveCriticalSection

@@:
        stdCall _RtlpUnWaitCriticalSection, <edx>
        xor     eax,eax                     ; return STATUS_SUCCESS
        stdRET  _RtlLeaveCriticalSection

        align   16
leave_recurs:
Lock3:
   lock dec     dword ptr CsLockCount[edx]  ; interlocked dec of
                                            ; ... CriticalSection->LockCount
        stdRET  _RtlLeaveCriticalSection

_RtlLeaveCriticalSection    endp

        page , 132
        subttl  "RtlTryEnterCriticalSection"
;++
;
; BOOLEAN
; RtlTryEnterCriticalSection(
;    IN PRTL_CRITICAL_SECTION CriticalSection
;    )
;
; Routine Description:
;
;    This function attempts to enter a critical section without blocking.  If
;    the section is free it is acquired; if it is already owned by the calling
;    thread the recursion count is bumped; otherwise the function returns
;    immediately without waiting.
;
; Arguments:
;
;    CriticalSection - supplies a pointer to a critical section.
;
; Return Value:
;
;    TRUE if the critical section was entered (or recursively re-entered);
;    FALSE if it is currently owned by another thread.
;
;--

        align   16
cPublicProc _RtlTryEnterCriticalSection,1
cPublicFpo 1,0

        mov     eax,CriticalSection         ; (eax) -> critical section
        lea     ecx,CsLockCount[eax]        ; (ecx) -> CriticalSection->LockCount
        xor     edx,edx                     ; (edx) == 0  : exchange value (acquired; LockCount 0)
        mov     eax,-1                       ; (eax) == -1 : comperand  (LockCount when free)

;
; ml 6.11 predates the 486 cmpxchg mnemonic, so emit the opcode bytes -- the same
; sequence proven by kernel32's InterlockedCompareExchange:
;   F0          lock prefix (NOP'd on UP via _LdrpLockPrefixTable; Lock4 labels it)
;   0F B1 /r    cmpxchg r/m32, r32 ; ModRM 11h = mod 00 ([ecx]) / reg edx / r/m ecx
;               -> lock cmpxchg dword ptr [ecx], edx
;
Lock4:
        db      0F0h
        db      00Fh, 0B1h, 011h
        jnz     tryen10                     ; not free (eax == current LockCount)

;
; The critical section was free and is now owned by this thread.
;
        mov     edx,CriticalSection         ; (edx) -> critical section
        mov     ecx,fs:PcTeb                ; (ecx) == NtCurrentTeb()
        mov     eax,TbClientId+4[ecx]       ; (eax) == NtCurrentTeb()->ClientId.UniqueThread
        mov     CsOwningThread[edx],eax
        mov     dword ptr CsRecursionCount[edx],1

if DBG
        inc     dword ptr TbCountOfOwnedCriticalSections[ecx]
        mov     ecx,CsDebugInfo[edx]
        inc     dword ptr CsEntryCount[ecx]
endif ; DBG

        mov     eax,1                        ; return TRUE
        stdRET  _RtlTryEnterCriticalSection

;
; The critical section is held.  Succeed only on a recursive re-entry by the
; owning thread, mirroring RtlEnterCriticalSection's owned-by-self path.
;
        align   16
tryen10:
        mov     edx,CriticalSection         ; (edx) -> critical section
        mov     ecx,fs:PcTeb                ; (ecx) == NtCurrentTeb()
        mov     eax,TbClientId+4[ecx]       ; (eax) == this thread's UniqueThread
        cmp     CsOwningThread[edx],eax
        jne     tryen20                     ; owned by another thread -> fail

Lock5:
   lock inc     dword ptr CsLockCount[edx]  ; interlocked inc of LockCount
        inc     dword ptr CsRecursionCount[edx]
if DEVL
        mov     eax,CsDebugInfo[edx]
        inc     dword ptr CsEntryCount[eax]
endif
        mov     eax,1                        ; return TRUE
        stdRET  _RtlTryEnterCriticalSection

tryen20:
        xor     eax,eax                     ; return FALSE
        stdRET  _RtlTryEnterCriticalSection

stdENDP _RtlTryEnterCriticalSection

_TEXT   ends
        end
