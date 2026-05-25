/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    kdtrap.c

Abstract:

    This module contains code to implement the target side of the portable
    kernel debugger.

Author:

    Bryan M. Willman (bryanwi) 25-Sep-90

Revision History:

--*/

#include "kdp.h"

extern PUCHAR KdpCopyDataToStack(PUCHAR, ULONG);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGEKD, KdpTrap)
#pragma alloc_text(PAGEKD, KdIsThisAKdTrap)
#endif


BOOLEAN
KdpTrap (
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT ContextRecord,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChance
    )

/*++

Routine Description:

    This routine is called whenever a exception is dispatched and the kernel
    debugger is active.

Arguments:

    TrapFrame - Supplies a pointer to a trap frame that describes the
        trap.

    ExceptionFrame - Supplies a pointer to a exception frame that describes
        the trap.

    ExceptionRecord - Supplies a pointer to an exception record that
        describes the exception.

    ContextRecord - Supplies the context at the time of the exception.

    PreviousMode - Supplies the previous processor mode.

    SecondChance - Supplies a boolean value that determines whether this is
        the second chance (TRUE) that the exception has been raised.

Return Value:

    A value of TRUE is returned if the exception is handled. Otherwise a
    value of FALSE is returned.

--*/

{

    BOOLEAN Completion = FALSE;
    BOOLEAN Enable;
    BOOLEAN UnloadSymbols = FALSE;
    ULONG   RetValue;
    STRING  String, ReplyString;
    PUCHAR  Buffer;
    PKD_SYMBOLS_INFO SymbolInfo;
    PVOID   SavedEsp;
    PKPRCB  Prcb;

    _asm {
        //
        // Save esp on ebp frame so c-runtime registers are restored correctly
        //

        mov     SavedEsp, esp
    }

    //
    // Print, Prompt, Load symbols, Unload symbols, are all special
    // cases of STATUS_BREAKPOINT
    //

    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
        (ExceptionRecord->ExceptionInformation[0] != BREAKPOINT_BREAK)) {

        //
        // We have one of the support functions.
        //

        if (KdDebuggerNotPresent  &&
            ExceptionRecord->ExceptionInformation[0] != BREAKPOINT_PROMPT) {
            /*
             * MicroNT: no KD debugger attached, but we still want to see
             * DbgPrint/KdPrint from user-mode (smss, ntdll). Tee
             * BREAKPOINT_PRINT strings to HalDisplayString so kernel-mode
             * serial (COM2) carries them too.
             */
            if (ExceptionRecord->ExceptionInformation[0] == BREAKPOINT_PRINT) {
                try {
                    STRING LocalString;
                    UCHAR LocalBuf[513];
                    LocalString = *((PSTRING)ExceptionRecord->ExceptionInformation[1]);
                    if (LocalString.Length > 512) LocalString.Length = 512;
                    if (PreviousMode == UserMode) {
                        ProbeForRead(LocalString.Buffer, LocalString.Length,
                                     sizeof(UCHAR));
                    }
                    {
                        USHORT i;
                        for (i = 0; i < LocalString.Length; i++) {
                            LocalBuf[i] = LocalString.Buffer[i];
                        }
                        LocalBuf[LocalString.Length] = 0;
                    }
                    HalDisplayString(LocalBuf);
                } except (EXCEPTION_EXECUTE_HANDLER) {
                    ;
                }
            }
            ContextRecord->Eip++;
            return(TRUE);
        }


        //
        // Since some of these functions can be entered from user mode,
        // we hold off entering the debugger until the user mode buffers
        // are copied.  (Because they may not be present in memory, and
        // they must be paged in before we raise Irql to the
        // Highest level.)
        //
        //

        switch (ExceptionRecord->ExceptionInformation[0]) {

            //
            //  ExceptionInformation[1] is PSTRING to print
            //

            case BREAKPOINT_PRINT:

                if (PreviousMode == UserMode) {

                    //
                    // Move user mode parameters to kernel stack
                    //

                    try {
                        String = *((PSTRING)ExceptionRecord->ExceptionInformation[1]);
                        if (String.Length > 512) {
                            break;
                        }
                        ProbeForRead(String.Buffer, String.Length, sizeof(UCHAR));
                        String.Buffer =
                            KdpCopyDataToStack(String.Buffer, String.Length);

                    } except (EXCEPTION_EXECUTE_HANDLER) {

                        //
                        // If an exception occurs then don't handle
                        // this DebugService request.
                        //

                        break;
                    }

                } else {
                    String = *((PSTRING)ExceptionRecord->ExceptionInformation[1]);
                }
                /*
                 * MicroNT: always tee the print to HalDisplayString so we
                 * see user-mode DbgPrint / kernel DbgPrint output on COM2
                 * even when no KD debugger is attached.
                 */
                {
                    UCHAR _lbuf[513];
                    USHORT _li, _ll = String.Length;
                    if (_ll > 512) _ll = 512;
                    for (_li = 0; _li < _ll; _li++) _lbuf[_li] = String.Buffer[_li];
                    _lbuf[_ll] = 0;
                    HalDisplayString(_lbuf);
                }
                Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);
                if (KdpPrintString(&String)) {
                    ContextRecord->Eax = STATUS_BREAKPOINT;
                } else {
                    ContextRecord->Eax = STATUS_SUCCESS;
                }
                ContextRecord->Eip++;
                KdExitDebugger(Enable);
                Completion = TRUE;
                break;

            //
            //  ExceptionInformation[1] is prompt string,
            //  ExceptionInformation[2] is return string
            //

            case BREAKPOINT_PROMPT:
                if (PreviousMode == UserMode) {

                    //
                    // Move user mode parameters to kernel stack
                    //


                    try {
                        String = *((PSTRING)ExceptionRecord->ExceptionInformation[1]);
                        if (String.Length > 512) {
                            break;
                        }
                        ProbeForRead(String.Buffer, String.Length, sizeof(CHAR));
                        String.Buffer =
                            KdpCopyDataToStack(String.Buffer, String.Length);

                        ReplyString = *((PSTRING)ExceptionRecord->ExceptionInformation[2]);
                        if (ReplyString.MaximumLength > 512) {
                            break;
                        }
                        ProbeForWrite(ReplyString.Buffer,
                                      ReplyString.MaximumLength,
                                      sizeof(CHAR));
                        Buffer = ReplyString.Buffer;
                        ReplyString.Buffer =
                            KdpCopyDataToStack(
                                ReplyString.Buffer,
                                ReplyString.MaximumLength
                            );

                    } except (EXCEPTION_EXECUTE_HANDLER) {

                        //
                        // If an exception occurs then don't handle
                        // this DebugService request.
                        //

                        break;
                    }
                } else {
                    String = *((PSTRING)ExceptionRecord->ExceptionInformation[1]);
                    ReplyString = *((PSTRING)ExceptionRecord->ExceptionInformation[2]);
                }

                //
                // Prompt, keep prompting until no breakin seen.
                //

                Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);
                do {
                    RetValue = KdpPromptString(&String, &ReplyString);
                } while (RetValue == TRUE);

                ContextRecord->Eax =
                    ((PSTRING)ExceptionRecord->ExceptionInformation[2])->Length;
                ContextRecord->Eip++;
                KdExitDebugger(Enable);

                if (PreviousMode == UserMode) {

                    //
                    // Restore user mode return parameters
                    //

                    try {
                        KdpQuickMoveMemory(
                            Buffer,
                            ReplyString.Buffer,
                            ReplyString.Length
                        );
                    } except (EXCEPTION_EXECUTE_HANDLER) {

                        //
                        // If an exception occurs then don't handle
                        // this DebugService request.
                        //

                        break;
                    }
                }

                Completion = TRUE;
                break;

            //
            //  ExceptionInformation[1] is file name of new module
            //  ExceptionInformaiton[2] is the base of the dll
            //

            case BREAKPOINT_UNLOAD_SYMBOLS:
                UnloadSymbols = TRUE;

                //
                // Fall through
                //

            case BREAKPOINT_LOAD_SYMBOLS:

                if (PreviousMode != KernelMode) {
                    break;
                }

                Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);
                SymbolInfo = (PKD_SYMBOLS_INFO)ExceptionRecord->ExceptionInformation[2];

                Completion = KdpReportLoadSymbolsStateChange(
                    (PSTRING)ExceptionRecord->ExceptionInformation[1],
                    SymbolInfo,
                    UnloadSymbols,
                    ContextRecord
                    );
                ContextRecord->Eip++;
                KdExitDebugger(Enable);
                break;

            //
            //  Unknown command
            //

            default:
                // return FALSE
                break;
        }

    } else {

        if  ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) ||
             (ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP)  ||
             (NtGlobalFlag & FLG_STOP_ON_EXCEPTION) ||
             SecondChance) {

            if (!SecondChance &&
                (ExceptionRecord->ExceptionCode == STATUS_PORT_DISCONNECTED ||
                 NT_SUCCESS( ExceptionRecord->ExceptionCode )
                )
               ) {
                //
                // User does not really want to see these either.
                // so do NOT report it to debugger.
                //

                return FALSE;
                }

            //
            // Report state change to kernel debugger on host
            //


            Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);
            Prcb = KeGetCurrentPrcb();

            KiSaveProcessorControlState(&Prcb->ProcessorState);
            RtlCopyMemory(&Prcb->ProcessorState.ContextFrame,
                            ContextRecord,
                            sizeof (CONTEXT));

            Completion = KdpReportExceptionStateChange(
                            ExceptionRecord,
                            &Prcb->ProcessorState.ContextFrame,
                            SecondChance
                            );

            RtlCopyMemory(ContextRecord,
                            &Prcb->ProcessorState.ContextFrame,
                            sizeof (CONTEXT) );

            KiRestoreProcessorControlState(&KeGetCurrentPrcb()->ProcessorState);

            KdExitDebugger(Enable);

        } else {

            //
            // This is real exception that user doesn't want to see,
            // so do NOT report it to debugger.
            //

            // return FALSE;
        }
    }

    _asm {
        mov     esp, SavedEsp
    }
    return Completion;

    UNREFERENCED_PARAMETER(PreviousMode);
}


