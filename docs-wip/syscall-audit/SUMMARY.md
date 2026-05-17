# Syscall audit — consolidated map

The per-subsystem `*.md` files in this directory hold the
finding-by-finding detail.  This document is the **pattern-first
map**: each defect pattern, its reach across subsystems, and the
fix shape that closes every instance at once.

The audit covered **all 182 syscalls** in MicroNT's
`_KiServiceTable`.  Roughly 40 findings spread across the tree;
those 40 findings collapse to **~12 root patterns**.  Most
patterns recur because they're 1990–1992 NT house-style choices
applied uniformly, not localised mistakes by individual authors.

**Methodology note.**  The patterns below were found by static
review against a fixed bug-class catalog.  **P14 was not** — it was
found afterward by the `test/fuzz/*.lua` bugcheck-resistance suites,
in two syscalls (`NtConnectPort`, `NtAcceptConnectPort`) this audit
had explicitly cleared (see P1).  A static per-pattern pass can
certify a syscall only against the patterns in its catalog; it
cannot certify the syscall.  P14 is recorded both as a live pattern
and as a standing caveat on every "closed" above.

## Pattern catalog

Each pattern below lists: (a) the defect, (b) every syscall it
bites, (c) the fix shape.  Patterns are ordered by how many
syscalls they touch — the most-bites-most patterns close the
biggest fraction of findings per edit.

---

### P1 — Output-handle leak on `*Handle` write fault

**Closed.**  Every syscall in the audit's reach list now follows
the `LPCCREAT.C:240-247` template: when the `*Handle` user-mode
write faults, `NtClose(Handle)` runs in the `except` arm and the
function returns `GetExceptionCode()` (or sets `Status` to it
for syscalls that have additional cleanup after the try).
Closed across SE, OB, PS, MM, IO, EX, CONFIG; LPC sites
(`NtAcceptConnectPort`, `NtConnectPort`) were already correct
when audited.

Two pattern variants were folded into the same fix:

- **Silent fall-through** (`} except { }` empty body, or
  `return Status` with `Status == NT_SUCCESS`) — vintage NT 3.5
  "let the caller take an AV when they read their own buffer"
  design choice.  Found in OB, EX, IO, PS, CONFIG.  Changed to
  the LPCCREAT template (`NtClose + report fault`) so the
  syscall surface is honest about what happened.

- **Cross-process variant** (`NtDuplicateObject:1503`) — the
  handle was installed in `TargetProcess`'s table (not the
  caller's) by `ExCreateHandle` while attached.  The fix
  reattaches to `TargetProcess` for the `NtClose` call.

- **CONFIG outer-try variant** — `NtCreateKey`/`NtOpenKey`
  wrap the entire body (probes, open, multiple user-buffer
  writes) in one outer try.  Fix is `HANDLE Handle = NULL` at
  declaration plus `if (Handle != NULL) NtClose(Handle)` in
  the outer except.

**Original shape.**  After `ObInsertObject` /
`ObOpenObjectByName` succeeded, the syscall wrote the handle
value to a user-mode `OUT PHANDLE` parameter inside a `__try`.
On AV the `except` either fell through silently or returned
the success status, leaving the handle installed.

**Severity (when present).**  Self-inflicted DoS only.  Caller
leaks one handle slot per triggering call.  Cross-process
variant (`NtDuplicateObject`) leaked in the target process;
limited by who can hold `PROCESS_DUP_HANDLE` on both source and
target.

---

### P2 — User-controlled `NonPagedPool` length

**Closed.**  `IOP_MAX_TRANSFER_LENGTH = 32 MB` defined in
`IO/IOP.H`; every user-mode entry point in the reach list rejects
out-of-cap lengths with `STATUS_INVALID_PARAMETER` before any
allocation runs:

- `NtReadFile` / `NtWriteFile` — cap on `Length`.
- `NtDeviceIoControlFile` / `NtFsControlFile` (via
  `IopXxxControlFile`) — cap on both `InputBufferLength` and
  `OutputBufferLength`.
- `NtQueryEaFile` / `NtSetEaFile` — cap on `Length` (and
  `EaListLength` on the query side).
