/*++

Copyright (c) 2026 MicroNT

Module Name:

    csrstub.c

Abstract:

    csrss-free Csr* client-primitive stubs.

    Stock NT 3.5 ships these inside ntdll.dll (NTOS/DLL/CSRINIT.C,
    CSRTASK.C, CSRUTIL.C, CSRQUICK.C — all dropped from MicroNT's
    ntdll lift since csrss is gone).  kernel32's own .obj files
    (process.c, thread.c, dllatom.c, debug.c, vdm.c, support.c) and
    nlslib's section.c / tables.c reference Csr* via NTSYSAPI =
    __declspec(dllimport), so the linker hunts for `__imp__Csr*@N`
    thunks at link time.

    Rather than extend ntdll, we re-export Csr* FROM kernel32 itself
    (KERNEL32.SRC) with the fail-stub bodies below.  The lib step
    generates kernel32.exp with the right `__imp_` thunks; the link
    step satisfies every consumer .obj from kernel32.exp.  Net effect:
    Csr* are now kernel32 exports instead of ntdll exports.  Harmless
    for our toolchain — CL/LINK don't import Csr* from anywhere.  An
    out-of-tree binary that did `GetProcAddress(ntdll, "CsrFoo")` would
    get NULL, but `GetProcAddress(kernel32, "CsrFoo")` returns our
    stub.  Acceptable until the toolchain runs and we revisit.

    All bodies fail with STATUS_PORT_DISCONNECTED — semantically
    accurate ("the LPC port to csrss is not available") and the
    standard NT error every Csr* caller already knows how to handle.

Author:

    MicroNT (csrss-free port from NT 3.5 ntdll Csr* primitives)

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <ntcsrdll.h>
#include <ntcsrmsg.h>

NTSTATUS
NTAPI
CsrClientCallServer(
    IN OUT PCSR_API_MSG       m,
    IN OUT PCSR_CAPTURE_HEADER CaptureBuffer OPTIONAL,
    IN     CSR_API_NUMBER     ApiNumber,
    IN     ULONG              ArgLength
    )
{
    UNREFERENCED_PARAMETER(CaptureBuffer);
    UNREFERENCED_PARAMETER(ApiNumber);
    UNREFERENCED_PARAMETER(ArgLength);
    if (m) {
        m->ReturnValue = STATUS_PORT_DISCONNECTED;
    }
    return STATUS_PORT_DISCONNECTED;
}

PCSR_CAPTURE_HEADER
NTAPI
CsrAllocateCaptureBuffer(
    IN ULONG CountMessagePointers,
    IN ULONG CountCapturePointers,
    IN ULONG Size
    )
{
    UNREFERENCED_PARAMETER(CountMessagePointers);
    UNREFERENCED_PARAMETER(CountCapturePointers);
    UNREFERENCED_PARAMETER(Size);
    return NULL;
}

VOID
NTAPI
CsrFreeCaptureBuffer(
    IN PCSR_CAPTURE_HEADER CaptureBuffer
    )
{
    UNREFERENCED_PARAMETER(CaptureBuffer);
}

ULONG
NTAPI
CsrAllocateMessagePointer(
    IN OUT PCSR_CAPTURE_HEADER CaptureBuffer,
    IN ULONG  Length,
    OUT PVOID *Pointer
    )
{
    UNREFERENCED_PARAMETER(CaptureBuffer);
    UNREFERENCED_PARAMETER(Length);
    if (Pointer) *Pointer = NULL;
    return 0;
}

VOID
NTAPI
CsrCaptureMessageString(
    IN OUT PCSR_CAPTURE_HEADER CaptureBuffer,
    IN PCSTR  String,
    IN ULONG  Length,
    IN ULONG  MaximumLength,
    OUT PSTRING CapturedString
    )
{
    UNREFERENCED_PARAMETER(CaptureBuffer);
    UNREFERENCED_PARAMETER(String);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(MaximumLength);
    if (CapturedString) {
        CapturedString->Length        = 0;
        CapturedString->MaximumLength = 0;
        CapturedString->Buffer        = NULL;
    }
}

NTSTATUS NTAPI CsrNewThread(VOID)              { return STATUS_PORT_DISCONNECTED; }
NTSTATUS NTAPI CsrIdentifyAlertableThread(VOID){ return STATUS_PORT_DISCONNECTED; }

NTSTATUS
NTAPI
CsrSetPriorityClass(
    IN     HANDLE  ProcessHandle,
    IN OUT PULONG  PriorityClass
    )
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(PriorityClass);
    return STATUS_PORT_DISCONNECTED;
}