BOOLEAN
KdIsThisAKdTrap (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT ContextRecord,
    IN KPROCESSOR_MODE PreviousMode
    )

/*++

Routine Description:

    This routine is called whenever a user-mode exception occurs and
    it might be a kernel debugger exception (Like DbgPrint/DbgPrompt ).

Arguments:

    ExceptionRecord - Supplies a pointer to an exception record that
        describes the exception.

    ContextRecord - Supplies the context at the time of the exception.

    PreviousMode - Supplies the previous processor mode.

Return Value:

    A value of TRUE is returned if this is for the kernel debugger.
    Otherwise, a value of FALSE is returned.

--*/

{
    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
        (ExceptionRecord->NumberParameters > 0) &&
        (ExceptionRecord->ExceptionInformation[0] != BREAKPOINT_BREAK)) {

        return TRUE;
    } else {
        return FALSE;
    }
    UNREFERENCED_PARAMETER(ContextRecord);
}

BOOLEAN
KdpCheckTracePoint(
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN OUT PCONTEXT ContextRecord
    );

VOID
SaveSymLoad(
    IN PSTRING PathName,
    IN PVOID BaseOfDll,
    IN LONG ProcessId,
    IN BOOLEAN UnloadSymbols
    );

BOOLEAN
KdpStub (
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT ContextRecord,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChance
    )

