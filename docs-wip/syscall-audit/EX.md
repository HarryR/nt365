# Syscall audit — EX (Executive)

55 syscalls.  See 
[`README.md`](README.md) for legend, class definitions, and the
rationale for the N/A pre-fills.

## NtAllocateLocallyUniqueId

Source: [`EX/LUID.C`](../../src/NT/PRIVATE/NTOS/EX/LUID.C) · service #6

Generates a fresh `LUID` via `ExpAllocateLocallyUniqueId`.

- [x] C1 Probe-then-deref TOCTOU — output `LUID` probed + written
  inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — fixed `LUID`.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — no access mask.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `LUID` only.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtCancelTimer

Source: [`EX/TIMER.C`](../../src/NT/PRIVATE/NTOS/EX/TIMER.C) · service #9

References the `timer` for `TIMER_MODIFY_STATE`, calls
`KeCancelTimer`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `TIMER_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtClearEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #10

References the `event` for `EVENT_MODIFY_STATE`, calls
`KeClearEvent`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #16

Probes the output handle, calls `ObCreateObject` for
`KEVENT` and `ObInsertObject`.  Same probe + handle-write
shape as the OB/SE family.

- [x] C1 Probe-then-deref TOCTOU — output probed inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — fixed-size object.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObCreateObject` validates
  attributes.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation —
  fixed-size object body.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE`
  only; object body initialised by `Ke…Initialize`.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape as the OB/SE/IO/MM/PS
    siblings.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #17

Probes the output handle, calls `ObCreateObject` for
`KEVENT_PAIR` and `ObInsertObject`.  Same probe + handle-write
shape as the OB/SE family.

- [x] C1 Probe-then-deref TOCTOU — output probed inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — fixed-size object.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObCreateObject` validates
  attributes.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation —
  fixed-size object body.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE`
  only; object body initialised by `Ke…Initialize`.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape as the OB/SE/IO/MM/PS
    siblings.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateMutant

Source: [`EX/MUTANT.C`](../../src/NT/PRIVATE/NTOS/EX/MUTANT.C) · service #22

Probes the output handle, calls `ObCreateObject` for
`KMUTANT` and `ObInsertObject`.  Same probe + handle-write
shape as the OB/SE family.

- [x] C1 Probe-then-deref TOCTOU — output probed inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — fixed-size object.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObCreateObject` validates
  attributes.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation —
  fixed-size object body.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE`
  only; object body initialised by `Ke…Initialize`.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape as the OB/SE/IO/MM/PS
    siblings.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateProfile

Source: [`EX/PROFILE.C`](../../src/NT/PRIVATE/NTOS/EX/PROFILE.C) · service #27

Creates a profiling object that samples a process or thread's
IP at a configurable rate.  Probes the output handle, validates
the buffer + range against MM rules, references the source
process if specified, allocates the profile structure + sample
buffer.

- [x] C1 Probe-then-deref TOCTOU — output probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — `BufferSize` checked against range.
- [x] C5 Integer overflow in size computation — range arithmetic
  range-checked against user-VA bounds.
- [x] C6 Semantic validation gaps
  - `ProfileSource` enum validated.  `RangeBase + RangeSize`
    bounded.  Some sources (e.g. `ProfileTotalIssues`) gated by
    `SeSystemProfilePrivilege`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Sample buffer pinned via MDL; bounded by `BufferSize`
    which itself is bounded by the range × bucket size.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same handle-leak shape.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateSemaphore

Source: [`EX/SEMPHORE.C`](../../src/NT/PRIVATE/NTOS/EX/SEMPHORE.C) · service #29

Probes the output handle, calls `ObCreateObject` for
`KSEMAPHORE` and `ObInsertObject`.  Same probe + handle-write
shape as the OB/SE family.

- [x] C1 Probe-then-deref TOCTOU — output probed inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — fixed-size object.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObCreateObject` validates
  attributes.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation —
  fixed-size object body.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE`
  only; object body initialised by `Ke…Initialize`.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape as the OB/SE/IO/MM/PS
    siblings.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateTimer

Source: [`EX/TIMER.C`](../../src/NT/PRIVATE/NTOS/EX/TIMER.C) · service #32

Probes the output handle, calls `ObCreateObject` for
`KTIMER` and `ObInsertObject`.  Same probe + handle-write
shape as the OB/SE family.

- [x] C1 Probe-then-deref TOCTOU — output probed inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — fixed-size object.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObCreateObject` validates
  attributes.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation —
  fixed-size object body.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE`
  only; object body initialised by `Ke…Initialize`.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape as the OB/SE/IO/MM/PS
    siblings.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtDelayExecution

Source: [`EX/DELAY.C`](../../src/NT/PRIVATE/NTOS/EX/DELAY.C) · service #34

Blocks the calling thread for the captured timeout.

- [x] C1 Probe-then-deref TOCTOU — `Interval` captured via
  `ProbeAndReadLargeInteger`.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — capture inside try.
- [x] C4 Length-field trust — `LARGE_INTEGER` is a fixed type.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — none needed.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A — alertable wait
  handled by `KeDelayExecutionThread`.

---

## NtDisplayString

Source: [`EX/EXINIT.C`](../../src/NT/PRIVATE/NTOS/EX/EXINIT.C) · service #39

**Privileged-ish** — prints a string to the debug console.
Probes the input `UNICODE_STRING`, captures buffer, calls
`HalDisplayString`.

- [x] C1 Probe-then-deref TOCTOU — `String` captured.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — `USHORT`-bounded.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — privilege check
  (`SeTcbPrivilege`-equivalent in some builds).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - String capture bounded by `USHORT MaximumLength`.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtGetTickCount

Source: [`EX/I386/EVPAIR.ASM`](../../src/NT/PRIVATE/NTOS/EX/I386/EVPAIR.ASM) · service #53

Returns the system tick count.  ASM-implemented fast path
(`KE/I386/TRAP.ASM:940`); slow path in
`EX/I386/EVPAIR.ASM:141`.  No user-memory access — returns
`ULONG` in `eax`.

- [x] C1 Probe-then-deref TOCTOU — no input.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — none.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar return.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #67

Probes output handle, calls `ObOpenObjectByName` filtered to
`ExEventObjectType`.

- [x] C1 Probe-then-deref TOCTOU — output probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` validates.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #68

