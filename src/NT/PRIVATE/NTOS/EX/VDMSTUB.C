/*++

Module Name:

    vdmstub.c

Abstract:

    NtVdmControl stub.  The NTVDM subsystem and the VDM kernel surface
    have been removed from this build; the syscall slot is preserved so
    that the syscall numbering downstream of it does not shift, but the
    service simply returns STATUS_NOT_IMPLEMENTED.

Environment:

    Kernel mode only.

--*/

#include "exp.h"

NTSTATUS
NtVdmControl (
    IN ULONG Service,
    IN OUT PVOID ServiceData
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtVdmControl)
#endif


NTSTATUS
NtVdmControl (
    IN ULONG Service,
    IN OUT PVOID ServiceData
    )
{
    UNREFERENCED_PARAMETER(Service);
    UNREFERENCED_PARAMETER(ServiceData);

    PAGED_CODE();

    return STATUS_NOT_IMPLEMENTED;
}