/*++

Routine Description:

    This routine provides a kernel debugger stub routine to catch debug
    prints in a checked system when the kernel debugger is not active.

Arguments:

    TrapFrame - Supplies a pointer to a trap frame that describes the
        trap.

    ExceptionFrame - Supplies a pointer to a exception frame that describes
        the trap.

    ExceptionRecord - Supplies a pointer to an exception record that
        describes the exception.

    ContextRecord - Supplies the context at the time of the exception.

    PreviousMode - Supplies the previous processor mode.

    SecondChance - Supplies a boolean value that determines whether this is
        the second chance (TRUE) that the exception has been raised.

Return Value:

    A value of TRUE is returned if the exception is handled. Otherwise a
    value of FALSE is returned.

--*/

{
    PULONG  SymbolArgs;
    //
    // If the breakpoint is a debug print, then return TRUE. Otherwise,
    // return FALSE.
    //

    /*
     * MicroNT: surface user-mode unhandled exceptions (before the process
     * dies silently with an opaque exit status). Second chance only, since
     * first chance gives the user's own handler a shot.
     */
    {
        static ULONG _prev_code = 0, _prev_eip = 0;
        if (PreviousMode == UserMode &&
            ExceptionRecord->ExceptionCode != STATUS_BREAKPOINT &&
            ExceptionRecord->ExceptionCode != DBG_PRINTEXCEPTION_C &&
            ((ULONG)ExceptionRecord->ExceptionCode != _prev_code ||
             ContextRecord->Eip != _prev_eip))
        {
            static const CHAR _hx[] = "0123456789abcdef";
            CHAR tmp[160];
            ULONG values[5];
            const CHAR *labels[5];
            ULONG p;
            int li, j, k;
            _prev_code = (ULONG)ExceptionRecord->ExceptionCode;
            _prev_eip = ContextRecord->Eip;
            labels[0] = SecondChance ? "UMODE EXC(2nd): code=" : "UMODE EXC(1st): code=";
            labels[1] = " addr=";
            labels[2] = " p0=";
            labels[3] = " p1=";
            labels[4] = " eip=";
            values[0] = (ULONG)ExceptionRecord->ExceptionCode;
            values[1] = (ULONG)ExceptionRecord->ExceptionAddress;
            values[2] = ExceptionRecord->NumberParameters > 0
                    ? ExceptionRecord->ExceptionInformation[0] : 0;
            values[3] = ExceptionRecord->NumberParameters > 1
                    ? ExceptionRecord->ExceptionInformation[1] : 0;
            values[4] = ContextRecord->Eip;
            k = 0;
            for (li = 0; li < 5; li++) {
                for (p = 0; labels[li][p]; p++) tmp[k++] = labels[li][p];
                for (j = 28; j >= 0; j -= 4) tmp[k++] = _hx[(values[li] >> j) & 0xF];
            }
            tmp[k++] = '\n';
            tmp[k] = 0;
            HalDisplayString(tmp);

            /*
             * MicroNT: for an access violation, also dump the top of the
             * faulting user stack.  The fault EIP alone (e.g. a shared
             * wcscpy / RtlCopy) does not reveal who called it -- the return
             * address on the stack does, and its module range (python25 ~
             * 0x1e0xxxxx, msvcr71 ~ 0x7c34xxxx, kernel32 ~ 0x606xxxxx,
             * ntdll ~ 0x6010xxxx) identifies the caller.  Read-only and
             * probe-guarded so a bad Esp can't fault the kernel.
             */
            if (PreviousMode == UserMode &&
                ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION) {
                try {
                    PULONG sp = (PULONG)ContextRecord->Esp;
                    ULONG  sw[8];
                    int    n;
                    ProbeForRead(sp, sizeof(sw), sizeof(ULONG));
                    for (n = 0; n < 8; n++) {
                        sw[n] = sp[n];
                    }
                    k = 0;
                    for (p = 0; "UMODE STK:"[p]; p++) {
                        tmp[k++] = "UMODE STK:"[p];
                    }
                    for (n = 0; n < 8; n++) {
                        tmp[k++] = ' ';
                        for (j = 28; j >= 0; j -= 4) {
                            tmp[k++] = _hx[(sw[n] >> j) & 0xF];
                        }
                    }
                    tmp[k++] = '\n';
                    tmp[k] = 0;
                    HalDisplayString(tmp);
                } except (EXCEPTION_EXECUTE_HANDLER) {
                    ;
                }
            }
        }
    }

    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
        (ExceptionRecord->NumberParameters > 0) &&
        ((ExceptionRecord->ExceptionInformation[0] == BREAKPOINT_LOAD_SYMBOLS)||
         (ExceptionRecord->ExceptionInformation[0] == BREAKPOINT_UNLOAD_SYMBOLS)||
         (ExceptionRecord->ExceptionInformation[0] == BREAKPOINT_PRINT))) {

        /*
         * MicroNT: tee DbgPrint/KdPrint text to HalDisplayString so we see
         * kernel + user-mode debug output on COM2 even when no KD debugger
         * is attached. Boot options without DEBUG land us in KdpStub.
         */
        if (ExceptionRecord->ExceptionInformation[0] == BREAKPOINT_PRINT) {
            try {
                STRING LocalString;
                UCHAR  LocalBuf[513];
                USHORT i, ll;
                LocalString = *((PSTRING)ExceptionRecord->ExceptionInformation[1]);
                ll = LocalString.Length;
                if (ll > 512) ll = 512;
                if (PreviousMode == UserMode) {
                    ProbeForRead(LocalString.Buffer, ll, sizeof(UCHAR));
                }
                for (i = 0; i < ll; i++) LocalBuf[i] = LocalString.Buffer[i];
                LocalBuf[ll] = 0;
                HalDisplayString(LocalBuf);
            } except (EXCEPTION_EXECUTE_HANDLER) {
                ;
            }
        }
        ContextRecord->Eip++;
        return(TRUE);
    } else if (KdPitchDebugger == TRUE) {
        return(FALSE);
    } else {
        return(KdpCheckTracePoint(ExceptionRecord,ContextRecord));
    }
}