- `NtQueryInformationFile` / `NtSetInformationFile`,
  `NtQueryVolumeInformationFile` / `NtSetVolumeInformationFile`,
  `NtQueryDirectoryFile` / `NtNotifyChangeDirectoryFile` — cap
  on `Length`.

The simpler global-cap approach was chosen over the audit's
per-driver `DriverMaxIoSize` proposal: no `DRIVER_OBJECT` layout
change, no per-driver tuning surface, no I/O manager splitting
logic.  32 MB is several orders of magnitude beyond any
legitimate single transfer; larger transfers split into multiple
syscalls naturally.  Per-process quota is unchanged.

**Original shape.**  User-supplied `Length` /
`{Input,Output}BufferLength` parameters fed straight to
`ExAllocatePoolWithQuota` on `NonPagedPool` (or
`NonPagedPoolCacheAligned`), bounded only by `MmUserProbeAddress`
(~2 GB).  Quota tracking debited the caller's process, but a
single huge allocation could exhaust the system pool before
quota throttled it — affecting every other process.

**Original reach list (now all closed):**

| Syscall | Source |
| --- | --- |
| `NtDeviceIoControlFile` / `NtFsControlFile` | `INTERNAL.C` (`IopXxxControlFile`) |
| `NtReadFile`, `NtWriteFile` | `READ.C` / `WRITE.C` system-buffer paths |
| `NtQueryEaFile`, `NtSetEaFile` | `QSEA.C` |
| `NtQueryInformationFile`, `NtSetInformationFile` | `QSINFO.C` |
| `NtQueryVolumeInformationFile`, `NtSetVolumeInformationFile` | `QSFS.C` |
| `NtQueryDirectoryFile`, `NtNotifyChangeDirectoryFile` | `DIR.C` |

**Severity (when present).**  System-wide DoS via non-paged
pool exhaustion.  Any process with a file/device handle (i.e.
nearly all processes) could trigger.

Alternatives considered but not chosen: per-device
`DRIVER_OBJECT.DriverMaxIoSize` advertisement (more structural;
adds driver-side ABI surface, splitting logic in the I/O
manager).  Per-process aggregate non-paged-pool reservation
throttle (more invasive; touches quota machinery).

---

### P3 — Capture-helper overflow (`SeCapture*AndAttributesArray`)

**Cap in place.**  `CAPTURE.C` defines
`SEP_MAX_CAPTURE_COUNT = 0x10000`; both
`SeCaptureLuidAndAttributesArray` (`:1325`) and
`SeCaptureSidAndAttributesArray` (`:1615`) reject
`ArrayCount > SEP_MAX_CAPTURE_COUNT` with
`STATUS_INVALID_PARAMETER` before any multiply, allocation, or
per-element loop runs.  The kernel-pool OOB write primitive in
`SeCaptureSidAndAttributesArray`'s first-pass loop is no longer
reachable; the downstream OOB reads in `SepAdjustPrivileges` /
`SepAdjustGroups` (P5) are bounded by the same cap.  Max
single-call pool footprint: ~768 KB (LUID path) / ~5.5 MB (SID
path).  Tests: `pkg/test/fuzz/se.lua` cover hostile counts
(`cap+1`, two wrap-shapes, `MAXULONG`) across all four
reachable syscalls.  P4 (release-helper NULL feed-through) and
P5 (first-pass `__try`) remain open.

**Original shape.**  Two SE helpers computed
`ArrayCount * sizeof(...)` to size capture allocations without
overflow checking:

- `SeCaptureLuidAndAttributesArray` (`CAPTURE.C:1325`) —
  consistent wrap (probe, alloc, copy all use the wrapped
  value); downstream code reads OOB.
- `SeCaptureSidAndAttributesArray` (`CAPTURE.C:1615`) —
  **inconsistent wrap**; loop iterates the unwrapped count
  against the wrapped allocation.  Kernel-pool OOB **write**
  of attacker-influenced data.  Sustained corruption
  primitive.

**Reach (6+ sites):**

| Helper | Syscall |
| --- | --- |
| `SeCaptureLuidAndAttributesArray` | `NtAdjustPrivilegesToken`, `NtPrivilegeCheck`, `NtCreateToken` |
| `SeCaptureSidAndAttributesArray` | `NtAdjustGroupsToken`, `NtCreateToken` |

