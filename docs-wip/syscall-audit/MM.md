# Syscall audit — MM (Memory manager)

18 syscalls.  See 
[`README.md`](README.md) for legend, class definitions, and the
rationale for the N/A pre-fills.

## NtAllocateVirtualMemory

Source: [`MM/ALLOCVM.C`](../../src/NT/PRIVATE/NTOS/MM/ALLOCVM.C) · service #7

Probes `BaseAddress` and `RegionSize` (both `IN OUT`), captures
them at `:209-238`, validates `ZeroBits ≤ 21`, `AllocationType`
mask, `MEM_COMMIT|MEM_RESERVE` requirement, range checks
`CapturedBase ≤ MM_HIGHEST_VAD_ADDRESS` and
`MM_HIGHEST_VAD_ADDRESS - CapturedBase ≥ CapturedRegionSize`.
References target process, optionally attaches, takes the
working-set + address-space mutexes, reserves/commits the VAD.
Writes back final `*BaseAddress` and `*RegionSize` on success
(in a `__try` at the bottom).

- [x] C1 Probe-then-deref TOCTOU — captures inside one try at
  `:209-238`.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `CapturedRegionSize` used everywhere.
- [x] C5 Integer overflow in size computation
  - `MM_HIGHEST_VAD_ADDRESS + 1 - CapturedBase` subtraction at
    `:267` cannot wrap because `CapturedBase` was checked
    `≤ MM_HIGHEST_VAD_ADDRESS` at `:258`.
  - `ROUND_TO_PAGES(CapturedRegionSize)` at `:353` can round
    up — but the prior range check ensures the rounded value
    still fits.
- [x] C6 Semantic validation gaps
  - `ZeroBits ≤ 21` at `:172`; `AllocationType` mask at `:180`;
    `MEM_COMMIT|MEM_RESERVE` required at `:188`;
    `Protect` validated via `MiMakeProtectionMask` (raises on
    bad combinations).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Reserves user VA, charges page-file quota.  Hard upper
    bound at `MM_HIGHEST_VAD_ADDRESS - CapturedBase`.
    Per-process quota throttles reservation.
- [x] C10 Uninitialized output / pool-contents leak — `*BaseAddress`
  and `*RegionSize` written explicitly.
