/*++

    hello.c — Minimal kernel driver for MicroNT boot-time visibility.

    DriverEntry runs once during Phase 1 I/O init, immediately after being
    loaded from LoaderBlock.BootDriverListHead. It prints a message via
    HalDisplayString (→ our HAL COM2 serial) and returns success.

    Serves as the "hello, world" baseline for our driver pipeline: if this
    message appears, the kernel successfully loaded, relocated, imported,
    and invoked DriverEntry on a boot driver we supplied ourselves.

--*/

#include <ntddk.h>

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    HalDisplayString("hello.sys: DriverEntry reached — MicroNT is live\n");

    return STATUS_SUCCESS;
}