Plus reach into `SepAdjustPrivileges` / `SepAdjustGroups`
(`TOKENADJ.C`) which iterate the wrapped capture during
processing — same OOB reads, with `SepAdjustGroups`
additionally dereferencing OOB-read PSIDs via `RtlEqualSid`.

**Severity.**  `SeCaptureSidAndAttributesArray` variant is a
**kernel-pool write primitive** with attacker-controlled
content (the `Sid` field of each entry).  Reachable from
unprivileged callers via `NtAdjustGroupsToken`.

**Fix shape.**  Two ~20-line edits in `CAPTURE.C`:

```c
ArraySize = ArrayCount * sizeof(SID_AND_ATTRIBUTES);
if (ArrayCount > MAX_SAFE_ARRAY_COUNT ||
    ArraySize / sizeof(SID_AND_ATTRIBUTES) != ArrayCount) {
    return STATUS_INVALID_PARAMETER;
}
```

Or backport `RtlULongMult` from NT 4 / 2000 and use it
throughout SE.

Combined with **P5 below**, wraps the first-pass `SepAdjust*`
calls in `__try` so an OOB-read AV becomes a status return,
not a bug-check.

---

### P4 — Release-helper NULL-pointer feed-through

**Closed.**  All five SE release helpers in `CAPTURE.C`
(`SeReleaseSecurityDescriptor`, `SeReleaseSid`,
`SeReleaseAcl`, `SeReleaseLuidAndAttributesArray`,
`SeReleaseSidAndAttributesArray`) now early-return on NULL
before reaching `ExFreePool`.  The two reach sites
(`NtPrivilegeCheck:418` on `PrivilegeCount==0` and
`NtAdjustGroupsToken:799` on `ResetToDefault=TRUE` + faulted
second-pass write) no longer bug-check `BAD_POOL_CALLER`.
Test: `pkg/test/fuzz/se.lua` `NtPrivilegeCheck succeeds on
PrivilegeCount=0` exercises the success-path NULL release; the
fault-path NULL release in `NtAdjustGroupsToken` is harder to
trigger from userspace (needs a read-only `PreviousState`
mapping) and is left untested.

**Original shape.**  `SeReleaseLuidAndAttributesArray`,
`SeReleaseSidAndAttributesArray`, `SeReleaseSid`,
`SeReleaseAcl` all called `ExFreePool(CapturedArray)` without
checking for NULL.  Modern Windows's `ExFreePool` handles NULL;
NT 3.5's bug-checks `BAD_POOL_CALLER`.

Most callers gate releases with
`if (Captured != NULL)`, so the NULL never reaches the helper
in practice.  Two outlier sites don't:

**Reach (2 sites):**

- `NtAdjustGroupsToken:799` — `ResetToDefault=TRUE` path with
  a second-pass user-write fault.
- `NtPrivilegeCheck:418` — `PrivilegeCount=0` reaches the
  success cleanup.

**Severity.**  Local DoS via `BAD_POOL_CALLER` bug-check.
Trivially triggerable.

**Fix shape.**  Add NULL guard at the top of each release
helper.  Two single-line edits in `CAPTURE.C`.

Alternative: backport modern `ExFreePool` NULL-tolerance (one
helper-side edit; benefits the whole kernel).

---

### P5 — First-pass helpers called without `__try`

**Closed.**  Both first-pass `SepAdjust*` calls in
`TOKENADJ.C` are now wrapped in `__try / __except`:

- `NtAdjustPrivilegesToken` first pass at `:313` matches the
  existing second-pass cleanup at `:380-411` — release lock
  with `FALSE`, dereference token, release captured array
  (NULL-safe via P4), return `GetExceptionCode()`.
- `NtAdjustGroupsToken` first pass at `:677` matches the
  existing second-pass cleanup at `:774-801`.

