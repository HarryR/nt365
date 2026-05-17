# Syscall audit — OB (Object manager)

15 syscalls.  See 
[`README.md`](README.md) for legend, class definitions, and the
rationale for the N/A pre-fills.

## NtClose

Source: [`OB/OBCLOSE.C`](../../src/NT/PRIVATE/NTOS/OB/OBCLOSE.C) · service #11

`HANDLE` in, status out — no user-memory access in the body.
Validates the handle via `ExMapHandleToPointer`, rejects protected
handles (`OBJ_PROTECT_CLOSE`), audits if `OBJ_AUDIT_OBJECT_CLOSE`,
decrements the handle count, derefs the object.

- [x] C1 Probe-then-deref TOCTOU — no user-memory deref.
- [x] C2 Direct user-pointer deref without capture — no pointers.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps — protected-handle gate at `:71-89`
  rejects user-mode close of `OBJ_PROTECT_CLOSE` handles.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — no
  user-sized allocation.
- [x] C10 Uninitialized output / pool-contents leak — no output buffer.
- [x] C11 Reference-count discipline under error paths
  - `ExUnlockHandleTable` paired with `ExMapHandleToPointer` on both
    the success path (`:99`) and the protected-handle / error path
    (`:86`).  `ObDereferenceObject` at `:138` on success.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateDirectoryObject

Source: [`OB/OBDIR.C`](../../src/NT/PRIVATE/NTOS/OB/OBDIR.C) · service #15

Probes the output `DirectoryHandle`, allocates and zeros a
directory object via `ObCreateObject` + `RtlZeroMemory`, inserts it.

- [x] C1 Probe-then-deref TOCTOU — only output write, inside try.
- [x] C2 Direct user-pointer deref without capture — single output write.
- [x] C3 Missing `__try` wrap — output write inside try.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — fixed-size object.
- [x] C6 Semantic validation gaps — `ObCreateObject` validates
  `ObjectAttributes`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation —
  fixed-size object.
- [x] C10 Uninitialized output / pool-contents leak
  - `RtlZeroMemory(Directory, sizeof(*Directory))` at `:79`
    immediately after `ObCreateObject` — clean.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same handle-leak shape as `NtCreateSymbolicLinkObject` at
    `:95-102`.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a single `HANDLE`.

---

## NtCreateSymbolicLinkObject

Source: [`OB/OBLINK.C`](../../src/NT/PRIVATE/NTOS/OB/OBLINK.C) · service #30

Probes the output `LinkHandle` and the `LinkTarget` `UNICODE_STRING`
(header + buffer), captures both into kernel locals, creates the
symbolic link object via `ObCreateObject` sized by
`MaximumLength` (USHORT-bounded), copies the link target inside a
second `__try`, inserts the handle, writes the handle to user.

- [x] C1 Probe-then-deref TOCTOU — **finding** *(closed: P14 deref-before-probe sweep)*
  - The prologue read `ObjectAttributes->Attributes` at `:166`
    with no probe of `ObjectAttributes` itself.  The enclosing
    `__try` does not make that safe — a kernel-range
    `ObjectAttributes` faults past SEH and bug-checks (pattern
    P14, same shape as `NtCreatePort`).  The original audit pass
    missed it: a bare deref *inside* the probe try reads as clean
    only if `__try` is mistaken for a pointer validator.  Closed
    by `ProbeForRead( ObjectAttributes, … )` at `:162-165` ahead
    of the peek.
  - `*LinkTarget` is probed at `:167` and captured at `:168`; the
    follow-on `RtlMoveMemory` at `:230` re-reads
    `CapturedLinkTarget.Buffer` from user memory — same pointer
    probed at `:169` — inside its own `__try` at `:229-238`.
    TOCTOU window between probe and copy closed by the inner try
    (faults caught; no other consequence).
- [x] C2 Direct user-pointer deref without capture
  - Header values captured into locals; buffer copied wholesale
    into kernel-allocated symbolic link.
- [x] C3 Missing `__try` wrap — all user-memory accesses inside try.
- [x] C4 Length-field trust
  - `CapturedLinkTarget.MaximumLength` captured once, used by
    the probe and the copy.
