/*++

Module Name:

    bcryptp.c

Abstract:

    bcryptprimitives.dll -- the modern CSPRNG provider DLL. Exports the two
    "fill this buffer with random bytes" primitives that ship in this DLL on
    real Windows: ProcessPrng (the documented name modern Rust/Go/CRT import)
    and SystemPrng (its older internal sibling). Both are identical forwarders
    to the kernel CSPRNG via the NtGenerateSecureRandom syscall.

    (SystemFunction036 / RtlGenRandom is NOT here -- that ABI contract lives in
    advapi32; see WINDOWS/BASE/ADVAPI/crypt.c.)

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>

//
// Kernel CSPRNG syscall. Exported by ntdll, no PUBLIC header declares it.
//
extern NTSTATUS NTAPI NtGenerateSecureRandom(PVOID Buffer, ULONG Length);

BOOL
BcryptpDllInit(
    IN PVOID DllHandle,
    IN ULONG Reason,
    IN PVOID Context OPTIONAL
    )
{
    UNREFERENCED_PARAMETER( DllHandle );
    UNREFERENCED_PARAMETER( Reason );
    UNREFERENCED_PARAMETER( Context );
    return TRUE;
}

//
// ProcessPrng(PBYTE pbData, SIZE_T cbData). SIZE_T isn't in the NT 3.5 SDK and
// is 32-bit on i386 anyway, so we type the length as ULONG -- ABI-identical.
//
BOOL
APIENTRY
ProcessPrng(
    IN OUT PUCHAR pbData,
    IN     ULONG  cbData
    )
{
    return NT_SUCCESS( NtGenerateSecureRandom( pbData, cbData ) );
}

BOOL
APIENTRY
SystemPrng(
    IN OUT PUCHAR pbRandomData,
    IN     ULONG  cbRandomData
    )
{
    return NT_SUCCESS( NtGenerateSecureRandom( pbRandomData, cbRandomData ) );
}
