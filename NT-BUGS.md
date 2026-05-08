# NT source bugs, surfaced by MicroNT

A running collection of load-bearing bugs baked into the NT 3.5 source tree
that went unnoticed for decades because nobody ever actually *ran* the code
from scratch. Keeping notes so one day I can bother Raymond Chen about them.

---

## 1. The CSR quick-LPC protocol silently lies to its clients

### The punchline

When the kernel-side LPC dispatcher in CSRSS fails to route an API call — say
because the target subsystem's server DLL isn't registered — it returns the
NTSTATUS error (`STATUS_ILLEGAL_FUNCTION = 0xC00000AF`) in the same 32-bit
field that normally carries the API's *return value*. The client has no way
to distinguish "the API ran and returned 0xC00000AF bytes" from "the server
couldn't dispatch your call and this is an error code." So it happily
consumes `0xC00000AF` as a byte-count, and `RtlMoveMemory`'s next instruction
walks three-and-a-half gigabytes off the end of the caller's stack buffer.

Every unregistered GDI LPC in NT 3.5 is one unchecked `memmove` away from
annihilation. No one noticed because Microsoft always registered GDISRV.

### The setup

`CSR_QLPC_API_MSG` — the shared-memory message struct used by the fast LPC
path (`int 0x2c` / event-pair thread swap) — has exactly one field for
carrying information back from the server:

```c
// src/NT/PUBLIC/SDK/INC/NTCSRMSG.H
typedef struct _CSR_QLPC_API_MSG {
    ULONG Length;
    CSR_API_NUMBER ApiNumber;
    ULONG ReturnValue;      // ← the entire return channel. one ULONG.
    QLPC_ACTION Action;     // { CsrQLpcCall, CsrQLpcReturn }
    ULONG ServerSide;
    UCHAR ApiMessageData[ 4 ];
} CSR_QLPC_API_MSG;
```

The `Action` field is a two-value enum that discriminates call direction
(client→server vs. server→client callback). There is no status field.
There is no error flag. There is no sentinel range. `ReturnValue` means
whatever the API decides `ReturnValue` means.

### What happens when dispatch fails

In `src/NT/PRIVATE/CSR/SERVER/SRVQUICK.C:767`, the quick-LPC dispatcher checks
whether the requested server-DLL slot is actually populated:

```c
if (ServerDllIndex >= CSR_MAX_SERVER_DLL ||
    (LoadedServerDll = CsrLoadedServerDll[ ServerDllIndex ]) == NULL ) {
    ...
    LastReturnValue = (ULONG)STATUS_ILLEGAL_FUNCTION;
    break;
}
```

`LastReturnValue` is then written to `Msg->ReturnValue` at the bottom of the
loop. The server thread signals back to the client. The client wakes from
`int 0x2c` and receives: `Msg->ReturnValue = 0xC00000AF`.

### What the client does with it

The GDI client stub `cjGetNonFontObject` in
`src/NT/PRIVATE/WINDOWS/GDI/CLIENT/OBJECT.C:3579` issues an `EXTGETOBJECTW`
LPC and does this on the reply:

```c
cRet = CALLSERVER();

if ((cRet != 0) && (pv != (LPVOID) NULL))
    RtlMoveMemory((PBYTE) pv, (PBYTE) (pmsg+1), cRet);
```

`cRet` is `0xC00000AF`. `pv` is the caller-supplied destination buffer
(for example, `&BITMAP BitmapInfo;` — a 24-byte local in `PaintBitmapWindow`).
`RtlMoveMemory` promptly starts copying ~3 GB from the shared reply buffer
to the client's stack. The first write lands on the stack-base page. The
second one lands on the first *un*mapped page past it. The process eats an
access violation. The SEH dispatcher tries to run — on a stack that no
longer exists. STATUS_STACK_OVERFLOW. EIP=0. The cascade is beautiful in
the way a flaming train derailment is beautiful.

On MicroNT with `mkhive.py` missing the `ServerDll=winsrv:GdiServerDllInitialization,4`
line, every dialog paint turned into exactly this. Winlogon could load,
negotiate with LSASS, build dialogs — all of which went through USERSRV
(properly registered, slot 3) and never hit the hole. Then the WM_PAINT
path called `GetObject(hbmBitmap, sizeof(BITMAP), &BitmapInfo)` and boom.

### The real protocol bug

