# SEH chain corruption — resolved

Kernel-mode SEH chain corruption that bugchecked `0xCAFE5E1F`
during NTFS `t.raises` selftest cycles.  Tracked here from open
investigation through to the underlying `_KiKernelDispatch` fix.

**Status:** fixed in `3f4a3d0` — *"ke: synthetic trap frame in
`_KiKernelDispatch` for `NtContinue`/`NtRaiseException`."*

## Root cause

`KE/I386/sysstubs.asm:_KiKernelDispatch` (the kernel-mode `Zw*`
fast path that replaced INT 2E in MicroNT) didn't build a real
`KTRAP_FRAME`.  It did its own C-style `push ebp; mov ebp,esp`
prolog and called the service routine.

`_NtContinue` (`TRAP.ASM`) and `_NtRaiseException` both treat
`[ebp+0]` (their own saved-ebp slot) as a `KTRAP_FRAME *`.  Down
that path:

1. `KiContinue` writes context fields into the bogus "trap frame"
   — actually `_KiKernelDispatch`'s saved-ebp area on the stack.
2. `EXIT_ALL` (`KIMACRO.INC`) reads `[esp]+TsExceptionList`
   (offset `0x4C`) and writes it to `PCR.NtTib.ExceptionList`.
   That field was never populated by `KeContextToKframes`
   (`CONTEXT` has no `ExceptionList` member), so the value is
   whatever stale stack bytes happen to live there — typically a
   leftover `fs:[0]` pointer from a long-dead frame.
3. `fs:[0]` is now pointing into dead stack.  The next kernel-mode
   exception walks the rotten chain, lands on a frame whose
   `Handler` field reads back as `0x00000246` (an EFLAGS-shaped
   value), and the dispatcher would `call ecx` against pool data.

The dense reproducer was NCC's `try { NtfsUpdateDuplicateInfo() }
except` body in `CLEANUP.C` — the first kernel-mode try/except
whose body raised reliably under selftest.  Fastfat had the same
latent exposure but didn't exercise it densely.

## The fix

`_KiKernelDispatch` now builds a synthetic `KTRAP_FRAME` for
service numbers it knows take a `KTRAP_FRAME *` (`NtContinue`,
`NtRaiseException`), with `TsExceptionList = current fs:[0]`,
`TsPreviousPreviousMode`, and `TsSegCs = R0_CODE` so `EXIT_ALL`
does an intra-priv IRET.  ~20 lines of asm in `sysstubs.asm`;
impact limited to kernel-mode `Zw*` callers.

After the fix:

* The previous NTFS-side fs:[0] save/restore workaround in
  `NtfsCommonCleanup` (`NTFS_NCC_FS0_WORKAROUND`) was removed —
  `fs:[0]` now stays consistent across kernel `Zw*` round trips.
* `pkg/test/fs.lua`'s `t.raises` cycles run cleanly; the in-OS
  rebuild progresses past the SEH-heavy NTFS path.

## Diagnostic helper — `KI_SEH_VALIDATE_CHAIN`

A "super extra SEH debugging helper" stays compiled into the tree
behind a macro, **default OFF**, ready to flip on when (if) the
chain looks corrupt again.  When enabled it gives you the
bugcheck-with-context shape that originally led to the
`_KiKernelDispatch` discovery, instead of a generic #UD inside
`RtlpExecuteHandlerForException`.

### What it does

* `KE/I386/EXCEPTN.C` — `KiValidateExceptionChain` walks
  `PCR.NtTib.ExceptionList` on every kernel-mode exception
  (first chance), and trips `0xCAFE5E1F` on:

  | `arg2` | meaning |
  |--------|---------|
  | 1 | chain depth > 64 (`KI_SEH_MAX_DEPTH`) |
  | 2 | frame outside thread stack range |
  | 3 | `Handler` not in any loaded module |
  | 4 | `Handler >= 0xFB000000` (pool-resident) |

  It also dumps (via `DbgPrint` to KD/serial) the bad frame's
  EH3 layout, the 8 dwords either side of it on the stack, and a
  full chain walk with each `Handler` resolved against
  `PsLoadedModuleList`.

* `RTL/I386/EXDSPTCH.C` — pre-dispatch guard.  Same `0xFB000000`
  bound check before `RtlpExecuteHandlerForException` is allowed
  to `call` the handler.  Trips `0xCAFE5E1F` with `arg2 = 5`.

### How to enable

Set `KI_SEH_VALIDATE_CHAIN` to 1 — either edit the `#define` near
the top of `EXCEPTN.C` (and the matching one in `EXDSPTCH.C`), or
pass `-DKI_SEH_VALIDATE_CHAIN=1` to the kernel build.  Both halves
read the same macro name; you can flip them independently if you
only want one side.

### Cost

* OFF (default): zero — both sites compile out.
* ON: one chain walk + per-frame module-list lookup per
  kernel-mode exception.  Negligible on normal workloads,
  measurable on SEH-heavy paths (NTFS, etc.).

### When it'd be useful again

* Any future regression that resembles "exception dispatcher
  jumps to garbage / random #UD".
* When importing more NT subsystems with their own try/except
  patterns and you're not confident the trap-frame plumbing
  handles every kernel-mode `Zw*` path.
* Suspected kernel stack corruption from drivers — frame 2/3
  hits will name the corrupting frame's owner directly via the
  `DbgPrint` dump.

## Quick re-investigation recipe

If the bugcheck returns:

1. Flip `KI_SEH_VALIDATE_CHAIN` to 1 in both `EXCEPTN.C` and
   `EXDSPTCH.C`, rebuild kernel.
2. Reproduce.  Capture serial output — the
   `*** SEH chain corruption ***` block names the bad frame and
   walks the chain.
3. Map the bad `Handler` value to a function via `.dwf` files
   (gdb `add-symbol-file`).  Walk back through the chain to find
   the frame whose owner left a stranded `Next` or `Handler`.
4. Cross-check against the root-cause section above: any
   kernel-mode `Zw*` whose service routine takes a
   `KTRAP_FRAME *` is in the same risk class as the original
   `_KiKernelDispatch` bug.

## Related

* Project memory: `project_seh_global_unwind2_bug` — full
  forensic trail of the original investigation, suspect list,
  and how `_KiKernelDispatch` was identified.
* Fix commit: `3f4a3d0` (`ke: synthetic trap frame in
  _KiKernelDispatch for NtContinue/NtRaiseException`).
* The NCC workaround removal (`NTFS_NCC_FS0_WORKAROUND` /
  `_saved_fs0` blocks in `CLEANUP.C`) lands separately as part
  of the same cleanup cycle.
