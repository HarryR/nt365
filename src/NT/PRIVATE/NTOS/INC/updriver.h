/*++ BUILD Version: 0050    // Increment this if a change has global effects

Copyright (c) 1990-1994  Microsoft Corporation

Module Name:

    updriver.h

Abstract:

    This module defines the additional NT types, constants, and functions
    that are exposed to uniprocessor-specific drivers.  It is meant to be
    included after ntddk.h or ntifs.h.

Revision History:

--*/

#ifndef _UPDRIVER_
#define _UPDRIVER_


#if defined(_X86_)


#if defined(NT_UP)
#define ExAcquireSpinLock(Lock, OldIrql) KeAcquireSpinLock((Lock), (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeReleaseSpinLock((Lock), (OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock)
#else
#define ExAcquireSpinLock(Lock, OldIrql) KeAcquireSpinLock((Lock), (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeReleaseSpinLock((Lock), (OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock) KeAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock) KeReleaseSpinLockFromDpcLevel(Lock)
#endif



#endif // defined(_X86_)


#if defined(_MIPS_)


#if defined(NT_UP)
#define ExAcquireSpinLock(Lock, OldIrql) KeRaiseIrql(DISPATCH_LEVEL, (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeLowerIrql((OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock)
#else
#define ExAcquireSpinLock(Lock, OldIrql) KeAcquireSpinLock((Lock), (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeReleaseSpinLock((Lock), (OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock) KeAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock) KeReleaseSpinLockFromDpcLevel(Lock)
#endif


#endif // defined(_MIPS_)

#if defined(_ALPHA_)


#if defined(NT_UP)
#define ExAcquireSpinLock(Lock, OldIrql) KeRaiseIrql(DISPATCH_LEVEL, (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeLowerIrql((OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock)
#else
#define ExAcquireSpinLock(Lock, OldIrql) KeAcquireSpinLock((Lock), (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeReleaseSpinLock((Lock), (OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock) KeAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock) KeReleaseSpinLockFromDpcLevel(Lock)
#endif

#endif // _ALPHA_

#if defined(_PPC_)


#if defined(NT_UP)
#define ExAcquireSpinLock(Lock, OldIrql) \
    KeRaiseIrql(DISPATCH_LEVEL, (OldIrql))
#else
#define ExAcquireSpinLock(Lock, OldIrql) \
    KeAcquireSpinLock((Lock), (OldIrql))
#endif

#if defined(NT_UP)
#define ExReleaseSpinLock(Lock, OldIrql) \
    KeLowerIrql((OldIrql))
#else
#define ExReleaseSpinLock(Lock, OldIrql) \
    KeReleaseSpinLock((Lock), (OldIrql))
#endif

#if defined(NT_UP)
#define ExAcquireSpinLockAtDpcLevel(SpinLock)
#else
#define ExAcquireSpinLockAtDpcLevel(SpinLock) \
    KeAcquireSpinLockAtDpcLevel(SpinLock)
#endif

#if defined(NT_UP)
#define ExReleaseSpinLockFromDpcLevel(SpinLock)
#else
#define ExReleaseSpinLockFromDpcLevel(SpinLock) \
    KeReleaseSpinLockFromDpcLevel(SpinLock)
#endif


#endif // defined(_PPC_)

#endif // _UPDRIVER_