A future bug in `SepAdjust*` or any helper they call that
takes a fault is now a clean error return rather than a
system bug-check.  Combined with P3 (no current OOB-read
primitive from hostile input) and P4 (no NULL-release DoS),
the SE capture/adjust pipeline is structurally clean.
Existing happy-path tests in `pkg/test/se.lua`
(`adjust_privileges save_previous returns prior state`,
`adjust_privileges with mixed enable/disable`, `adjust_groups:
disable a group...`) cover the no-regression case.

**Original shape.**  `SepAdjustPrivileges` (`TOKENADJ.C:313`)
and `SepAdjustGroups` (`TOKENADJ.C:677`) were called for the
"counting" first pass before any user-side writes, both
**outside any `__try` block**.  An OOB-read AV (from **P3**)
inside the first-pass walk would escalate to
`KMODE_EXCEPTION_NOT_HANDLED` and bug-check the system.

---

### P6 — Per-class info-struct padding (latent C10)

**Shape.**  `Nt*Query*Information*` family populates per-class
output structs field-by-field with no `RtlZeroMemory` of the
output buffer before population.  On 32-bit NT 3.5 most of
these structs happen to have no padding; on 64-bit or after
adding fields they'll quietly leak pool bytes from the
kernel's local-variable stack onto user memory.

**Reach (5+ syscalls, latent):**

- `NtQueryVirtualMemory` — `MEMORY_BASIC_INFORMATION`
- `NtQueryInformationProcess` — multiple info classes
- `NtQueryInformationThread` — multiple info classes
- `NtQuerySystemInformation` — most info-class arms
- `NtQueryKey`, `NtEnumerateKey`, `NtEnumerateValueKey`,
  `NtQueryValueKey` — per-class registry info structs

**Severity.**  Latent today (no padding); fragile against
future change.  Modern Windows added defensive zero-init
across these paths.

**Fix shape.**  One-line `RtlZeroMemory(output_struct, size)`
at the top of each per-class arm.  ~20 total small edits.

---

### P7 — `NtQuerySystemInformation` kernel-pointer disclosure

**Shape.**  Two info classes in `NtQuerySystemInformation`
return raw kernel addresses to unprivileged callers:

- **`SystemHandleInformation`** — calls
  `ObGetHandleInformation` (`OBHANDLE.C:1619`) →
  `ObpCaptureHandleInformation` (`:1584`) which copies
  `NonPagedObjectHeader->Object` (raw kernel object pointer)
  into the `SYSTEM_HANDLE_TABLE_ENTRY_INFO.Object` field.
- **`SystemModuleInformation`** — returns the loaded-module
  list with `ImageBase` kernel addresses.

**Severity.**  Severe — defeats kernel-ASLR-equivalent
assumptions.  Any unprivileged process can map out every
kernel object and module address in the system.  Vintage
NT 3.5 (gated behind `SeDebugPrivilege` in modern Windows).

**Fix shape.**  Add `SeDebugPrivilege` check at the top of
the two info-class arms.  Either fail with
`STATUS_PRIVILEGE_NOT_HELD` or zero the `Object`/`ImageBase`
field per entry.  Two small edits in `SYSINFO.C`.

---

### P8 — `NtQueryDirectoryObject` phase-1 kernel-pointer write

**Shape.**  `NtQueryDirectoryObject` (`OBDIR.C:1000-1066`)
uses a two-phase write pattern: phase 1 writes
`DirInfo->Name.Buffer = ObjectName.Buffer` (kernel pointer)
directly into the user output buffer; phase 2 (`querydone:`)
rewrites the field to a user-buffer-relative address.

If phase 1 faults mid-enumeration (e.g. guard-page after the
first page), phase 2 is skipped and kernel pointers remain
in the user buffer.  Function returns
`STATUS_ACCESS_VIOLATION` but the mapped page already
contains the disclosure.

**Reach (1 site):** `NtQueryDirectoryObject`.

**Severity.**  Kernel-pointer disclosure of named-object
region.  Triggerable by any process with `DIRECTORY_QUERY`
access on any directory.

**Fix shape.**  Restructure phase 1 to write user-buffer
offsets directly (computed against the known destination
address), or stage the entire layout in a kernel-pool buffer
and copy with a single `__try`.

---

### P9 — `NtRemoveIoCompletion` data-loss on user-write fault