- [x] C5 Integer overflow in size computation
  - `sizeof(*SymbolicLink) + MaximumLength` cannot overflow:
    `MaximumLength` is `USHORT` (≤ 65535), `sizeof(SymbolicLink)`
    is small.
- [x] C6 Semantic validation gaps
  - Validates `Length > MaximumLength`, odd length, and empty
    name when not `OBJ_OPENIF` at `:182-190`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Bounded by USHORT `MaximumLength` (≤ 65535 + sizeof header).
- [x] C10 Uninitialized output / pool-contents leak
  - `ObCreateObject` body is filled field-by-field at `:214-218`,
    then the link buffer is `RtlMoveMemory`'d into the symbolic
    link.  No uninitialized bytes returned to user.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - At `:247-254`, `*LinkHandle = Handle` write fault is caught
    with a "fall through, since we do not want to undo what we
    have done" comment.  The handle was already inserted via
    `ObInsertObject` (`:239`); the user never receives its
    value.  Same self-inflicted handle-leak pattern as the SE
    syscalls (NtCreateToken, NtOpenProcessToken, etc.).
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a single `HANDLE`.

---

## NtDuplicateObject

Source: [`OB/OBHANDLE.C`](../../src/NT/PRIVATE/NTOS/OB/OBHANDLE.C) · service #40

Validates and references three handles in sequence:
`SourceProcessHandle`, `SourceHandle` (via attached source process),
`TargetProcessHandle`.  Computes the target's granted-access mask,
optionally performs access validation (AVR) when expanding access,
inserts the new handle into the target process's table, writes the
handle value back to user.

- [x] C1 Probe-then-deref TOCTOU — `*TargetHandle` is the only
  user-memory write, inside try.
- [x] C2 Direct user-pointer deref without capture — single output.
- [x] C3 Missing `__try` wrap — output writes inside try.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps
  - `ObpValidateDesiredAccess` at `:1200` rejects reserved bits
    in `DesiredAccess`.  `PROCESS_DUP_HANDLE` required on both
    source and target processes.  When the duplicated handle
    asks for more access than the source has, `SeCreateAccessState`
    plus the type's `SecurityProcedure` perform AVR — except
    when the type uses a private security method, in which case
    `STATUS_ACCESS_DENIED` is returned (`:1419`).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — no
  user-sized allocation.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - The success path's `*TargetHandle = MAKE_OBJECT_HANDLE(NewHandle)`
    write at `:1502-1511` is wrapped in `__try` with "fall
    through, since we cannot undo what we have done."  Same
    handle-leak shape — `ExCreateHandle` succeeded, handle is
    in the target process's handle table, user never receives
    the value.  Self-inflicted on a same-process duplicate;
    cross-process duplicate leaks a handle in the **target**
    process (still callable only by a process that has
    `PROCESS_DUP_HANDLE` on both — limited reachability).
  - Other reference cleanup is meticulous: every error path
    walks through `ObDereferenceObject(SourceObject)`,
    `ObDereferenceObject(SourceProcess)`,
    `ObDereferenceObject(TargetProcess)`, plus
    `PsUnlockProcess` and `SeDeleteAccessState` as
    appropriate.  Symmetric across all branches.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a `HANDLE` (process-scoped handle table index).

---

## NtMakeTemporaryObject

Source: [`OB/OBCLOSE.C`](../../src/NT/PRIVATE/NTOS/OB/OBCLOSE.C) · service #62

`HANDLE` in, status out.  Refs the object, calls
`ObMakeTemporaryObject` (clears `OB_FLAG_PERMANENT_OBJECT`,
triggers name removal), derefs.

- [x] C1 Probe-then-deref TOCTOU — no user-memory deref.
- [x] C2 Direct user-pointer deref without capture — no pointers.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps — handle validated by
  `ObReferenceObjectByHandle` with `DELETE` access requirement.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths
  - Ref at `:174`, deref at `:188` on success; on `!NT_SUCCESS`
    return path no ref was acquired.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenDirectoryObject

Source: [`OB/OBDIR.C`](../../src/NT/PRIVATE/NTOS/OB/OBDIR.C) · service #66

Probes the output `DirectoryHandle`, calls `ObOpenObjectByName`,
writes the handle.

