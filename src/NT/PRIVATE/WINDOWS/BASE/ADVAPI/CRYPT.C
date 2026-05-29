/*++

Module Name:

    crypt.c

Abstract:

    The randomness path of the legacy CryptoAPI, MicroNT-owned. Just enough
    for callers that do CryptAcquireContext -> CryptGenRandom -> Release for
    entropy (Python 2.5 os.urandom, OpenSSL rand_win.c, ...). There is no real
    CSP -- no key containers, no hashing, no signing -- only the RNG, which
    forwards to the kernel CSPRNG via the NtGenerateSecureRandom syscall.

    SystemFunction036 (the documented RtlGenRandom) also lives here: advapi32
    is its real ABI home in NT.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>

//
// Kernel CSPRNG syscall. Exported by ntdll but not declared in any PUBLIC
// header, so spell out the prototype here.
//
extern NTSTATUS NTAPI NtGenerateSecureRandom(PVOID Buffer, ULONG Length);

//
// wincrypt.h isn't in the NT 3.5 SDK. HCRYPTPROV is a pointer-sized opaque
// handle -- 32-bit ULONG on i386. We hand out a fixed non-null cookie: there's
// no provider state to track, only the randomness path.
//
typedef ULONG HCRYPTPROV;

#define MICRONT_CRYPT_COOKIE  ((HCRYPTPROV)0x4D526E47)   /* 'MRnG' */

BOOL
APIENTRY
CryptAcquireContextW(
    OUT HCRYPTPROV *phProv,
    IN  LPCWSTR     szContainer,
    IN  LPCWSTR     szProvider,
    IN  DWORD       dwProvType,
    IN  DWORD       dwFlags
    )
{
    UNREFERENCED_PARAMETER( szContainer );
    UNREFERENCED_PARAMETER( szProvider );
    UNREFERENCED_PARAMETER( dwProvType );
    UNREFERENCED_PARAMETER( dwFlags );

    if ( phProv == NULL ) {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    *phProv = MICRONT_CRYPT_COOKIE;
    return TRUE;
}

BOOL
APIENTRY
CryptAcquireContextA(
    OUT HCRYPTPROV *phProv,
    IN  LPCSTR      szContainer,
    IN  LPCSTR      szProvider,
    IN  DWORD       dwProvType,
    IN  DWORD       dwFlags
    )
{
    UNREFERENCED_PARAMETER( szContainer );
    UNREFERENCED_PARAMETER( szProvider );
    UNREFERENCED_PARAMETER( dwProvType );
    UNREFERENCED_PARAMETER( dwFlags );

    if ( phProv == NULL ) {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    *phProv = MICRONT_CRYPT_COOKIE;
    return TRUE;
}

BOOL
APIENTRY
CryptGenRandom(
    IN     HCRYPTPROV hProv,
    IN     DWORD      dwLen,
    IN OUT BYTE      *pbBuffer
    )
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER( hProv );

    if ( pbBuffer == NULL ) {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    Status = NtGenerateSecureRandom( pbBuffer, dwLen );
    if ( !NT_SUCCESS( Status ) ) {
        SetLastError( RtlNtStatusToDosError( Status ) );
        return FALSE;
    }
    return TRUE;
}

BOOL
APIENTRY
CryptReleaseContext(
    IN HCRYPTPROV hProv,
    IN DWORD      dwFlags
    )
{
    UNREFERENCED_PARAMETER( hProv );
    UNREFERENCED_PARAMETER( dwFlags );
    return TRUE;
}

//
// SystemFunction036 == RtlGenRandom (advapi32 ordinal-documented as the latter).
// BOOLEAN return; fills the buffer from the kernel CSPRNG.
//
BOOLEAN
APIENTRY
SystemFunction036(
    OUT PVOID RandomBuffer,
    IN  ULONG RandomBufferLength
    )
{
    return (BOOLEAN) NT_SUCCESS(
        NtGenerateSecureRandom( RandomBuffer, RandomBufferLength ) );
}