**Closed.**  `NtRemoveIoCompletion` (`COMPLETE.C`) now writes
the three user outputs (`*ApcContext` / `*KeyContext` /
`*IoStatusBlock`) from kernel-side locals *before* calling
`IoFreeIrp`, all inside the inner `try`.  If a user-buffer
write faults, the `except` arm re-queues the still-intact IRP
via `KeInsertQueue` and returns `GetExceptionCode()` instead of
the previously-unconditional `STATUS_SUCCESS`.  The completion
is no longer consumed-and-lost on a faulted delivery: the
caller gets an error status and the entry stays on the port
for a later `NtRemoveIoCompletion` to drain.

**Original shape.**  At `COMPLETE.C:534-549`, the inner `try`
wrote `*KeyContext` / `*ApcContext` / `*IoStatusBlock` **after**
`IoFreeIrp(Irp)` had returned the IRP to the pool.  On
user-write fault, the completion event was consumed and gone;
the caller received `STATUS_SUCCESS` (set unconditionally)
with uninitialized output pointers.

**Reach (1 site):** `NtRemoveIoCompletion`.

**Severity (when present).**  Classic IOCP-server data-loss
primitive: the server thinks it processed the completion while
the actual data is lost.  The faulting write is a TOCTOU
window — `COMPLETE.C`'s outer probe (`ProbeForWriteLong` /
`ProbeForWriteIoStatus`) touches exactly the bytes the inner
write touches, so a *static* bad page is caught by the probe;
the inner write faults only when a concurrent thread
re-protects or frees an output buffer between probe and write.

---

### P10 — `NtGetContextThread` pool-block zero typo

**Closed.**  Both occurrences of the typo in `PS/PSCTX.C`
(`NtGetContextThread:95` and the benign sibling in
`NtSetContextThread:246`) have been swapped to allocate-then-
zero-the-block:

```c
Ctx = ExAllocatePoolWithQuota(NonPagedPool, sizeof(GETSETCONTEXT));
if (Ctx) {
    RtlZeroMemory(Ctx, sizeof(GETSETCONTEXT));
}
```

`KeContextToKframes` writes only fields requested by
`ContextFlags`, so without the zero the unrequested
fields used to leak kernel pool slot contents to user.

**Original shape.**  `RtlZeroMemory(&Ctx, sizeof(Ctx))` zeroed
the 4-byte pointer variable (immediately clobbered by the
assignment below), not the `GETSETCONTEXT` allocation.

**Severity (when present).**  Per-`CONTEXT`-class kernel-pool
disclosure.  Reachable by any process with `THREAD_GET_CONTEXT`
on a target thread.

---

### P11 — `NonPagedPoolMustSucceed` fallback under pressure

**Closed.**  `MiDoPoolCopy` in `MM/READWRT.C` (the staged-copy
helper behind `NtReadVirtualMemory`/`NtWriteVirtualMemory`) no
longer falls back to `NonPagedPoolMustSucceed`.  The retry loop
still halves the staging buffer from `MAX_MOVE_SIZE` down toward
`MINIMUM_ALLOCATION` (128 bytes) against regular `NonPagedPool`;
once even that minimum request fails it returns
`STATUS_INSUFFICIENT_RESOURCES`.  The status propagates through
`MmCopyVirtualMemory` to both syscalls; `BytesCopied` is
zero-initialised at the syscall entry, so the bytes-transferred
count stays 0 and nothing reads uninitialised memory.  A
transient shortage of large blocks still degrades gracefully via
the halving loop — only total non-paged-pool exhaustion now
fails the call, where it previously risked a system bug-check.

**Original shape.**  `MM/READWRT.C:819` fell back to
`NonPagedPoolMustSucceed` when the regular `NonPagedPool`
allocation failed.  Under pool pressure this could recurse
until the system bug-checked.

**Reach (1 site):** the read/write helper in `READWRT.C`.

**Severity (when present).**  System DoS under pressure.
Required `PROCESS_VM_READ` or `PROCESS_VM_WRITE`, so
privileged-ish.

---

### P12 — `NtAccessCheck` + `NtDuplicateToken` ad-hoc bugs

**Closed.**  Three SE one-off findings, plus a broader leak
caught during the fix:

- **`*PrivilegeSetLength` deref outside `__try`** at
  `ACCESSCK.C:970` and `:1012` — closed by capturing into
  `LocalPrivilegeSetLength` inside the initial probe try; bounds
  checks downstream use the local.
- **Token + SD + Privileges leak at `ACCESSCK.C:1107`**
  (`STATUS_INVALID_SECURITY_DESCR`) — closed.  Broader finding
  caught during the fix: `Privileges` from
  `SePrivilegePolicyCheck` was never freed anywhere in
  `NtAccessCheck`; every post-`:937` return path now calls
  `SeFreePrivileges`, which itself gained a NULL guard matching
  the P4 SE release-helper pattern.
- **`NtDuplicateToken:150` operator typo** (`&&` → `||`) —
  closed.  Invalid `TokenType` values now reject with
  `STATUS_INVALID_PARAMETER`.  Test:
  `pkg/test/fuzz/se.lua` `NtDuplicateToken rejects TokenType=...`.

SE module audit complete.  See [`SE.md`](SE.md) fix-scope
summary for the full closure list.

---

### P13 — `NtSetInformationFile` access-table off-by-one

**Closed.**  `IopSetOperationAccess[]` in `IODATA.C` — the
per-`FILE_INFORMATION_CLASS` required-access table consulted by
`NtSetInformationFile` (`QSINFO.C:924`) — was missing its
`FileCompletionInformation` initializer.  The parallel
`IopSetOperationLength[]` table has all 33 entries; the access
table had only 32, so every class from `FileCompletionInformation`
(30) upward inherited the *next* class's required access:

- `FileCompletionInformation` → `FILE_WRITE_DATA` (move-cluster's)
  instead of `0` — a read-only handle could not be associated
  with a completion port.
- `FileMoveClusterInformation` → `FILE_WRITE_ATTRIBUTES`
  (storage's) instead of `FILE_WRITE_DATA` — cluster relocation,
  which mutates file data, was gated on the wrong right.
- `FileStorageInformation` → `0` (off the end of the initializer)
  instead of `FILE_WRITE_ATTRIBUTES` — **under-protected**: any
  handle could set storage information.

**Reach (1 site):** `NtSetInformationFile`, three info classes.

**Fixed.**  Inserted the missing `0,  // completion` initializer
in `IopSetOperationAccess[]`, realigning every entry with the
`FILE_INFORMATION_CLASS` enum and with `IopSetOperationLength[]`.
Surfaced while building the IOCP completion-source test helper
(`pkg/test/iosrc.lua`), which associates a file with a port via
`FileCompletionInformation`.

---

### P14 — Untrusted-pointer deref without a preceding probe

**LPC, SE, OB swept; reach beyond unknown.**  Found post-audit by the
`test/fuzz/*.lua` pointer-slot sweeps, not by the static pass.

**Defect.**  A syscall reads a field of a caller-supplied `IN`
pointer before a `ProbeForRead/Write` (or a capture helper) has
validated it.  The NT 3.5 house style wraps the prologue in one
`__try` and treats that as sufficient.  It is not:

- `__try` catches a fault on a **user-range** address (the trap
  path raises `STATUS_ACCESS_VIOLATION`, SEH unwinds).  It does
  **not** catch a fault on a **kernel-range** address — that
  bug-checks `0x50` directly, or raises `0x1E` with no handler.
  A hostile caller passes `0x80000000`; `__try` is irrelevant.
- A probe is a **range check** (`p + len <= MmUserProbeAddress`)
  evaluated *before* the dereference — the only thing that
  rejects a kernel-range pointer.  `__try` is a fault catcher,
  not a pointer validator; the two are not interchangeable.
- A probe is **not a capture**.  It validates, it does not copy.
  Probe-then-deref-the-live-pointer leaves a TOCTOU window (this
  is the root of **P9**).  The pointer must be captured into a
  kernel local and only the copy read.

**Instances (all closed):**

| Syscall | Source | Deref-before-probe |
| --- | --- | --- |
| `NtCreatePort` | `LPCCREAT.C` | `ObjectAttributes->ObjectName`, attrs unprobed |
| `ObReferenceObjectByName` | `OBREF.C` | `ObjectName->Length` ahead of `ObpCaptureObjectName` |
| `NtConnectPort` | `LPCCONN.C` | `ClientView/ServerView->Length` before `ProbeForWrite` |
| `NtAcceptConnectPort` | `LPCCOMPL.C` | same view pattern |
| `NtCreateSymbolicLinkObject` | `OBLINK.C` | `ObjectAttributes->Attributes` peeked unprobed |

The first four closed in `70c62cd`, `27eefad`; `NtCreateSymbolicLinkObject`
in the OB-namespace sweep.

**Already in the catalog, fragmented.**  P5 (`SepAdjust*` first
pass called without `__try`) and P12 bullet 1 (`*PrivilegeSetLength`
dereferenced *outside* `__try`, `ACCESSCK.C:970/1012`) are the same
family — an untrusted pointer dereferenced where an AV is not
cleanly handled.  P5 was closed by adding `__try` (necessary, but
per above not sufficient against a kernel-range pointer); P12 was
filed as an "ad-hoc bug."  Neither was generalised, so the catalog
had no P14 entry and the audit never swept for it.  A one-off that
is really a pattern instance is a catalog gap.

**Reach.**  Unknown by construction — the static pass did not look
for this class.  LPC, SE and OB have since been swept with the
per-subsystem `test/fuzz/*.lua` pointer-slot sweeps: SE audited
clean, LPC and OB one-plus fixes each.  IO/MM/PS/CM/EX remain
unswept — every `Nt*` syscall with an `IN` pointer is a candidate.

**Severity.**  Local DoS — system bug-check from an unprivileged
caller passing a kernel-range pointer.  No privilege required
beyond the ability to issue the syscall.  Direct violation of the
bugcheck-resistance invariant.

**Fix shape.**  Per site: probe (or run the capture helper) before
the first field access, for the `PreviousMode != KernelMode` path.
Structurally: Primitive 8.

---

## Defense-in-depth roadmap

Beyond the per-pattern fixes, the audit suggests a set of
**defense-in-depth primitives** that the NT 3.5 kernel
predates.  Backporting them creates a stronger floor for the
fixes above and for whatever comes next.

### Primitive 1 — checked arithmetic helpers

Backport from NT 4 / 2000:

- `RtlULongMult(a, b, &result) → NTSTATUS`
- `RtlULongAdd(a, b, &result) → NTSTATUS`
- `RtlSIZETMult`, `RtlSIZETAdd` (when SIZE_T arrives)

Routes every `ArrayCount * sizeof(...)` style multiply
through overflow detection.  Closes **P3** structurally and
prevents future variants.

### Primitive 2 — `ExFreePool` NULL tolerance

Make `ExFreePool(NULL)` a no-op (modern Windows behavior).
Closes **P4** structurally; every helper that releases
optional pool blocks becomes safer.

### Primitive 3 — `RtlZeroMemoryUlong` / capture-after-alloc pattern

Pair every `ExAllocatePoolWithQuota` (and friends) with
`RtlZeroMemory` of the allocation.  Modern Windows ships
`ExAllocatePoolZero` for this; backport.  Closes **P6** and
**P10** structurally.

### Primitive 4 — `NtCloseSafe` / `ObCloseHandleSafe`

A handle-close that tolerates being called on a fault path
without re-entering kernel-side cleanup loops.  Makes **P1**
fixes safer.

### Primitive 5 — Per-driver `MaxIoSize`

A new `DRIVER_OBJECT` field (or device-extension flag) that
the I/O manager respects when sizing system buffers in
`IopXxxControlFile` / `NtReadFile` / etc.  Default a system-
wide cap (e.g. 32 MB).  Closes **P2** structurally.

### Primitive 6 — `SeDebugPrivilege` gate for kernel-pointer info classes

Modern Windows requires `SeDebugPrivilege` for:

- `NtQuerySystemInformation(SystemHandleInformation)`
- `NtQuerySystemInformation(SystemModuleInformation)`
- `NtQueryInformationProcess(ProcessHandleInformation)`
- A handful of others

Add the gate in MicroNT.  Closes **P7** structurally and
prevents future kernel-pointer-disclosure regressions.

### Primitive 7 — Two-pass staged-write pattern

Standardize "build full layout in kernel pool, single
`__try` `RtlMoveMemory` to user" as the way enumerators
return composite structures with embedded pointers.  Closes
**P8** structurally and prevents similar partial-write
disclosures elsewhere.

### Primitive 8 — Capture-first syscall prologue

The structural close for **P14**.  Modern Windows syscalls open
with a single block that probes and copies *every* `IN` pointer
argument into kernel locals; the body then never touches user
memory again except through `OUT` writes (each with its own
probe + `__try`).  NT 3.5's house style interleaves probe-and-use
down the length of the prologue — and that interleaving is the
bug surface.

NT 3.5 already has the capture helpers (`ObpCaptureObjectName`,
`ObpCaptureObjectAttributes`, `SeCaptureSid`, ...).  The defect is
that they are used inconsistently — `NtCreatePort` hand-peeked
`ObjectName` rather than defer to `ObpCaptureObjectAttributes`;
`ObReferenceObjectByName` ran a fast-fail check ahead of its own
`ObpCaptureObjectName`.  So the primitive is mostly a *discipline*,
not new code: capture is the only path to a user structure, and a
hand-rolled peek at user memory is itself the defect.

Pairs with the fuzz spine: "did this syscall capture-first?" is not
mechanically checkable from source, so the per-subsystem
pointer-slot fuzz sweep is the enforcement and regression net.

---

## Fix-effort summary

| Pattern | Edits | Effort |
| --- | --- | --- |
| ~~P1 — Handle leak~~ (closed kernel-wide) | 22+ sites | done |
| ~~P2 — NonPaged length cap~~ (closed: `IOP_MAX_TRANSFER_LENGTH`) | 1 header + 6 syscall files | done |
| ~~P3 — Capture overflow~~ (closed: cap in `CAPTURE.C`) | 2 helpers | done |
| ~~P4 — Release NULL~~ (closed: NULL guards in `CAPTURE.C`) | 5 helpers | done |
| ~~P5 — First-pass `__try`~~ (closed: both wrapped in `TOKENADJ.C`) | 2 sites | done |
| P6 — Padding zero | ~20 arms, 1 line each | ~20 lines |
| P7 — Kernel-pointer info | 2 sites, ~5 lines each | ~10 lines |
| P8 — Phase-1 disclosure | 1 site, restructure | ~50 lines |
| ~~P9 — IOCP data-loss~~ (closed: reorder + re-queue in `COMPLETE.C`) | 1 site | done |
| ~~P10 — Pool zero typo~~ (closed: alloc-then-zero in `PSCTX.C`) | 2 sites | done |
| ~~P11 — Must-succeed fallback~~ (closed: fallback dropped in `READWRT.C`) | 1 site | done |
| ~~P12 — `NtAccessCheck` adhoc~~ (closed: SE wrap-up commit) | 4 sites | done |
| ~~P13 — SetInfo access-table off-by-one~~ (closed: missing entry inserted in `IODATA.C`) | 1 site | done |
| P14 — Untrusted-pointer deref (LPC/SE/OB swept; IO/MM/PS/CM/EX TBD) | 5 sites done, rest pending fuzz | LPC/SE/OB done |
| **Subtotal — direct fixes** | **~60 edits** | **~400 lines** |
| Primitives backport | 7 primitives | ~200-300 lines per primitive |

**Estimate**: 1-2 weeks for the direct fixes, 3-4 weeks for the
primitives + their conversions.  Total ~5-6 weeks for a
hardening pass that closes 40+ findings and shifts the
defensive-coding floor up by ~25 years.

---

## Cross-references

- [`../KERNEL-ABI-HARDENING.md`](../KERNEL-ABI-HARDENING.md) —
  bug-class catalog this audit is organised against.
- AFD `_AFD_CONTROL_CODE` macro (Class 7) — driver-side bug,
  not covered by the syscall audit but flagged here for the
  hardening doc.
- `IsDHCPZeroAddress` `sin_zero` flag (Class 6) — deliberate
  type confusion, not a vulnerability; documented as an
  example.