Even once GDISRV is properly registered, the bug doesn't go away — it just
waits. Any future subsystem mis-registration, any out-of-range function
index, any dispatch-layer glitch will produce the same 3 GB memmove.
The NT 3.5 source has no mechanism at all to distinguish "API returned X"
from "dispatch failed, this is an NTSTATUS." The quick-LPC protocol was
designed assuming dispatch always succeeds — which in production always
held, because Microsoft's own CSRSS shipped with all four `ServerDll=`
entries hardcoded into the registry template.

It is a single-ULONG protocol carrying a sum type with no tag bit.

### What makes it especially fun

`CSR_QLPC_API_MSG` is mirrored by a MASM `_CSR_QLPC_API_MSG STRUC` in NT4
with identical field offsets. Any attempt to add a discriminant field to
the struct breaks ASM consumers. The `Action` field is `DD` on the wire
and callers compare it with `==` against a two-value enum — so the
repair *is* backward-compatible at the byte layout level: widen the enum,
not the struct. Two integer comparisons are backward-compatible; an
inserted field is not.

### The fix

Extend `QLPC_ACTION` with a third value:

```c
typedef enum _QLPC_ACTION {
    CsrQLpcCall   = 0,
    CsrQLpcReturn = 1,
    CsrQLpcError  = 2    // NEW — dispatch failed, ReturnValue is NTSTATUS
} QLPC_ACTION;
```

In `SRVQUICK.C::CsrpProcessApiRequest`, when dispatch bails out, also set
`Msg->Action = CsrQLpcError` alongside the existing `ReturnValue =
STATUS_ILLEGAL_FUNCTION`. In `CsrClientSendMessage` in ntdll, check
`Action == CsrQLpcError` after the `int 0x2c` resume, translate
`ReturnValue` through `RtlNtStatusToDosError`, stash it in
`Teb->LastErrorValue`, and return 0 — the convention GDI's MSGERROR
cleanup path already uses for "the API failed."

Total: one new enum value, one new assignment on two bail-out paths,
one check in the client's LPC glue. No struct layout change. Every old
caller still works unchanged.

### The gotcha during implementation

`CsrpProcessApiRequest` has three call sites in `SRVQUICK.C`, each of
which unconditionally overwrites `Msg->Action = CsrQLpcReturn`
immediately after the function returns, pre-emptively erasing the tag
this fix just set. All three call sites need the guard
`if (Msg->Action != CsrQLpcError)`. And this — this is the best part —
the call site that actually fires for real LPC traffic is NOT
`CsrpDedicatedClientThread` (as the obvious-looking one). It is
`CsrClientCallback`, a function whose name, comment, and general
vibe suggest it only runs for server-initiated callbacks, but which
is in fact the live server-side dispatcher reached by the int-0x2c
path. If you only guard the dedicated-thread path — which is what
every reasonable reading of the code would tell you to do — the tag
is still silently overwritten and the crash still fires.

The comment at `SRVQUICK.C:636` over `CsrClientCallback` reads in full:

```
ULONG
CsrClientCallback( VOID )
```

Zero doc comment. Zero context. This is the routine through which every
cross-process GDI call in NT 3.5 actually flows. Twenty-nine years of
Microsoft developers apparently just knew.

### Relevant diffs landed in MicroNT

- `src/NT/PUBLIC/SDK/INC/NTCSRMSG.H` — added `CsrQLpcError = 2`.
- `src/NT/PRIVATE/CSR/SERVER/SRVQUICK.C` — set `Msg->Action = CsrQLpcError`
  on the two bail-out paths; guard the unconditional
  `Msg->Action = CsrQLpcReturn` assignments in both
  `CsrpDedicatedClientThread` *and* `CsrClientCallback`.
- `src/NT/PRIVATE/NTOS/DLL/CSRQUICK.C` — `CsrClientSendMessage` now
  checks `Msg->Action == CsrQLpcError` after `int 0x2c` and converts
  to `Teb->LastErrorValue` + return 0.

### For Raymond

Hypothetical title: *"Why doesn't my GDI call just return an error when
the server isn't running?"* Answer: "It does. It returns
`STATUS_ILLEGAL_FUNCTION`. You just memcpy'd three gigabytes of it into
your stack."

---

## 2. Memory Manager: Copy-on-Write does not work for image sections

### The punchline

