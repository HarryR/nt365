# Memory Manager: Copy-on-Write broken for image sections

## Summary

The kernel's memory manager does not correctly handle copy-on-write
(COW) faults for pages in mapped image sections. When the NT loader
(ntdll `LdrpInitializeProcess`) writes import address fixups or
relocation entries into read-only pages of a DLL, the page fault
should trigger COW: allocate a private page, copy the data, remap
the PTE as writable, and resume. Instead the fault is delivered to
user mode as STATUS_ACCESS_VIOLATION (c0000005), leaving the DLL's
import table unresolved.

## Impact

Any PE DLL whose Import Address Table (IAT) resides in a read-only
section (.rdata, .text, .edata) will have broken imports. Functions
imported by that DLL resolve to stale addresses from the original
link, causing null-pointer or wild-pointer calls at runtime.

**DLLs built from source are unaffected** — our LINK 2.50 emits a
separate writable `.idata` section for the IAT, so the loader never
needs to write to a read-only page.

**Pre-built DLLs are affected** — binaries from the VC++ 2.2
distribution (NETAPI32.DLL, NETRAP.DLL, WINSPOOL.DRV, CRTDLL.DLL,
etc.) typically merge the IAT into `.rdata` or even `.text`, which
are mapped read-only. These DLLs load but their imports are not
fixed up, causing crashes when any imported function is called.

## Observed symptoms

```
CSRSRV: CsrSbCreateSession — Process=0025A360
UMODE EXC(1st): code=c0000005 addr=6010554b p0=00000001 p1=77af7000
```

- `p0=1` → write fault
- `p1=77af7000` → start of NETAPI32.DLL `.rdata` (IAT)
- `p1=77b31000` → start of NETRAP.DLL `.text` (IAT merged into code)

Later, winlogon crashes at `eip=00000000` when calling an imported
function through the unresolved IAT.

## Diagnosis method

Patching the affected DLL sections to add IMAGE_SCN_MEM_WRITE
(`0x80000000`) eliminates the page fault for that DLL — confirming
the fault is a COW failure, not a loader bug. The faulting address
then shifts to the next DLL with a read-only IAT.

```python
# Temporary workaround — NOT a fix
import pefile
pe = pefile.PE("NETAPI32.DLL")
for s in pe.sections:
    if s.Name.strip(b'\0') == b'.rdata':
        s.Characteristics |= 0x80000000  # IMAGE_SCN_MEM_WRITE
pe.write("NETAPI32.DLL")
```

## Root cause location

The page fault handler is in `NT/PRIVATE/NTOS/MM/`. Key files:

- `I386/PROBEWRT.C` — `MmCopyOnWriteCheck` / probe-and-write logic
- `MMFAULT.C` / `ACCESCHK.C` — access fault dispatch
- `MI.H:138` — "if copy-on-write" PTE check (`MM_PTE_WRITECOPY 0x200`)
- `WSMANAGE.C`, `PAGFAULT.C` — working set / demand-page handlers

The expected flow for a COW fault:

1. User writes to a page mapped from an image section
2. Page fault fires (PTE is read-only or COW-marked)
3. MM checks PTE — sees COW bit or image-backed page
4. Allocates a new physical page, copies contents
5. Updates PTE to point to new page with write access
6. Returns to user mode — write succeeds transparently

The failure: step 3–5 are not happening for user-mode image faults.
The fault falls through to the access-violation path and is delivered
as an unhandled exception to user mode.

## How to verify the fix

After fixing the MM COW handler:

1. Remove the `.rdata` write-flag workaround from pre-built DLLs
2. Boot with unmodified NETAPI32.DLL / NETRAP.DLL / WINSPOOL.DRV
3. No `c0000005` write faults to DLL address ranges should appear
4. winlogon should proceed past dialog creation without `eip=0` crash
5. `!process 0 0` in kernel debugger should show DLLs with private
   (COW'd) pages for their .rdata/.text sections

## Testing plan

### Hypothesis A: MM COW fundamentally broken

The kernel's page fault handler never invokes `MiCopyOnWrite` because
the PTEs for image section pages are not set with the CopyOnWrite bit.
This would affect all profiles (micront, headless, gui).

**Test:** Write a minimal native-subsystem test program (no csrss
dependency) that:

1. Creates a section from a file (`NtCreateSection` with
   `SEC_IMAGE`)
2. Maps it with `PAGE_WRITECOPY` (`NtMapViewOfSection`)
3. Reads a page (should succeed)
4. Writes to that page (should trigger COW)
5. Verifies the write succeeded and the original file is unchanged

Run under the `micront` profile (bare kernel, no Win32 subsystem).
If the write faults with STATUS_ACCESS_VIOLATION, COW is
fundamentally broken in the MM.

### Hypothesis B: Section mapping doesn't set COW bit

`NtMapViewOfSection` for image sections may set PTEs as plain
read-only (Write=0, CopyOnWrite=0) instead of COW-protected
(Write=0, CopyOnWrite=1). The fault handler code at MMFAULT.C:452
checks `TempPte.u.Hard.CopyOnWrite != 0` — if this bit is never
set, the fault falls through to line 458 which returns
STATUS_ACCESS_VIOLATION.

**Test:** Add a DbgPrint in `MmAccessFault` at line 444
(`if (StoreInstruction)`) to dump the PTE contents on a write fault
to the NETAPI32 IAT address range. Check whether the CopyOnWrite
bit is set.

### Hypothesis C: csrss/QLPC interaction specific

The COW mechanism works for kernel-mode and simple user-mode
processes but breaks when the Win32 subsystem (csrss) is involved —
e.g., shared section mappings or desktop heap interactions corrupt
PTE state.

**Test:** Compare the micront-profile test (Hypothesis A) against
the same test running under the headless profile (with csrss). If
COW works under micront but fails under headless, the issue is in
the csrss interaction.

### Hypothesis D: Image section prototype PTEs

The loader uses `NtMapViewOfSection` which creates prototype PTEs
for the image section. When demand-paging resolves these, the
resulting hardware PTE may not carry the CopyOnWrite bit from the
prototype PTE. This would be a bug in `MiResolveProtoPteFault` or
`MiCompleteProtoPteFault`.

**Test:** Trace through `MiDispatchFault` → `MiResolveProtoPteFault`
for a fault on a NETAPI32 .rdata page. Check that the prototype PTE
has COW protection and that the resulting hardware PTE preserves it.

## Key source files for the fix

| File | Role |
|------|------|
| `MM/MMFAULT.C:452` | Write fault COW check on valid PTE |
| `MM/WRTFAULT.C:25` | `MiCopyOnWrite` implementation |
| `MM/MMSUP.C` | `MmAccessFault` kernel-mode callers |
| `MM/I386/PROBEWRT.C:101` | Probe-and-write COW check |
| `MM/MI.H:1042` | PTE CopyOnWrite bit definition |
| `MM/MMFAULT.C:458` | ACCESS_VIOLATION for write to read-only (non-COW) |
| `MM/PAGFAULT.C` | Demand-page / prototype PTE resolution |
| `MM/SECTN.C` / `CREASECT.C` | Section creation / mapping |

## Affected pre-built binaries

| DLL | IAT section | Section flags | Needs COW |
|-----|------------|---------------|-----------|
| NETAPI32.DLL | .rdata | 0x40000040 (R) | Yes |
| NETRAP.DLL | .text | 0x68000020 (RX) | Yes |
| WINSPOOL.DRV | .rdata | likely R | Probably |
| CRTDLL.DLL | unknown | unknown | Check |
| MSVCRT20.DLL | unknown | unknown | Check |