- [x] C1 Probe-then-deref TOCTOU — only output write, inside try.
- [x] C2 Direct user-pointer deref without capture — single output write.
- [x] C3 Missing `__try` wrap — output write inside try.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` validates.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — `HANDLE` only.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same handle-leak shape at `:152-159`.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a single `HANDLE`.

---

## NtOpenSymbolicLinkObject

Source: [`OB/OBLINK.C`](../../src/NT/PRIVATE/NTOS/OB/OBLINK.C) · service #77

Probes the output `LinkHandle`, calls `ObOpenObjectByName`,
writes the handle.

- [x] C1 Probe-then-deref TOCTOU — only output write, inside try.
- [x] C2 Direct user-pointer deref without capture — single output write.
- [x] C3 Missing `__try` wrap — output write inside try.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps — `ObOpenObjectByName` is the
  validator; type-filtered to `ObpSymbolicLinkObjectType`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — no
  user-sized allocation.
- [x] C10 Uninitialized output / pool-contents leak — single
  `HANDLE` output.
- [x] C11 Reference-count discipline under error paths — **finding (minor)** *(closed: P1 handle-leak sweep)*
  - Same handle-leak shape as `NtCreateSymbolicLinkObject` at
    `:330-337`.  Output write fault drops handle name on the
    floor; handle remains in caller's table.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a single `HANDLE`.

---

## NtQueryDirectoryObject

Source: [`OB/OBDIR.C`](../../src/NT/PRIVATE/NTOS/OB/OBDIR.C) · service #87

Probes the output buffer and `Context`, captures `*Context`,
references the directory, iterates the hash buckets writing one
`OBJECT_DIRECTORY_INFORMATION` entry per child, then post-processes
to copy the name strings into the same buffer.  **Two-phase write
pattern**: phase 1 writes entries with `Name.Buffer` pointing at
the kernel-side name strings; phase 2 (`querydone:`) overwrites
those `Buffer` fields with addresses inside the user buffer and
copies the strings in.

- [x] C1 Probe-then-deref TOCTOU
  - `Context` captured at `:926/:938`.  Buffer probe at `:917`
    covers the whole output region.
- [x] C2 Direct user-pointer deref without capture
  - `Context` captured into local; output writes guarded by try.
- [x] C3 Missing `__try` wrap — all user-memory writes inside try.
- [x] C4 Length-field trust
  - `Length` is by-value; comparisons against accumulated
    `TotalLengthNeeded` and `LengthNeeded` (both kernel-derived
    from `USHORT`-bounded directory entry names).
- [x] C5 Integer overflow in size computation
  - `LengthNeeded` and `TotalLengthNeeded` are sums of
    `USHORT`-bounded values across at most
    `NUMBER_HASH_BUCKETS` × bucket-chain entries.  Bounded.
- [x] C6 Semantic validation gaps — root directory mutex held
  across the walk.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — output
  in user buffer; no kernel-pool allocation.
- [x] C10 Uninitialized output / pool-contents leak
  - `RtlZeroMemory(DirInfo, sizeof(*DirInfo))` at `:1044` zeroes
    the trailing NULL-terminator entry on success.  Per-entry
    fields are filled explicitly.
- [x] C11 Reference-count discipline under error paths
  - `ObReferenceObjectByHandle` at `:946`; `ObDereferenceObject`
    at `:1083` reached on every path (including the
    `querydone` write-fault branch).  Root mutex released at
    `:1081`.
- [ ] C12 Kernel-address / kernel-pointer leak via info classes — **finding**
  - Phase 1 at `:1000-1015` writes
    `DirInfo->Name.Buffer = ObjectName.Buffer` (kernel pointer)
    and `DirInfo->TypeName.Buffer = NonPagedObjectHeader->Type->Name.Buffer`
    (kernel pointer) directly into the user output buffer.
    Phase 2 at `:1042-1066` rewrites those `Buffer` fields to
    point at user-space offsets inside the same buffer, then
    `RtlMoveMemory`s the name strings in.
  - **Phase-2 is skipped when `NT_SUCCESS(Status)` is false**.
    If phase-1's try at `:1000-1015` faults on entry N (e.g.
    user buffer has a guard page after the first page), the
    function sets `Status = GetExceptionCode()` at `:1014` and
    `goto querydone`.  In `querydone`, `if (NT_SUCCESS(Status))`
    is false → phase-2 fixup skipped.  Entries `0 .. N-1` and
    the partial entry N retain **kernel pointers** in their
    `Buffer` fields.
  - Triggering: allocate the output buffer with one mapped page
    followed by a guard page, request enough directory entries
    that filling them crosses the page boundary mid-entry.
    Function returns `STATUS_ACCESS_VIOLATION`, but the mapped
    page contains `OBJECT_DIRECTORY_INFORMATION` entries with
    `Name.Buffer` and `TypeName.Buffer` pointing to kernel
    name-string addresses — disclosing kernel pool/data layout
    of named-object regions.
  - Fix shape: phase 1 should never write a kernel pointer to
    user memory.  Either compute the user-space offsets up
    front (knowing the entry stride and total entry count, the
    name-buffer offsets are predictable) and write those, or
    write `NULL`/`0` in phase 1 and only set the real (user)
    buffer pointer in phase 2.  Alternative: build the entire
    layout in a kernel-pool staging buffer and single-`__try`
    `RtlMoveMemory` it to the user.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryObject

Source: [`OB/OBQUERY.C`](../../src/NT/PRIVATE/NTOS/OB/OBQUERY.C) · service #99

Switch over 5 `OBJECT_INFORMATION_CLASS` values
(`ObjectBasicInformation`, `ObjectNameInformation`,
`ObjectTypeInformation`, `ObjectTypesInformation`,
`ObjectHandleFlagInformation`).  `ObjectTypesInformation` is the
only arm that does not require a `Handle`.

- [x] C1 Probe-then-deref TOCTOU
  - Single `ProbeForWrite(ObjectInformation, ObjectInformationLength, …)`
    at `:71/:77` covers the output buffer.  Per-arm writes inside
    `__try`.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output writes inside try.
- [x] C4 Length-field trust
  - `ObjectInformationLength` checked against per-arm minimum
    sizes (`sizeof(OBJECT_BASIC_INFORMATION)` etc.).
- [x] C5 Integer overflow in size computation
  - `ObjectBasicInformation` arm: `NameInfoSize` accumulates
    `USHORT` lengths walking the parent-directory chain; bounded
    by directory tree depth × `USHORT_MAX`.  No wrap concern.
  - `ObjectTypesInformation` arm: iterates ≤ `OBP_MAX_DEFINED_OBJECT_TYPES`
    types — bounded by compile-time constant.
- [x] C6 Semantic validation gaps
  - Default arm at `:324` returns `STATUS_INVALID_INFO_CLASS`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak
  - `ObjectBasicInformation`: struct filled field-by-field; if
    no symbolic link, `CreationTime` is `RtlZeroMemory`'d at
    `:150-152`.  All fields written explicitly.
  - Other arms: per-arm helpers populate output struct.
- [x] C11 Reference-count discipline under error paths
  - `ObReferenceObjectByHandle` at `:94`; conditional deref at
    `:329-331` (`if (Object != NULL)` — only `ObjectTypesInformation`
    skips ref).  Default arm at `:325` derefs before return.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - `ObjectBasicInformation` returns `HandleCount`, `PointerCount`,
    `PagedPoolCharge`, etc. — counts/sizes, not pointers.
  - `ObjectTypesInformation` builds `OBJECT_TYPE_INFORMATION`
    entries inside the user buffer with `TypeName.Buffer`
    pointing into the user buffer (via `ObQueryTypeInfo`'s
    fixup).  No kernel pointers exposed.
  - `ObjectNameInformation` (`ObQueryNameString`) builds a path
    string by walking the parent-directory chain; the
    `Name.Buffer` field is set to point into the user buffer
    at `:466+`.  No kernel pointers exposed.

---

## NtQuerySecurityObject

Source: [`OB/OBSE.C`](../../src/NT/PRIVATE/NTOS/OB/OBSE.C) · service #102

Probes the output `SecurityDescriptor` for `Length` bytes and the
`LengthNeeded` ULONG, references the object, dispatches to the
type's `SecurityProcedure(QuerySecurityDescriptor, …)` which writes
the SD into the user buffer, writes `*LengthNeeded`, derefs.

- [x] C1 Probe-then-deref TOCTOU
  - Output probes at `:185-187`; `*LengthNeeded` write at
    `:228-236` inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output writes inside try.
- [x] C4 Length-field trust
  - `Length` is by-value; the per-type `SecurityProcedure`
    receives `&Length` and updates it to the actual SD size.
- [x] C5 Integer overflow in size computation
  - `Length` bounded by `ProbeForWrite`'s wrap detection.
- [x] C6 Semantic validation gaps
  - `SeQuerySecurityAccessMask` derives the required access from
    `SecurityInformation`; handle must have it.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak
  - Per-type `SecurityProcedure` is responsible for writing the
    SD self-relative format with all bytes accounted for.  No
    SD padding bytes leak from the OB layer.
- [x] C11 Reference-count discipline under error paths
  - Ref at `:202`, deref at `:234`/`:238`.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Self-relative SD format uses offsets, not pointers.

---

## NtQuerySymbolicLinkObject

Source: [`OB/OBLINK.C`](../../src/NT/PRIVATE/NTOS/OB/OBLINK.C) · service #104

Probes both the `LinkTarget` `UNICODE_STRING` header and its
`Buffer` (for writeability), and `ReturnedLength` if present.
References the symbolic link, copies the target string into the
user buffer in a `__try`, derefs the object.

- [x] C1 Probe-then-deref TOCTOU
  - `CapturedLinkTarget` captured at `:389` inside the probe try.
    Later writes (`:429`, `:435`, `:437`, `:449`) sit in their
    own `__try` blocks.
- [x] C2 Direct user-pointer deref without capture
  - `LinkTarget` struct captured into local; `CapturedLinkTarget.Buffer`
    used as a destination pointer for `RtlMoveMemory` inside try.
- [x] C3 Missing `__try` wrap — all user-memory access inside try.
- [x] C4 Length-field trust
  - Bounds-check at `:420-426` compares
    `SymbolicLink->Link.Length` / `MaximumLength` (kernel-side)
    against `CapturedLinkTarget.MaximumLength` (captured).
- [x] C5 Integer overflow in size computation — no size arithmetic
  beyond USHORT-bounded lengths.
- [x] C6 Semantic validation gaps
  - Bounds + buffer-too-small handling at `:420-459`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — no
  kernel allocation in the syscall body.
- [x] C10 Uninitialized output / pool-contents leak
  - Copies bytes 0..`Length` (or `MaximumLength`) from
    kernel-side `SymbolicLink->Link.Buffer`.  The kernel-side
    buffer was populated wholesale from user-supplied bytes at
    create time, so the trailing area between `Length` and
    `MaximumLength` may carry whatever was in that user buffer
    originally — but that's user-supplied content, not kernel
    pool.
- [x] C11 Reference-count discipline under error paths
  - `ObReferenceObjectByHandle` at `:412`, `ObDereferenceObject`
    at `:461` — reached unconditionally when ref succeeded.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - `LinkTarget->Length` is updated to the actual length;
    `Buffer` field is left pointing to the user-side buffer the
    caller supplied.  No kernel pointers written.

---

## NtSetInformationObject

Source: [`OB/OBQUERY.C`](../../src/NT/PRIVATE/NTOS/OB/OBQUERY.C) · service #143

Single valid info class (`ObjectHandleFlagInformation`); probes
the input, captures it, changes the handle-table entry via
`ExChangeHandle`.

- [x] C1 Probe-then-deref TOCTOU
  - Single probe + capture into `Params.CapturedObjectInfo`
    inside `__try` at `:666-675`.  No re-read.
- [x] C2 Direct user-pointer deref without capture — captured into
  local struct.
- [x] C3 Missing `__try` wrap — input deref inside try.
- [x] C4 Length-field trust
  - `ObjectInformationLength` checked against
    `sizeof(OBJECT_HANDLE_FLAG_INFORMATION)` at `:685`.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps
  - Invalid info classes rejected at `:681-683`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths
  - No object reference taken — `ExChangeHandle` operates on the
    handle table entry directly.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output.

---

## NtSetSecurityObject

Source: [`OB/OBSE.C`](../../src/NT/PRIVATE/NTOS/OB/OBSE.C) · service #152

References the object with security-information-appropriate access,
captures the user `SECURITY_DESCRIPTOR` via `SeCaptureSecurityDescriptor`,
validates owner/group presence, dispatches to the type's
`SecurityProcedure(SetSecurityDescriptor, …)`, releases.

- [x] C1 Probe-then-deref TOCTOU — capture done by helper.
- [x] C2 Direct user-pointer deref without capture — SD captured.
- [x] C3 Missing `__try` wrap — `SeCaptureSecurityDescriptor`
  handles probe + capture under try.
- [x] C4 Length-field trust — SD self-describing; helper validates.
- [x] C5 Integer overflow in size computation
  - `SeCaptureSecurityDescriptor` and the type's security
    procedure handle their own bounds.
- [x] C6 Semantic validation gaps
  - Validates `SecurityDescriptor != NULL` at `:103-106`.
  - Validates owner/group presence when the corresponding
    `SecurityInformation` bit is set at `:118-126`.
  - `SeSetSecurityAccessMask` derives the required access mask
    from `SecurityInformation`, so the handle must have the
    right access bits.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - `SeCaptureSecurityDescriptor` allocates `PagedPool` sized by
    the SD's internal lengths — bounded by SD format
    (sub-counts are `USHORT`).
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths
  - Object ref'd at `:74`, dereffed at `:104`/`:129`/`:145`.
    `CapturedDescriptor` released at `:146` after the procedure
    returns.  Symmetric.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output.

---

## NtWaitForMultipleObjects

Source: [`OB/OBWAIT.C`](../../src/NT/PRIVATE/NTOS/OB/OBWAIT.C) · service #174

Validates `Count` in [1, MAXIMUM_WAIT_OBJECTS] and `WaitType` is
`WaitAny`/`WaitAll`.  Probes + captures `Handles[]` array and
optional `Timeout`.  Walks handle table inline (lock held) to
ref each object.  Optionally allocates `KWAIT_BLOCK` array from
non-paged pool for large counts.  Calls `KeWaitForMultipleObjects`.

- [x] C1 Probe-then-deref TOCTOU
  - `Handles[]` probed at `:240` for `Count * sizeof(HANDLE)`;
    captured into stack array `CapturedHandles[MAXIMUM_WAIT_OBJECTS]`
    inside the same try at `:243-248`.  `Timeout` captured at
    `:236`.
- [x] C2 Direct user-pointer deref without capture — all captured.
- [x] C3 Missing `__try` wrap — probe + capture inside try.
- [x] C4 Length-field trust
  - `Count` validated `[1, MAXIMUM_WAIT_OBJECTS]` at `:214`.
- [x] C5 Integer overflow in size computation
  - `Count * sizeof(HANDLE)` at `:240` and `Count * sizeof(KWAIT_BLOCK)`
    at `:263`: `Count ≤ MAXIMUM_WAIT_OBJECTS` (typically 64), so
    no overflow possible.
- [x] C6 Semantic validation gaps
  - `WaitType` validated at `:223`.  Duplicate-object check for
    `WaitAll` at `:352-365`.  Per-handle `SYNCHRONIZE` access
    check at `:300-305`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - `WaitBlockArray` from `NonPagedPool` at `:264`, sized
    `Count × sizeof(KWAIT_BLOCK)` — bounded by
    `MAXIMUM_WAIT_OBJECTS`.
- [x] C10 Uninitialized output / pool-contents leak — no output buffer.
- [x] C11 Reference-count discipline under error paths
  - Inline handle-table walk increments ref count per object;
    `ServiceFailed:` label at `:385` walks back through
    `Objects[0..RefCount-1]` calling `ObDereferenceObject`.
    `WaitBlockArray` freed at `:397-399` if allocated.  Every
    error path goes through the cleanup label.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output buffer (return value is status / wait index).
- C13 Cancel / completion-routine races — N/A — same alert/APC
  basis as `NtWaitForSingleObject`.

---

## NtWaitForSingleObject

Source: [`OB/OBWAIT.C`](../../src/NT/PRIVATE/NTOS/OB/OBWAIT.C) · service #175

Probes + captures the optional `Timeout`, references the wait
object with `SYNCHRONIZE` access, computes the
`KWAIT_OBJECT`-derived address from the object's
`DefaultObject` field, calls `KeWaitForSingleObject`.

- [x] C1 Probe-then-deref TOCTOU — `Timeout` captured via
  `ProbeAndReadLargeInteger` at `:92`.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — capture inside try.
- [x] C4 Length-field trust — no length-bearing parameter.
- [x] C5 Integer overflow in size computation — no size arithmetic.
- [x] C6 Semantic validation gaps
  - Handle validated for `SYNCHRONIZE` access.  Object type
    not constrained (any waitable type is allowed).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths
  - Ref at `:105`, deref at `:133` after wait returns.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output.
- C13 Cancel / completion-routine races — N/A — alert/APC based
  wait cancellation, not IRP-style; `KeWaitForSingleObject`
  handles it internally.

---

## Fix-scope summary across OB

### Root-cause groups

1. **Output-handle leak on `*OutHandle` write fault** — same pattern
   as in SE.  After `ObInsertObject` / `ExCreateHandle` succeeds,
   the `*Handle = LocalHandle` write inside a fault-catching
   `__try` discards the handle name if the user-side page faults.
   The handle remains installed in the caller's (or target
   process's) handle table; the value is never communicated.
   Self-inflicted by callers in same-process cases, mildly
   cross-process for `NtDuplicateObject`.
   - Bites: `NtCreateSymbolicLinkObject:247`,
     `NtOpenSymbolicLinkObject:330`,
     `NtCreateDirectoryObject:95`,
     `NtOpenDirectoryObject:152`,
     `NtDuplicateObject:1502`.

2. **`NtQueryDirectoryObject` kernel-pointer leak via partial
   phase-1 writes** (C12 finding) — phase 1 of the directory
   enumeration writes `DirInfo->Name.Buffer = ObjectName.Buffer`
   (kernel pointer) into the user buffer; phase 2 rewrites those
   to user-space offsets.  If phase 1 faults on a guard page mid-
   enumeration, phase 2 is skipped and the kernel pointers
   persist in the user buffer.  Returns
   `STATUS_ACCESS_VIOLATION` to the caller but the pointer
   bytes are already written and readable.

### Fix shape

1. **Handle-leak fixes (5 syscalls)** — replace the
   "fall-through" comment pattern with:
   ```c
   try {
       *OutHandle = LocalHandle;
   } except( EXCEPTION_EXECUTE_HANDLER ) {
       NtClose( LocalHandle );
       return GetExceptionCode();
   }
   ```
   For `NtDuplicateObject` the close needs to happen in the
   target-process context (`KeAttachProcess(&TargetProcess->Pcb)`
   around `NtClose`).  Same five-site shape as the SE
   handle-leak fixes.

2. **`NtQueryDirectoryObject` C12 fix** — restructure the
   two-phase write so phase 1 never stores a kernel pointer in
   the user buffer.  Two equivalent options:
   - **Pre-compute user offsets**: knowing the entry stride and
     the total entry count up front, write the (user) name
     buffer addresses directly in phase 1.  Phase 2 becomes
     just the `RtlMoveMemory` string copies, with all
     `Buffer` fields already correct.
   - **Stage in kernel pool**: allocate a paged-pool staging
     buffer sized to `Length`, build the entire layout there
     (with user-space pointer values computed against the
     known destination), then single-`__try` `RtlMoveMemory`
     the whole staging buffer to user.  Costs a `Length`-sized
     pool allocation per call but eliminates the partial-write
     window entirely.

   The staging-buffer option is the more conservative fix and
   matches the pattern used by `SeCaptureSidAndAttributesArray`
   etc.  The pre-compute option is leaner but error-prone.

### Clean classes

Every other OB syscall checked clean across all 11 applicable
classes.  No C5 wraps reach OB (no per-element captures with
user-controlled counts above the IOCTL layer).  Refcount
discipline is consistently meticulous — every error path walks
back through every `ObDereferenceObject`, `ExUnlockHandleTable`,
`PsUnlockProcess`, etc. that was taken.

### Cross-references

- `ObGetHandleInformation` (`OBHANDLE.C:1619`) and
  `ObpCaptureHandleInformation` (`:1584`) leak kernel object
  pointers via the `SYSTEM_HANDLE_TABLE_ENTRY_INFO.Object`
  field, but those helpers are only reached through
  `NtQuerySystemInformation(SystemHandleInformation)` which
  lives in the PS subsystem — track that finding when auditing
  PS.  Vintage NT 3.5 design (the info class was unprivileged in
  3.5; modern Windows requires SE_DEBUG).