The NT loader (`LdrpInitializeProcess`) resolves every DLL's Import
Address Table at process start by writing function pointers into the
IAT pages. If a DLL stores its IAT in a read-only section — as every
Microsoft-shipped pre-built DLL from the VC++ 2.2 era does — those
writes are supposed to trigger copy-on-write: the MM allocates a
private page, copies the section contents, remaps the PTE as writable,
and the write transparently succeeds. In the NT 3.5 tree as shipped,
**the MM never does any of that.** The write faults with
`STATUS_ACCESS_VIOLATION`, the IAT stays at its link-time values
(usually zero or a stale RVA), and the DLL runs with every imported
function pointing at garbage. The first call through the IAT branches
to `EIP=0` and the process dies.

If you build your own DLLs with LINK 2.50 you don't hit this, because
that linker puts the IAT in its own writable `.idata` section. If you
ship anyone else's DLLs — NETAPI32, NETRAP, WINSPOOL, CRTDLL — you
can't load them.

### The setup

In NT's PE-section ACL model, image sections are mapped `PAGE_EXECUTE_READ`
or `PAGE_READONLY` unless the section explicitly has `IMAGE_SCN_MEM_WRITE`
in its characteristics. The loader writes *into those pages anyway*
during IAT fixup, relocation processing, and forwarded-import patching,
relying on the kernel's copy-on-write machinery to silently turn each
"write to a read-only section" into "write to a private copy of that
section." This is so load-bearing that Windows literally cannot boot
without it working — every DLL in the system depends on it during
initialization.

The MM design is:

1. `NtMapViewOfSection` maps image pages with `PAGE_WRITECOPY` —
   read-only in the hardware PTE, with the `CopyOnWrite` bit set.
2. Writing to a COW page faults.
3. `MmAccessFault` at `MM/MMFAULT.C:452` sees the `CopyOnWrite` bit,
   calls `MiCopyOnWrite` (`MM/WRTFAULT.C:25`), which allocates a
   private page, memcpies the section contents, updates the PTE to
   point at the new page with write access, and returns
   `STATUS_PAGE_FAULT_COPY_ON_WRITE`.
4. User-mode write instruction is restarted; it succeeds against the
   private page.
5. Every other process mapping the same DLL still sees the original
   pristine page.

Step 3 is exactly the kind of subsystem where "it never worked and
Microsoft never noticed" is genuinely implausible. Except it's what
we observe.

### What actually happens

Boot a winlogon that imports NETAPI32.DLL. The loader hits the
`.rdata` page containing NETAPI32's IAT (starts at RVA `0x7000` in
our copy, VA `0x77AF7000` after ASLR):

```
CSRSRV: CsrSbCreateSession — Process=0025A360
UMODE EXC(1st): code=c0000005 addr=6010554b p0=00000001 p1=77af7000
```

- `p0=1` → write fault.
- `p1=77af7000` → exact start of NETAPI32.DLL `.rdata`.

The expected COW resolve never happens. The fault is delivered to
user mode as an access violation. The loader swallows it (per NT's
design — `LdrpInitializeProcess` runs under a `__try/__except`) and
continues with an un-fixed-up IAT. Every imported symbol in NETAPI32
now dispatches through an unresolved slot. Eventually winlogon calls
one. `EIP=0`. Cascade. Crash.

Patching a victim DLL's section characteristics to add
`IMAGE_SCN_MEM_WRITE` (`0x80000000`) makes the fault disappear for
*that* DLL. The faulting address then shifts to the next pre-built
DLL whose IAT is in a read-only section. Whack-a-mole confirms the
diagnosis: the MM's COW path is never getting reached.

### Hypotheses for the root cause

Four candidates, ordered by what I'd check first:

**A. `NtMapViewOfSection` isn't setting the `CopyOnWrite` bit at all.**
   The PTE is plain read-only (Write=0, CopyOnWrite=0). The write
   fault falls through the COW branch in `MmAccessFault` and lands
   at `MMFAULT.C:458` which returns `STATUS_ACCESS_VIOLATION`.
   Instrument `MmAccessFault` to dump PTE contents on a write fault
   to a known IAT address range. If `CopyOnWrite` is 0, this is it.

**B. Prototype PTEs don't propagate `CopyOnWrite` when demand-paged.**
   Image section PTEs start as prototype PTEs. On first access
   `MiResolveProtoPteFault` materializes them into hardware PTEs.
   If the materialization drops the `CopyOnWrite` bit, the first
   read succeeds (readable), the next write faults, and the fault
   handler sees a plain read-only PTE.