Probes output handle, calls `ObOpenObjectByName` filtered to
`ExEventPairObjectType`.

- [x] C1 Probe-then-deref TOCTOU — output probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` validates.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenMutant

Source: [`EX/MUTANT.C`](../../src/NT/PRIVATE/NTOS/EX/MUTANT.C) · service #72

Probes output handle, calls `ObOpenObjectByName` filtered to
`ExMutantObjectType`.

- [x] C1 Probe-then-deref TOCTOU — output probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` validates.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenSemaphore

Source: [`EX/SEMPHORE.C`](../../src/NT/PRIVATE/NTOS/EX/SEMPHORE.C) · service #76

Probes output handle, calls `ObOpenObjectByName` filtered to
`ExSemaphoreObjectType`.

- [x] C1 Probe-then-deref TOCTOU — output probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` validates.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenTimer

Source: [`EX/TIMER.C`](../../src/NT/PRIVATE/NTOS/EX/TIMER.C) · service #80

Probes output handle, calls `ObOpenObjectByName` filtered to
`ExTimerObjectType`.

- [x] C1 Probe-then-deref TOCTOU — output probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` validates.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same output-handle-leak shape.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtPulseEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #83

References the `event` for `EVENT_MODIFY_STATE`, calls
`KePulseEvent`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryDefaultLocale

Source: [`EX/SYSINFO.C`](../../src/NT/PRIVATE/NTOS/EX/SYSINFO.C) · service #85

Returns the system or thread default `LCID`.

- [x] C1 Probe-then-deref TOCTOU — output `LCID` probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — none.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `LCID` only.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #89

Probes the per-class output struct, references the
`event` for `EVENT_QUERY_STATE`, fills the fixed-size info
struct.

- [x] C1 Probe-then-deref TOCTOU — output probed; written inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — `Length` checked against class size.
- [x] C5 Integer overflow in size computation — fixed.
- [x] C6 Semantic validation gaps — `EVENT_QUERY_STATE` access; only
  `EventBasicInformation` class accepted.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — fields
  written explicitly.
- [x] C11 Reference-count discipline under error paths — object
  derefed on every branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  scalar fields only.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryIntervalProfile

Source: [`EX/PROFILE.C`](../../src/NT/PRIVATE/NTOS/EX/PROFILE.C) · service #96

Returns the configured profile interval for a profile source.

- [x] C1 Probe-then-deref TOCTOU — output `ULONG` probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `ProfileSource` validated.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `ULONG` only.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryMutant

Source: [`EX/MUTANT.C`](../../src/NT/PRIVATE/NTOS/EX/MUTANT.C) · service #98

Probes the per-class output struct, references the
`mutant` for `MUTANT_QUERY_STATE`, fills the fixed-size info
struct.

- [x] C1 Probe-then-deref TOCTOU — output probed; written inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — `Length` checked against class size.
- [x] C5 Integer overflow in size computation — fixed.
- [x] C6 Semantic validation gaps — `MUTANT_QUERY_STATE` access; only
  `MutantBasicInformation` class accepted.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — fields
  written explicitly.
- [x] C11 Reference-count discipline under error paths — object
  derefed on every branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  scalar fields only.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryPerformanceCounter

Source: [`EX/PROFILE.C`](../../src/NT/PRIVATE/NTOS/EX/PROFILE.C) · service #100

Returns the high-resolution performance counter + (optionally)
its frequency.  Probes both `LARGE_INTEGER` outputs.

- [x] C1 Probe-then-deref TOCTOU — outputs probed.
- [x] C2 Direct user-pointer deref without capture — outputs only.
- [x] C3 Missing `__try` wrap — outputs inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — none.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar outputs.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQuerySemaphore

Source: [`EX/SEMPHORE.C`](../../src/NT/PRIVATE/NTOS/EX/SEMPHORE.C) · service #103

Probes the per-class output struct, references the
`semaphore` for `SEMAPHORE_QUERY_STATE`, fills the fixed-size info
struct.

- [x] C1 Probe-then-deref TOCTOU — output probed; written inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — `Length` checked against class size.
- [x] C5 Integer overflow in size computation — fixed.
- [x] C6 Semantic validation gaps — `SEMAPHORE_QUERY_STATE` access; only
  `SemaphoreBasicInformation` class accepted.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — fields
  written explicitly.
- [x] C11 Reference-count discipline under error paths — object
  derefed on every branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  scalar fields only.
- C13 Cancel / completion-routine races — N/A

---

## NtQuerySystemEnvironmentValue

Source: [`EX/SYSENV.C`](../../src/NT/PRIVATE/NTOS/EX/SYSENV.C) · service #105

Returns the value of a named NVRAM (firmware) variable.
Probes input `VariableName` (`UNICODE_STRING`), captures it,
probes output `VariableValue` for `ValueLength`, calls
`HalGetEnvironmentVariable`.

- [x] C1 Probe-then-deref TOCTOU — both captured inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `ValueLength` is by-value.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `SeSystemEnvironmentPrivilege`
  required.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - HAL bounds NVRAM access.
- [x] C10 Uninitialized output / pool-contents leak — HAL fills
  exactly the bytes the variable has.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQuerySystemInformation

Source: [`EX/SYSINFO.C`](../../src/NT/PRIVATE/NTOS/EX/SYSINFO.C) · service #106

The biggest info-class switch in the kernel.  ~50
`SYSTEM_INFORMATION_CLASS` values covering processor info,
memory stats, process list, handle table, performance counters,
modules list, etc.  Each arm probes the output buffer once at
entry, then per-class population.

- [x] C1 Probe-then-deref TOCTOU — single probe at top.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `SystemInformationLength` checked
  against per-class minimum.
- [x] C5 Integer overflow in size computation
  - `SystemProcessInformation`, `SystemHandleInformation`,
    and `SystemModuleInformation` accumulate per-entry sizes;
    each entry's size is bounded.  No per-element multiply
    with attacker-controlled factor.
- [x] C6 Semantic validation gaps — per-class info-class enum
  validated.  Some classes are kernel-mode-only; the others
  are unprivileged in NT 3.5.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Handle-table snapshot is sized by the system-wide handle
    count; bounded by aggregate handle quota.
- [ ] C10 Uninitialized output / pool-contents leak
  - Per-class output structs filled field-by-field; padding
    between fields is not defensively zeroed.  Latent leak
    risk shared with `NtQueryInformationProcess` / `…Thread`
    family.
- [x] C11 Reference-count discipline under error paths
  - Snapshot routines (`ExSnapShotHandleTables`,
    `MmSnapshot…`) hold internal locks while walking; release
    them on every exit.
- [ ] C12 Kernel-address / kernel-pointer leak via info classes — **finding (severe)**
  - `SystemHandleInformation` arm calls
    `ObGetHandleInformation` → `ObpCaptureHandleInformation`
    (`OBHANDLE.C:1584`) which **explicitly copies
    `NonPagedObjectHeader->Object`** into the
    `SYSTEM_HANDLE_TABLE_ENTRY_INFO.Object` field returned to
    user.  This is a raw kernel object pointer.
  - **Reachability**: unprivileged in NT 3.5.  Any process can
    call `NtQuerySystemInformation(SystemHandleInformation,
    …)` and receive a list of every handle in the system
    paired with its kernel-object-pointer.  Defeats kernel-
    ASLR-equivalent assumptions and feeds into corruption
    primitives (now you know where every `EPROCESS`,
    `ETHREAD`, file object, etc. lives).
  - Modern Windows gates this behind `SeDebugPrivilege` and
    zeroes the `Object` field for non-debug callers.  Same
    fix shape here.
  - `SystemProcessInformation` also returns kernel-derived
    state — `UniqueProcessId`, `InheritedFromUniqueProcessId`,
    `CreateTime`, image-name — but no raw kernel pointers.
    OK.
  - `SystemModuleInformation` returns loaded-module list with
    `ImageBase` — kernel-address disclosure (the module load
    addresses).  Unprivileged in NT 3.5.  Modern Windows
    requires `SeDebugPrivilege`.
- C13 Cancel / completion-routine races — N/A

---

## NtQuerySystemTime

Source: [`EX/SYSTIME.C`](../../src/NT/PRIVATE/NTOS/EX/SYSTIME.C) · service #107

Returns the current system time.

- [x] C1 Probe-then-deref TOCTOU — output `LARGE_INTEGER` probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — none.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `LARGE_INTEGER` only.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryTimer

Source: [`EX/TIMER.C`](../../src/NT/PRIVATE/NTOS/EX/TIMER.C) · service #108

Probes the per-class output struct, references the
`timer` for `TIMER_QUERY_STATE`, fills the fixed-size info
struct.

- [x] C1 Probe-then-deref TOCTOU — output probed; written inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — `Length` checked against class size.
- [x] C5 Integer overflow in size computation — fixed.
- [x] C6 Semantic validation gaps — `TIMER_QUERY_STATE` access; only
  `TimerBasicInformation` class accepted.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — fields
  written explicitly.