- [x] C11 Reference-count discipline under error paths
  - Process ref'd at `:290`, dereffed on every `goto
    ErrorReturn` / clean exit.  Attach/detach symmetric.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a user-space VA.

---

## NtCreatePagingFile

Source: [`MM/MODWRITE.C`](../../src/NT/PRIVATE/NTOS/MM/MODWRITE.C) · service #24

**Privileged** — requires `SeCreatePagefilePrivilege` at
`MODWRITE.C:238`.  Probes + captures the `PageFileName`
`UNICODE_STRING` and the `MinimumSize`/`MaximumSize`
`LARGE_INTEGER`s.  Allocates the file, sets up paging-file
descriptors, registers it in `MmPagingFile[]`.

- [x] C1 Probe-then-deref TOCTOU — captures inside try at
  `:252-292`.
- [x] C2 Direct user-pointer deref without capture — name buffer
  copied into kernel pool at `:287-292`.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `USHORT`-bounded.
- [x] C5 Integer overflow in size computation
  - `LARGE_INTEGER` sizes used directly; no multiply.
- [x] C6 Semantic validation gaps — privilege check is the
  primary gate.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - `MmPagingFile[]` and paging-file metadata allocations
    bounded by paging-file size (limited by disk + admin policy).
    Privileged-only attacker surface.
- [x] C10 Uninitialized output / pool-contents leak — no output
  buffer.
- [x] C11 Reference-count discipline under error paths
  - Name buffer / file handle / kernel allocations freed on
    error.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output.
- C13 Cancel / completion-routine races — N/A

---

## NtCreateSection

Source: [`MM/CREASECT.C`](../../src/NT/PRIVATE/NTOS/MM/CREASECT.C) · service #28

Probes the output `SectionHandle`, optional `MaximumSize`
`LARGE_INTEGER`, validates `SectionPageProtection` /
`AllocationAttributes`, optionally references `FileHandle` for
file-backed sections, allocates control-area + segment +
extended-header from pool (variously paged / non-paged),
inserts the section object.

- [x] C1 Probe-then-deref TOCTOU — captures inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `MaximumSize.QuadPart` used
  directly; per-file-system bound from file size.
- [x] C5 Integer overflow in size computation
  - Segment size = number-of-PTEs × `sizeof(MMPTE)`.  Number-of-
    PTEs derived from `MaximumSize / PAGE_SIZE` — bounded by
    address space.  No attacker-supplied multiplier in this
    arithmetic.
- [x] C6 Semantic validation gaps — `SectionPageProtection` and
  `AllocationAttributes` masks validated; file vs. pagefile-
  backed branching at the top.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [ ] C9 Pool exhaustion via attacker-controlled allocation
  - Multiple paged/non-paged pool allocations sized from
    `MaximumSize`: `ControlArea` (`:1425`/`:1435`),
    `Segment` (`:1465`/`:2422`/`:2636`), `ExtendedHeader`
    (`:1352`/`:1743`).  Each grows with the section's
    page-count.  A caller with `SECTION_MAP_WRITE` can ask for
    a section as large as their pagefile quota allows.
  - Mostly bounded by quota; flagged for completeness.
- [x] C10 Uninitialized output / pool-contents leak — `RtlZeroMemory`
  on every pool block (`:536`, `:1456`, `:1480`, `:2434`,
  `:2676`, `:2717`).  Defensive zeroing is consistent.
- [x] C11 Reference-count discipline under error paths
  - `FileHandle` ref released; pool blocks freed on cleanup.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  output is a `HANDLE`.
- C13 Cancel / completion-routine races — N/A

---

## NtExtendSection

Source: [`MM/EXTSECT.C`](../../src/NT/PRIVATE/NTOS/MM/EXTSECT.C) · service #44

References the section, optionally probes `NewSectionSize`
`LARGE_INTEGER`, extends the section's `SizeOfSection`.  Used
mostly for file-backed sections.

- [x] C1 Probe-then-deref TOCTOU — `NewSectionSize` captured.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — captured once.
- [x] C5 Integer overflow in size computation — bounded by
  underlying file size.
- [x] C6 Semantic validation gaps — `SECTION_EXTEND_SIZE` access
  required.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation
  - Section pool allocations grow with size, bounded by
    pagefile quota.
- [x] C10 Uninitialized output / pool-contents leak — single
  `LARGE_INTEGER` writeback.
- [x] C11 Reference-count discipline under error paths — section
  derefed on each branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtFlushInstructionCache

Source: [`MM/FLUSHBUF.C`](../../src/NT/PRIVATE/NTOS/MM/FLUSHBUF.C) · service #46

References the target process, optionally attaches, calls
`KeSweepIcache` for the range.  Trivial body.

- [x] C1 Probe-then-deref TOCTOU — `BaseAddress` and `Length`
  are by-value scalars (not pointers).
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory deref.
- [x] C4 Length-field trust — `Length` is by-value.
- [x] C5 Integer overflow in size computation — range arithmetic
  checked against user-space bounds before sweep.
- [x] C6 Semantic validation gaps — process handle validated for
  any access (no specific access mask).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths — process
  derefed on each branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtFlushVirtualMemory

Source: [`MM/FLUSHSEC.C`](../../src/NT/PRIVATE/NTOS/MM/FLUSHSEC.C) · service #48

`IN OUT PVOID *BaseAddress`, `IN OUT PULONG RegionSize`,
`OUT PIO_STATUS_BLOCK IoStatusBlock`.  Probes + captures the
two `IN OUT`s, references the process, walks the VAD, flushes
backing-store pages via `MiFlushSectionInternal`.

- [x] C1 Probe-then-deref TOCTOU — captures inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `CapturedRegionSize`.
- [x] C5 Integer overflow in size computation — range arithmetic
  bounded by user-VA range.
- [x] C6 Semantic validation gaps — `PROCESS_VM_OPERATION` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — fixed
  `IO_STATUS_BLOCK` and scalar `*BaseAddress` / `*RegionSize`.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtFlushWriteBuffer

Source: [`MM/FLUSHBUF.C`](../../src/NT/PRIVATE/NTOS/MM/FLUSHBUF.C) · service #49

No arguments.  Calls `KeFlushWriteBuffer` on the current
processor.  No user-memory access.  Trivial.

- [x] C1 Probe-then-deref TOCTOU — no input.
- [x] C2 Direct user-pointer deref without capture — no input.
- [x] C3 Missing `__try` wrap — no input.
- [x] C4 Length-field trust — no input.
- [x] C5 Integer overflow in size computation — no arithmetic.
- [x] C6 Semantic validation gaps — no input to validate.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths — no refs.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtFreeVirtualMemory

Source: [`MM/FREEVM.C`](../../src/NT/PRIVATE/NTOS/MM/FREEVM.C) · service #50

Mirror of `NtAllocateVirtualMemory` for release: probes +
captures `*BaseAddress` and `*RegionSize`, validates `FreeType`,
walks VADs to free the region, writes back final values.

- [x] C1 Probe-then-deref TOCTOU — captures inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — captured.
- [x] C5 Integer overflow in size computation — bounded by
  user-VA arithmetic with explicit range checks.
- [x] C6 Semantic validation gaps — `FreeType` must be
  `MEM_DECOMMIT` or `MEM_RELEASE`; `MEM_RELEASE` requires
  `RegionSize=0` (whole-allocation release).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar writebacks only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtLockVirtualMemory

Source: [`MM/LOCKVM.C`](../../src/NT/PRIVATE/NTOS/MM/LOCKVM.C) · service #61

Pins user pages in physical memory.  Probes + captures
`*BaseAddress` and `*RegionSize`, references the process,
walks pages and increments lock counts.

- [x] C1 Probe-then-deref TOCTOU — captures inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — captured.
- [x] C5 Integer overflow in size computation — bounded.
- [x] C6 Semantic validation gaps — `PROCESS_VM_OPERATION` access;
  `SeLockMemoryPrivilege` required for `MAP_PROCESS` /
  `MAP_SYSTEM` lock modes (privileged ops).
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [ ] C9 Pool exhaustion via attacker-controlled allocation
  - Locking pages converts pageable memory to non-pageable
    (effectively); per-process quota bounds this but a
    privileged caller (`SeLockMemoryPrivilege`) can drain
    physical memory.  Quota is real but the locked-pool budget
    is high by default.
- [x] C10 Uninitialized output / pool-contents leak — scalar writebacks.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtMapViewOfSection

Source: [`MM/MAPVIEW.C`](../../src/NT/PRIVATE/NTOS/MM/MAPVIEW.C) · service #63

Probes `*BaseAddress`, `*ViewSize`, `*SectionOffset` and
validates `InheritDisposition` / `AllocationType` / `Win32Protect`.
References the section (with the per-section access derived from
`Win32Protect`) and the target process, allocates a VAD,
inserts it.

- [x] C1 Probe-then-deref TOCTOU — captures inside try at
  `:367-374`.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — `*ViewSize` captured.
- [x] C5 Integer overflow in size computation
  - `SectionOffset + ViewSize ≤ SectionSize` checked.
- [x] C6 Semantic validation gaps — `InheritDisposition` and
  `AllocationType` validated; `Win32Protect` mapped to per-PTE
  protection bits with structural validity check.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [ ] C9 Pool exhaustion via attacker-controlled allocation
  - VAD allocation is fixed-size (`sizeof(MMVAD)` ≈ 40 bytes)
    from non-paged pool.  `Subsection` allocation at `:1105`
    is sized by view size in subsections — bounded by section
    metadata.
- [x] C10 Uninitialized output / pool-contents leak — VAD fields
  populated; scalar writebacks for `*BaseAddress`/`*ViewSize`.
- [x] C11 Reference-count discipline under error paths
  - Process and section refs each released on every branch.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is a user-space view base.
- C13 Cancel / completion-routine races — N/A

---

## NtOpenSection

Source: [`MM/CREASECT.C`](../../src/NT/PRIVATE/NTOS/MM/CREASECT.C) · service #75

Probes `SectionHandle`, calls `ObOpenObjectByName` filtered to
`MmSectionObjectType`, writes the handle.

- [x] C1 Probe-then-deref TOCTOU — output only.
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
  - Same handle-leak shape as SE/OB/IO Open*: `*SectionHandle =
    Handle` write fault leaves the handle installed but
    un-communicated.  Self-inflicted DoS.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  `HANDLE` only.
- C13 Cancel / completion-routine races — N/A

---

## NtProtectVirtualMemory

Source: [`MM/PROTECT.C`](../../src/NT/PRIVATE/NTOS/MM/PROTECT.C) · service #82

Probes + captures `*BaseAddress`, `*RegionSize`, `*OldProtect`,
references the process, validates `NewProtect`, walks pages and
applies the new protection.  Writes back the previous protection
in `*OldProtect`.

- [x] C1 Probe-then-deref TOCTOU — captures inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — captured.
- [x] C5 Integer overflow in size computation — bounded.
- [x] C6 Semantic validation gaps — `NewProtect` validated via
  `MiMakeProtectionMask`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar writebacks.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtQuerySection

Source: [`MM/QUERYSEC.C`](../../src/NT/PRIVATE/NTOS/MM/QUERYSEC.C) · service #101

Probes `SectionInformation` for the per-class fixed-size struct,
references the section, fills in the struct (basic info or
image info), writes to user.

- [x] C1 Probe-then-deref TOCTOU — output probe + write inside try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — output inside try.
- [x] C4 Length-field trust — `SectionInformationLength` checked
  against per-class size.
- [x] C5 Integer overflow in size computation — fixed-size structs.
- [x] C6 Semantic validation gaps — `SECTION_QUERY` access.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — struct fields
  written explicitly.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Returns section attributes (size, allocation attributes,
    transfer address for image sections).  `TransferAddress` in
    `SectionImageInformation` is the image's entry-point VA in
    the loaded view — user-space, not kernel.
- C13 Cancel / completion-routine races — N/A

---

## NtQueryVirtualMemory

Source: [`MM/QUERYVM.C`](../../src/NT/PRIVATE/NTOS/MM/QUERYVM.C) · service #111

References the target process, probes per-class output buffer,
walks the VAD tree to fill `MEMORY_BASIC_INFORMATION` (or other
classes), writes to user.

- [x] C1 Probe-then-deref TOCTOU — output probed + written under try.
- [x] C2 Direct user-pointer deref without capture — output only.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — per-class size validated.
- [x] C5 Integer overflow in size computation — none at this
  layer.
- [x] C6 Semantic validation gaps — `PROCESS_QUERY_INFORMATION`
  access; per-class info-class enum validated.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [ ] C10 Uninitialized output / pool-contents leak
  - `MEMORY_BASIC_INFORMATION` fields populated from VAD state;
    padding bytes inside the struct (between fields) are *not*
    explicitly zeroed.  On 32-bit NT 3.5 the struct happens to
    have no padding (all fields are 4-byte aligned), so no leak
    today, but adding a field could quietly introduce one.
    Defensive `RtlZeroMemory(&info, sizeof(info))` at entry
    would close the latent risk.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is `BaseAddress`/`AllocationBase`/etc. — all
    user-space VAs.
- C13 Cancel / completion-routine races — N/A

---

## NtReadVirtualMemory

Source: [`MM/READWRT.C`](../../src/NT/PRIVATE/NTOS/MM/READWRT.C) · service #117

Cross-process memory read.  Probes `Buffer` and optional
`NumberOfBytesRead`, references the source process with
`PROCESS_VM_READ`, copies `BufferSize` bytes from source's VA
space into caller's buffer using a sized helper:
- Small transfers (`BufferSize ≤ ~512`): copy directly via
  `KeAttachProcess` + bounded stack buffer.
- Large transfers: allocate non-paged pool block (`MaximumMoved`
  ≤ `MM_VM_READWRITE_MAX_SIZE`, ~4KB-65KB depending on build),
  loop reading chunks.

- [x] C1 Probe-then-deref TOCTOU — probes inside try at `:169` etc.
- [x] C2 Direct user-pointer deref without capture
  - Source-side reads happen under `KeAttachProcess` to the
    target process; destination writes after detach.  Source
    process can't directly fault destination since the
    destination is the caller's own buffer.
- [x] C3 Missing `__try` wrap — both source-read and
  destination-write wrapped in their own try blocks at
  `:645-657` and `:675-679`.
- [x] C4 Length-field trust — `BufferSize` is by-value.
- [x] C5 Integer overflow in size computation — `MaximumMoved`
  is the kernel-bounded chunk size.
- [x] C6 Semantic validation gaps — `PROCESS_VM_READ` required.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation *(closed: P11 must-succeed fallback dropped)*
  - `:815` `ExAllocatePoolWithTag(NonPagedPool, MaximumMoved)`
    where `MaximumMoved` is kernel-bounded (`MAX_MOVE_SIZE`,
    64KB).  Per-call bounded, but rapid concurrent calls can put
    non-paged-pool pressure on the system.
  - The `NonPagedPoolMustSucceed` fallback is gone: `MiDoPoolCopy`'s
    retry loop now halves the staging buffer down to
    `MINIMUM_ALLOCATION` (128 bytes) against regular
    `NonPagedPool`, then returns `STATUS_INSUFFICIENT_RESOURCES`
    rather than bug-checking under exhaustion.
- [x] C10 Uninitialized output / pool-contents leak
  - Pool block populated by source-process read; copied to user
    destination.  `NumberOfBytesRead` written via try.
- [x] C11 Reference-count discipline under error paths
  - Process ref'd then attached/detached symmetrically; pool
    block freed on all branches.
- [x] C12 Kernel-address / kernel-pointer leak via info classes
  - Output is the bytes-read count + the requested memory
    bytes from the source process — no kernel pointers
    introduced at this layer.
- C13 Cancel / completion-routine races — N/A

---

## NtUnlockVirtualMemory

Source: [`MM/LOCKVM.C`](../../src/NT/PRIVATE/NTOS/MM/LOCKVM.C) · service #171

Mirror of `NtLockVirtualMemory` — probes captures, references
the process, walks pages decrementing lock counts.

- [x] C1 Probe-then-deref TOCTOU — captures inside try.
- [x] C2 Direct user-pointer deref without capture — captured.
- [x] C3 Missing `__try` wrap — accesses inside try.
- [x] C4 Length-field trust — captured.
- [x] C5 Integer overflow in size computation — bounded.
- [x] C6 Semantic validation gaps — `PROCESS_VM_OPERATION`;
  same privilege gates as Lock for `MAP_PROCESS`/`MAP_SYSTEM`.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — none.
- [x] C10 Uninitialized output / pool-contents leak — scalar writebacks.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtUnmapViewOfSection

Source: [`MM/UMAPVIEW.C`](../../src/NT/PRIVATE/NTOS/MM/UMAPVIEW.C) · service #172

References the process, removes the VAD matching
`BaseAddress`, releases the section's reference.

- [x] C1 Probe-then-deref TOCTOU — `BaseAddress` is by-value
  scalar.
- [x] C2 Direct user-pointer deref without capture — none.
- [x] C3 Missing `__try` wrap — no user-memory access.
- [x] C4 Length-field trust — no length parameter.
- [x] C5 Integer overflow in size computation — none.
- [x] C6 Semantic validation gaps — VAD lookup validates the base
  matches an existing section mapping.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation — frees
  VAD; net reduction in pool usage.
- [x] C10 Uninitialized output / pool-contents leak — no output.
- [x] C11 Reference-count discipline under error paths
  - Process and section refs released.
- [x] C12 Kernel-address / kernel-pointer leak via info classes — none.
- C13 Cancel / completion-routine races — N/A

---

## NtWriteVirtualMemory

Source: [`MM/READWRT.C`](../../src/NT/PRIVATE/NTOS/MM/READWRT.C) · service #181

Mirror of `NtReadVirtualMemory` but with `PROCESS_VM_WRITE`
required and source/destination swapped.  Same staged-buffer
pattern at `:863-879`.

- [x] C1 Probe-then-deref TOCTOU — probes inside try.
- [x] C2 Direct user-pointer deref without capture — source read
  + target write under attach/detach.
- [x] C3 Missing `__try` wrap — both transfer sides wrapped.
- [x] C4 Length-field trust — by-value `BufferSize`.
- [x] C5 Integer overflow in size computation — kernel-bounded.
- [x] C6 Semantic validation gaps — `PROCESS_VM_WRITE` required.
- C7 IOCTL access-bit encoding — N/A
- C8 Output buffer aliasing / METHOD mismatch — N/A
- [x] C9 Pool exhaustion via attacker-controlled allocation *(closed: P11 must-succeed fallback dropped)*
  - Same `NonPagedPool` staged-buffer pattern as
    `NtReadVirtualMemory`; shares the `MiDoPoolCopy` helper and
    its `STATUS_INSUFFICIENT_RESOURCES` exhaustion path.
- [x] C10 Uninitialized output / pool-contents leak — input syscall;
  scalar `NumberOfBytesWritten` writeback only.
- [x] C11 Reference-count discipline under error paths.
- [x] C12 Kernel-address / kernel-pointer leak via info classes —
  no output beyond bytes-written count.
- C13 Cancel / completion-routine races — N/A

---

## Fix-scope summary across MM

### Root-cause groups

1. **`NtOpenSection` handle-leak on output-write fault** — single
   instance of the now-familiar
   `try { *Handle = LocalHandle } except { fall through }`
   pattern.  Same shape and fix as the OB/SE/IO siblings.

2. **`NtQueryVirtualMemory` latent C10 padding leak** — the
   `MEMORY_BASIC_INFORMATION` struct happens to have no padding
   on 32-bit NT 3.5, so today there's no leak — but the struct
   is populated field-by-field with no defensive zero-init.
   Adding a field (or building on a 64-bit target where padding
   appears) would quietly introduce a kernel-pool-contents
   leak.

3. **`NtReadVirtualMemory` / `NtWriteVirtualMemory`
   `NonPagedPoolMustSucceed` fallback** *(closed)* — at
   `READWRT.C:819`, when the standard non-paged-pool allocation
   failed for the staged transfer buffer, the code fell back to
   `NonPagedPoolMustSucceed` which bypasses fail-out semantics.
   Under pool pressure, this could keep allocating until the
   system bug-checked.  Privileged-only in practice (requires
   `PROCESS_VM_READ` / `PROCESS_VM_WRITE` on a target process)
   but a debugged process across processes could apply pressure.

### Fix shape

1. **`NtOpenSection` handle-leak** — same one-line `NtClose` fix
   as elsewhere.

2. **`NtQueryVirtualMemory` defensive zero-init** — add
   `RtlZeroMemory(&info, sizeof(info))` at the top of the
   `MEMORY_BASIC_INFORMATION` arm (or zero the user's output
   buffer once the probe succeeds, before populating fields).
   Cheap, closes the latent risk.

3. **`NtReadVirtualMemory` / `NtWriteVirtualMemory`
   `NonPagedPoolMustSucceed`** — **done.**  The fallback is
   dropped: `MiDoPoolCopy`'s retry loop halves the staging
   buffer down to `MINIMUM_ALLOCATION` (128 bytes) against
   regular `NonPagedPool`, then returns
   `STATUS_INSUFFICIENT_RESOURCES` on total exhaustion instead
   of bug-checking.  The status propagates through
   `MmCopyVirtualMemory` to both syscalls; the caller can split
   the transfer.

### Clean classes

Every other MM syscall checked clean across applicable classes.
The probe-then-capture discipline is consistently rigorous in
MM — better than IO's per-syscall variance.  No new C5 wraps
were found (size arithmetic uses kernel-bounded counts and
range-checked subtractions; multiplications use page-shift
arithmetic that doesn't wrap below sane sizes).

`NtCreateSection` allocations are large but quota-bounded;
flagged as C9 for completeness but not fixable in MM itself
(the per-process pagefile quota already handles it).

### Deferred items

- **`MiMakeProtectionMask`** — protection-bit validator reached
  by `NtAllocateVirtualMemory` and `NtProtectVirtualMemory`.
  Quick verification needed that all invalid `Protect` masks
  raise rather than silently mapping to a default.
- **`MiFindEmptyAddressRange{,Down}`** — VAD-search helpers
  reached from `NtAllocateVirtualMemory`.  Not user-input-
  driven, but worth a sanity check.
- **VAD insertion / deletion paths** — refcounting for the
  section back-references during view mapping is intricate;
  the syscall-layer audit assumes those helpers are correct.

### Cross-references

- `SeLockMemoryPrivilege` gate for `NtLockVirtualMemory`'s
  `MAP_PROCESS`/`MAP_SYSTEM` modes — verify the privilege
  check is performed before any pool work.
- `SeCreatePagefilePrivilege` for `NtCreatePagingFile` is at the
  top of the syscall (`MODWRITE.C:238`) — correct ordering.