**C. COW works for private (allocated) sections but not image ones.**
   Different PTE paths for `SEC_IMAGE` vs. `SEC_RESERVE`/`SEC_COMMIT`.
   Testable with a native-subsystem test that maps a file both ways
   and tries to write.

**D. CSRSS / subsystem interaction specific.**
   Works under `micront` profile (no csrss), breaks under `headless` /
   `gui`. Less likely but worth ruling out with the same test program
   under both profiles.

### Current workaround (ugly)

A pre-process patching step rewrites affected DLLs' section
characteristics to `IMAGE_SCN_MEM_WRITE` before staging them on disk:

```python
# Temporary workaround — NOT a fix
import pefile
pe = pefile.PE("NETAPI32.DLL")
for s in pe.sections:
    if s.Name.strip(b'\0') == b'.rdata':
        s.Characteristics |= 0x80000000  # IMAGE_SCN_MEM_WRITE
pe.write("NETAPI32.DLL")
```

This makes the MM map those pages `PAGE_READWRITE` from the start,
sidestepping the COW requirement entirely. Every process now gets a
private IAT at load time, which means no cross-process sharing of
the `.rdata` page — a small memory cost but it works. The real fix
is in the kernel's fault handler.

### Key source files for the fix

| File | Role |
|---|---|
| `MM/MMFAULT.C:452` | Write fault COW check on valid PTE |
| `MM/MMFAULT.C:458` | `ACCESS_VIOLATION` for write to read-only (non-COW) |
| `MM/WRTFAULT.C:25` | `MiCopyOnWrite` implementation |
| `MM/I386/PROBEWRT.C:101` | Probe-and-write COW check |
| `MM/MI.H:1042` | PTE `CopyOnWrite` bit definition |
| `MM/MMSUP.C` | `MmAccessFault` kernel-mode callers |
| `MM/PAGFAULT.C` | Demand-page / prototype PTE resolution |
| `MM/SECTN.C`, `MM/CREASECT.C` | Section creation / mapping |

### Affected pre-built binaries

| DLL | IAT section | Section flags | COW needed |
|---|---|---|---|
| NETAPI32.DLL | `.rdata` | `0x40000040` (R) | Yes |
| NETRAP.DLL | `.text` | `0x68000020` (RX) | Yes |
| WINSPOOL.DRV | `.rdata` | probably R | Probably |
| CRTDLL.DLL | unknown | unknown | Check |
| MSVCRT20.DLL | unknown | unknown | Check |

DLLs built from source in this tree are unaffected — LINK 2.50
emits a separate writable `.idata` section for the IAT.

### For Raymond

Hypothetical title: *"Why does my DLL's IAT contain garbage when the
program starts running?"* Answer: "Because Copy-on-Write has been
broken in the kernel you're running for as long as you've been
running it, and nobody noticed because every DLL you ever loaded
was linked by your own toolchain, which put the IAT in a writable
section."

---

## 3. Kernel SD-handling code dereferences user-derived pointers without guards

### The punchline

Several functions in `ntoskrnl!Se*` that operate on a `SECURITY_DESCRIPTOR`
treat fields like `Owner`, `Group`, `Sacl`, `Dacl` as guaranteed-non-NULL
and guaranteed-valid, and call helpers like `SeLengthSid()` /
`AclSize` reads on them without first checking. When a malformed or NULL
pointer slips through, the kernel takes a `c0000005` access violation in
ring-0 and bugchecks with `KMODE_EXCEPTION_NOT_HANDLED`.

In MicroNT, this fires today during the first interactive logon's
profile-load path: winlogon → `RestoreUserProfile` → `GiveUserDefaultProfile`
→ `MyRegLoadKey` (succeeds) → `SetupNewDefaultHive` →
`ApplySecurityToRegistryTree` → `RegSetKeySecurity` →
`NtSetSecurityObject` → kernel-side hits the unguarded deref and STOP'd
with the very informative

```
*** STOP: 0x0000001E (0xC0000005, 0x80168BFD, 0x00000000, 0x00000001)
KMODE_EXCEPTION_NOT_HANDLED
```

The faulting instruction `movzx eax, BYTE PTR [ecx+0x1]` with `ecx==0`
is a `SeLengthSid(NULL)` — reading `SID->SubAuthorityCount` at offset +1.

### Concrete instance found and patched

`SeAssignSecurity` in `src/NT/PRIVATE/NTOS/SE/SEASSIGN.C`, the self-relative
copy-out phase around line 727:

```c
// before fix:
RtlMoveMemory( Field, NewOwner, SeLengthSid(NewOwner) );  // bugcheck if NewOwner==NULL
((SECURITY_DESCRIPTOR *)(*NewDescriptor))->Owner = (PSID)RtlPointerToOffset(Base,Field);
Field += NewOwnerSize;

if (NewGroup != NULL) {                                    // Group is guarded — Owner isn't
    RtlMoveMemory( Field, NewGroup, SeLengthSid(NewGroup) );
}
((SECURITY_DESCRIPTOR *)(*NewDescriptor))->Group = (PSID)RtlPointerToOffset(Base,Field);
// ^ also a latent bug: Group offset set even when Group was NULL,
//   leaving it pointing past the buffer at uninitialised memory.
```

The Owner branch was patched to mirror the Group NULL-check (both now also
correctly write a NULL/zero-offset into the resulting self-relative SD
when the source was NULL).

### Why this is the bigger pattern, not a one-off

The kernel's contract is that any pointer crossing the user→kernel
boundary must be probed and captured before deref. `SeCaptureSecurityDescriptor`
does this rigorously — `ProbeForRead` everywhere, `try/except` around every
deref, NULL checks before each field access. But callers downstream that
receive an already-"captured" SD (`SeAssignSecurity`, the
self-relative-builder helpers) silently assume:

  - Owner is never NULL (per the documented contract of
    `SepGetDefaultsSubjectContext`)
  - Group is sometimes NULL (and is checked)
  - Sacl/Dacl AclSize fields are valid USHORTs (no validation)

Any of these assumptions can be violated by a malformed token (LSA build
bug, missing `DefaultOwnerIndex` setup, etc.) or by a user-mode caller
that crafts an `ExplicitDescriptor` with fields the upstream capture
didn't probe.

### Audit targets

Anything in `src/NT/PRIVATE/NTOS/SE/` that walks an SD's Owner/Group/Sacl/Dacl
without guards. Quick grep:

```
grep -rnE 'SeLengthSid\s*\(|->AclSize\b|RtlpOwnerAddrSecurityDescriptor|RtlpGroupAddrSecurityDescriptor' \
   src/NT/PRIVATE/NTOS/SE/
```

Each hit deserves: (a) a NULL guard if the field could be NULL, and
(b) ideally an SEH wrap if the pointer could be user-derived.

### Real-world implication

In a hostile environment this class of bug becomes a **local
denial-of-service / kernel panic vector** at minimum, and depending on
the exact deref pattern (e.g. arbitrary-offset reads driven by
attacker-controlled SubAuthorityCount or AclSize values) potentially an
**information leak or escalation** vector. Classic kernel-trust-boundary
mistake, exactly the shape that ate Microsoft for a decade of Patch
Tuesdays. We're getting away with it now because MicroNT runs as one
trusted user; a real multi-user system would have to take this seriously.

### For Raymond

Hypothetical title: *"Why does winlogon take down my entire OS just by
trying to give me a default profile?"* Answer: "Because the security
subsystem assumed the token would always have a non-NULL default owner,
and you're the first person in 30 years to feed it one that didn't."

---

## 4. NTFS log records can't disambiguate FRS within a cluster

### The punchline

NT 3.5's NTFS Log File Service (`LFS`) writes log records that locate
the on-disk target by **VCN + cluster count**.  With a 4 KB cluster and
the standard 1 KB File Record Segment, **four FRS share the same VCN**.
The log record has nowhere to record *which* FRS within the cluster it
applies to.  On replay (and on every in-memory update that goes through
the duplicated-info path) the LFS picks one — and it's not always the
right one.

The visible failure: directory creates succeed, the file is openable
by exact name, the parent's `$INDEX_ROOT` / `$INDEX_ALLOCATION` carry
the entry — but `NtQueryDirectoryFile` walks the index and silently
skips it.  Five `CreateFile`s in a row go in fine; 1, 2, 4 enumerate;
3, 5 are invisible.  This is one of those bugs where every primitive
test ("can I open it by name?", "is it in the index?") passes
individually and only the composite operation fails.

### The setup

`NtfsCommonCleanup` (NCC) in `CLEANUP.C` calls `NtfsUpdateDuplicateInfo`
(NUDI) to re-stamp the directory entry's duplicated `$STANDARD_INFORMATION`
and `$FILE_NAME` fields.  NUDI in turn issues an LFS log record so the
update can be replayed after a crash.  The log-record header in NT 3.5:

```c
// NTFSLOG.H (pre-fix)
typedef struct _NTFS_LOG_RECORD_HEADER {
    LSN              ThisLsn;
    USHORT           RedoOperation;
    USHORT           UndoOperation;
    USHORT           RedoOffset;
    USHORT           RedoLength;
    ...
    LCN              TargetVcn;
    ULONG            ClusterCount;
    USHORT           Reserved[2];      // ← 4 unused bytes
    USHORT           AttributeOffset;
    ...
} NTFS_LOG_RECORD_HEADER, *PNTFS_LOG_RECORD_HEADER;
```

`TargetVcn + ClusterCount` is enough to identify a target *if* one
cluster maps to one logical thing.  When `BytesPerCluster >
BytesPerFileRecordSegment`, that invariant breaks.  NT 3.5's design
quietly assumed cluster ≥ FRS; the format-time defaults satisfy it; the
LFS schema therefore has no field to record sub-cluster offset.

### Why MicroNT trips it

We format with the canonical NTFS 1.x defaults: `1024 B/sector × 4
sectors/cluster = 4 KB cluster`, `BytesPerFileRecordSegment = 1024 B`.
Four FRS per cluster.  Any cleanup of FRS *N* for *N ∈ {1, 2, 3}*
within a cluster issues a log record whose `TargetVcn` is the cluster
holding all four — the LFS replay path picks FRS 0 by default and
patches the wrong record.  After enough creates, the directory's
$INDEX_ALLOCATION accumulates a divergence between what was written
and what the duplicated-info path subsequently re-stamps; entries
that *survive* the divergence are visible, the ones that don't are
ghosts.

### What NT 4.0 did

Changed the LFS record schema to **byte-form**: `StreamOffset`
(LONGLONG) + `StructureSize` (ULONG) replace `TargetVcn + ClusterCount`,
and the previously-reserved 4 bytes become a `ClusterBlockOffset` to
record the FRS-within-cluster index for the sub-cluster case:

```c
// NTFSLOG.H (post-fix, byte-form)
typedef struct _NTFS_LOG_RECORD_HEADER {
    ...
    LONGLONG         TargetStreamOffset;   // was VCN
    ULONG            StructureSize;        // was ClusterCount
    USHORT           ClusterBlockOffset;   // was Reserved[0]
    USHORT           Reserved;             // was Reserved[1]
    ...
} NTFS_LOG_RECORD_HEADER, *PNTFS_LOG_RECORD_HEADER;
```

Same wire bytes (the field widening reuses the previously-reserved
slots).  The signature change of `NtfsWriteLog` ripples through ~43
callers — every `LfsWriteLogRecord` site picks up the new
sub-cluster-aware locator.

### The fix in MicroNT

NT 4.0's byte-form was backported wholesale.  The diff:

- `NTFSLOG.H` — `TargetVcn` → `TargetStreamOffset`, `ClusterCount` →
  `StructureSize`, `Reserved[0]` → `ClusterBlockOffset`.
- `LOGSUP.C` — `NtfsWriteLog` signature flipped from `(VCN Vcn, ULONG
  ClusterCount)` to `(LONGLONG StreamOffset, ULONG StructureSize)`;
  internal derivation of `LogClusterCount` / `LogVcn` /
  `ClusterBlockOffset` from byte form for compatibility with
  cluster-shaped LFS internals.
- ~43 caller sites converted from `Vcn / ClusterCount` to
  `StreamOffset / StructureSize`.

### Detection

Without instrumentation, the symptom presents as "every other file in
this directory is invisible to FindFirstFile but openable by exact name."
With instrumentation, the cleanest probe is to dump every
`NtfsWriteLog` call's `(Vcn, ClusterCount)` and grep for cases where
`ClusterCount == 1` *and* `BytesPerCluster > BytesPerFileRecordSegment`
— every such call is a sub-cluster log record, and on cluster-form
NT 3.5 every one is a candidate ghost.

### For Raymond

Hypothetical title: *"Why does my brand-new file exist when I open it by
name, but disappear when I list the folder it lives in?"*  Answer:
"Because NTFS's transaction log writes target locators in clusters, and
your cluster has more than one file record in it.  The log replays the
wrong one.  Pick a cluster size that exactly matches a file record and
your problem goes away.  Or upgrade to NT 4.0."