- [x] C11 Reference-count discipline under error paths — object
  derefed on every branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  scalar fields only.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryTimerResolution

Source: [`EX/SYSTIME.C`](../../src/NT/PRIVATE/NTOS/EX/SYSTIME.C) · service #109

Returns the current/max/min timer resolution.

- [x] C1 Probe-then-deref TOCTOU — three output ULONGs probed.
- [x] C2 Direct user-pointer deref without capture — outputs only.
- [x] C3 Missing `__try` wrap — outputs inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — none.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalars only.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtRaiseHardError

Source: [`EX/HARDERR.C`](../../src/NT/PRIVATE/NTOS/EX/HARDERR.C) · service #114

**Privileged** — requires `SeTcbPrivilege`.  Probes input
parameters array, captures them, dispatches a hard-error popup
via the registered hard-error LPC port.

- [x] C1 Probe-then-deref TOCTOU — parameters captured.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — parameter count bounded by
  `MAXIMUM_HARDERROR_PARAMETERS`.
- [x] C5 Integer overflow in size computation — bounded count.
- [x] C6 Semantic validation gaps — privilege gate.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Parameter copy bounded; LPC message sized by per-port
    `MaxMessageLength`.
- [x] C10 Uninitialized output / pool-contents leak — output is
  user's response code from the hard-error dialog.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtReleaseMutant

Source: [`EX/MUTANT.C`](../../src/NT/PRIVATE/NTOS/EX/MUTANT.C) · service #119

Releases the mutant (decrements ownership count).

- [x] C1 Probe-then-deref TOCTOU — optional `PreviousCount` ULONG
  probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — caller must be the mutant's
  owner.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — ULONG only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtReleaseSemaphore

Source: [`EX/SEMPHORE.C`](../../src/NT/PRIVATE/NTOS/EX/SEMPHORE.C) · service #121

References the `semaphore` for `SEMAPHORE_MODIFY_STATE`, calls
`KeReleaseSemaphore`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `SEMAPHORE_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtResetEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #129

References the `event` for `EVENT_MODIFY_STATE`, calls
`KeResetEvent`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetDefaultHardErrorPort

Source: [`EX/HARDERR.C`](../../src/NT/PRIVATE/NTOS/EX/HARDERR.C) · service #134

**Privileged** — `SeTcbPrivilege`.  References the hard-error
LPC port handle, stashes it in `ExpDefaultErrorPort`.

- [x] C1 Probe-then-deref TOCTOU — no user pointers.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — privilege gate.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetDefaultLocale

Source: [`EX/SYSINFO.C`](../../src/NT/PRIVATE/NTOS/EX/SYSINFO.C) · service #135

Sets the system or thread default `LCID`.

- [x] C1 Probe-then-deref TOCTOU — input by-value.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — system default requires
  `SeSystemtimePrivilege`-equivalent (verify).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetEvent

Source: [`EX/EVENT.C`](../../src/NT/PRIVATE/NTOS/EX/EVENT.C) · service #137

References the `event` for `EVENT_MODIFY_STATE`, calls
`KeSetEvent`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetHighEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #138

References the `event pair` for `EVENT_PAIR_MODIFY_STATE`, calls
`KeSetEventPair (high)`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_PAIR_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetHighWaitLowEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #139

References the `event pair` for `EVENT_PAIR_MODIFY_STATE`, calls
`KeSetHighWaitLowEventPair`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_PAIR_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetHighWaitLowThread

Source: [`EX/I386/EVPAIR.ASM`](../../src/NT/PRIVATE/NTOS/EX/I386/EVPAIR.ASM) · service #140

Special syscall — entered via `INT 2Bh` (no service-number
dispatch).  Wakes the high event of the current thread's
EventPair if attached, then waits on the low event.  Used by
Win32 subsystem for fast thread-pair message dispatch.

- [x] C1 Probe-then-deref TOCTOU — no user pointers.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — requires an attached event
  pair on the current thread.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetIntervalProfile

Source: [`EX/PROFILE.C`](../../src/NT/PRIVATE/NTOS/EX/PROFILE.C) · service #147

Sets the per-source profile interval (system-wide).

- [x] C1 Probe-then-deref TOCTOU — no user pointers.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `SeSystemProfilePrivilege`
  required.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetLowEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #149

References the `event pair` for `EVENT_PAIR_MODIFY_STATE`, calls
`KeSetEventPair (low)`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_PAIR_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetLowWaitHighEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #150

References the `event pair` for `EVENT_PAIR_MODIFY_STATE`, calls
`KeSetLowWaitHighEventPair`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `EVENT_PAIR_MODIFY_STATE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetLowWaitHighThread

Source: [`EX/I386/EVPAIR.ASM`](../../src/NT/PRIVATE/NTOS/EX/I386/EVPAIR.ASM) · service #151

Mirror of `NtSetHighWaitLowThread` for the opposite direction.
Entered via `INT 2Ch`.

- [x] C1 Probe-then-deref TOCTOU — no user pointers.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — requires attached event pair.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetSystemEnvironmentValue

Source: [`EX/SYSENV.C`](../../src/NT/PRIVATE/NTOS/EX/SYSENV.C) · service #153

Mirror of `NtQuerySystemEnvironmentValue` for set.  Requires
`SeSystemEnvironmentPrivilege`.

- [x] C1 Probe-then-deref TOCTOU — name + value captured inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — `USHORT`-bounded.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — privilege gate.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — bounded.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetSystemInformation

Source: [`EX/SYSINFO.C`](../../src/NT/PRIVATE/NTOS/EX/SYSINFO.C) · service #154

Set side of `NtQuerySystemInformation`.  Most classes are
privileged; only a few are accepted (`SystemTimeAdjustment`
etc.).  Probes input buffer + length, validates class.

- [x] C1 Probe-then-deref TOCTOU — input captured.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — `Length` checked per class.
- [x] C5 Integer overflow in size computation — fixed per-class.
- [x] C6 Semantic validation gaps — per-class privilege check
  (`SeSystemtimePrivilege`, `SeTcbPrivilege` etc.).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetSystemTime

Source: [`EX/SYSTIME.C`](../../src/NT/PRIVATE/NTOS/EX/SYSTIME.C) · service #155

**Privileged** — `SeSystemtimePrivilege`.  Sets the system
time-of-day clock; updates CMOS via HAL.

- [x] C1 Probe-then-deref TOCTOU — `NewTime` captured.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — privilege gate.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — optional
  previous-time output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetTimer

Source: [`EX/TIMER.C`](../../src/NT/PRIVATE/NTOS/EX/TIMER.C) · service #156

References the timer for `TIMER_MODIFY_STATE`, captures the
optional `TimerApcRoutine`/`TimerContext`, sets the timer via
`KeSetTimerEx`.

- [x] C1 Probe-then-deref TOCTOU — `DueTime` and APC params
  captured inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — `Period` ULONG;
  bounded by timer-resolution checks downstream.
- [x] C6 Semantic validation gaps — `TIMER_MODIFY_STATE` access;
  APC routine is opaque to kernel.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — optional
  `PreviousState` boolean only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSetTimerResolution

Source: [`EX/SYSTIME.C`](../../src/NT/PRIVATE/NTOS/EX/SYSTIME.C) · service #157

Sets the system-wide timer interrupt rate within
hardware-supported bounds.  Requires `SeIncreaseBasePriorityPrivilege`
or similar.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — privilege gate;
  HAL clamps the value to supported range.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — ULONG only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtShutdownSystem

Source: [`EX/EXINIT.C`](../../src/NT/PRIVATE/NTOS/EX/EXINIT.C) · service #160

**Privileged** — `SeShutdownPrivilege`.  Initiates system
shutdown, optionally rebooting or powering off.

- [x] C1 Probe-then-deref TOCTOU — input by-value enum.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — privilege gate; `Action`
  enum validated.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtStartProfile

Source: [`EX/PROFILE.C`](../../src/NT/PRIVATE/NTOS/EX/PROFILE.C) · service #161

References the profile object, starts sampling.

- [x] C1 Probe-then-deref TOCTOU — no user pointers.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — profile handle validated.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtStopProfile

Source: [`EX/PROFILE.C`](../../src/NT/PRIVATE/NTOS/EX/PROFILE.C) · service #162

References the profile object, stops sampling.  Same shape as
`NtStartProfile`.

- [x] C1 Probe-then-deref TOCTOU — no user pointers.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — profile handle validated.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtSystemDebugControl

Source: [`EX/DBGCTRL.C`](../../src/NT/PRIVATE/NTOS/EX/DBGCTRL.C) · service #164

**Highly privileged** — `SeDebugPrivilege`.  Kernel debugger
control interface.  Includes commands for reading/writing
kernel memory, dumping pool, etc.  Probes input + output
buffers, dispatches per command.

- [x] C1 Probe-then-deref TOCTOU — probes inside try.
- [x] C2 Direct user-pointer deref without capture — input
  captured per command.
- [x] C3 Missing `__try` wrap — inside try.
- [x] C4 Length-field trust — per-command size checks.
- [x] C5 Integer overflow in size computation — per-command
  arithmetic bounded.
- [x] C6 Semantic validation gaps — privilege gate; per-command
  enum validated.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Per-command staging buffers; bounded but caller has
    `SeDebugPrivilege` so this is admin-only DoS surface.
- [ ] C10 Uninitialized output / pool-contents leak
  - Several commands return raw kernel memory dumps (the
    debugger needs this).  Privileged-only; consistent with
    NT design.
- [x] C11 Reference-count discipline under error paths.
- [ ] C12 Kernel-address / kernel-pointer leak via info classes
  - Same: privileged-only, so the leak is by design.  Worth
    documenting that this syscall is a deliberate
    kernel-pointer disclosure channel for the debugger.
- C13 Cancel / completion-routine races — N/A

---

## NtVdmControl

Source: [`EX/VDMSTUB.C`](../../src/NT/PRIVATE/NTOS/EX/VDMSTUB.C) · service #173

**Removed in MicroNT** — NTVDM (16-bit DOS / Win16
emulation) was stripped.  The stub at `VDMSTUB.C:23/34`
returns `STATUS_NOT_IMPLEMENTED`.

- [x] C1 Probe-then-deref TOCTOU — no body.
- [x] C2 Direct user-pointer deref without capture — N/A.
- [x] C3 Missing `__try` wrap — N/A.
- [x] C4 Length-field trust — N/A.
- [x] C5 Integer overflow in size computation — N/A.
- [x] C6 Semantic validation gaps — returns `STATUS_NOT_IMPLEMENTED`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — N/A.
- [x] C10 Uninitialized output / pool-contents leak — N/A.
- [x] C11 Reference-count discipline under error paths — N/A.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — N/A.
- C13 Cancel / completion-routine races — N/A
- **Note**: leaving this syscall slot occupied with a stub
  rather than removing the dispatch entry preserves the
  service-number layout; harmless.

---

## NtWaitHighEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #177

References the `event pair` for `SYNCHRONIZE`, calls
`KeWaitForGate (high)`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `SYNCHRONIZE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtWaitLowEventPair

Source: [`EX/EVENTPR.C`](../../src/NT/PRIVATE/NTOS/EX/EVENTPR.C) · service #178

References the `event pair` for `SYNCHRONIZE`, calls
`KeWaitForGate (low)`.

- [x] C1 Probe-then-deref TOCTOU — optional output ULONG probed.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — none.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — `SYNCHRONIZE` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar
  output only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## Fix-scope summary across EX

### Root-cause groups

1. **Output-handle leak (6 syscalls)** — `NtCreateEvent`,
   `NtCreateEventPair`, `NtCreateMutant`, `NtCreateSemaphore`,
   `NtCreateTimer`, `NtCreateProfile` plus their `NtOpen*`
   siblings (5).  Total **11 sites** with the same shape.

2. **`NtQuerySystemInformation` C12 — kernel-pointer
   disclosure** — `SystemHandleInformation` arm copies
   `NonPagedObjectHeader->Object` (raw kernel pointer) into
   user buffer via `ObGetHandleInformation` (`OBHANDLE.C:1619`).
   `SystemModuleInformation` discloses kernel module load
   addresses.  Both **unprivileged in NT 3.5**; both
   gated behind `SeDebugPrivilege` in modern Windows.

3. **Per-class struct padding (latent)** in
   `NtQuerySystemInformation` — same shape as other
   Query*Information family.

### Fix shape

1. **Output-handle leak (11 sites)** — same `NtClose(Handle)`
   cleanup template.

2. **`NtQuerySystemInformation` kernel-pointer leak** — gate
   the two leaky info classes behind `SeDebugPrivilege`:
   - `SystemHandleInformation` arm: check privilege; if not
     held, either fail with `STATUS_PRIVILEGE_NOT_HELD` or
     zero the `Object` field per entry before returning.
   - `SystemModuleInformation` arm: same — gate or zero
     `ImageBase`.
   - For unprivileged callers, the legitimate uses of these
     classes (Task Manager, Process Explorer's basic mode)
     don't need the raw pointers; modern Windows demonstrates
     that the gating is feasible without breaking real
     workloads.

3. **Defensive zero-init** on per-class info-class arms in
   `NtQuerySystemInformation` — same as PS/CONFIG advice.

### Clean classes

The vast majority of EX is mechanical sync-object plumbing
with no user-pointer surface beyond `HANDLE` outputs.
Mutant/Event/Semaphore/Timer query+set sigs are textbook.

**P14 (deref-before-probe) — swept clean.**  A dedicated P14
prologue audit of every EX `Nt*` entry point found no
deref-before-probe: create/open probe the OUT handle
(`ProbeForWriteHandle`) and hand `OBJECT_ATTRIBUTES` to
`ObCreateObject` / `ObOpenObjectByName`; query probes the
output buffer + `ReturnLength`; the signal syscalls probe
their optional previous-state OUT param; `NtSetTimer` probes
`DueTime` (`ProbeForRead`) before the capture; `NtRaiseHardError`
bounds-checks `NumberOfParameters`, probes `Response` and the
`Parameters` array, then probe-captures each nested
`UNICODE_STRING`.  `test/fuzz/ex.lua` is the 31-test
confirm-net (kernel-range pointer-slot sweep over the bridged
event / event-pair / mutant / semaphore / timer syscalls).

`NtSystemDebugControl` is `SeDebugPrivilege`-gated and
deliberately exposes kernel state — by design.  The C10/C12
"findings" there are documented for completeness rather than
hardening.

`NtVdmControl` is a stub returning `STATUS_NOT_IMPLEMENTED`
since NTVDM was removed from MicroNT.

### Cross-references

- `ObGetHandleInformation` (`OBHANDLE.C:1619`) was already
  flagged during the OB audit as a known kernel-pointer
  source.  Confirmed reachable here.
- `NtSetSystemTime` / `NtSetSystemEnvironmentValue` /
  `NtSetIntervalProfile` / `NtSetTimerResolution` /
  `NtShutdownSystem` / `NtRaiseHardError` /
  `NtSetDefaultHardErrorPort` are all `Se*Privilege`-gated.
  The privilege names should be enumerated in a
  `KERNEL-ABI-HARDENING.md` cross-table.
