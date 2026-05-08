# SEH chain corruption around NTFS try/except — open

Latent kernel-mode SEH bug.  Currently masked (does not reproduce
after recent NTFS cleanup) but unconfirmed-fixed.  Re-verify any
time stack layout in NCC's call tree changes.

## Symptom

Bugcheck `0xCAFE5E1F` (`KI_SEH_GUARD_BUGCHECK`) raised by
`KiValidateExceptionChain` (`src/NT/PRIVATE/NTOS/KE/I386/EXCEPTN.C`)
during exception dispatch.  Args:

    arg1 = bad EH3 Frame address (somewhere on the kernel stack)
    arg2 = guard reason
            1 = chain too deep (> KI_SEH_MAX_DEPTH = 64)
            2 = frame outside thread stack range
            3 = Handler not in any loaded module
            4 = Handler points into pool (>= 0xFB000000)
    arg3 = the bad field value (Handler value, or off-stack ptr)
    arg4 = original ExceptionCode being dispatched

Originally observed: arg2=3 with Handler = `0x00000246` (an
EFLAGS-looking value left on dead stack memory).  Indicates fs:[0]
is pointing into stack that was once a real frame but has since
been reused.

## When it fires

After several `t.raises` calls in `pkg/test/fs.lua` /
`pkg/test/os.lua`.  Cumulative damage — single exceptions don't
trip it; the third or fourth raised-and-caught exception finds the
chain already corrupt.

## Mechanism (hypothesised)

`NtfsCommonCleanup` (NCC) wraps `NtfsUpdateDuplicateInfo` (NUDI)
in an inner `try { } except { }`.  After the inner unwind,
`fs:[0]` does NOT equal NCC's own EH3 frame as it should.  When
NCC eventually returns, the compiler-emitted `__SEH_epilog` pops
what it thinks is NCC's frame off `fs:[0]`, but actually pops a
stranded inner frame.  `fs:[0]` is left pointing into dead stack.
Next exception walks the rotten chain → bugcheck.

By the math, this should not happen.  C8 `_except_handler2` calls
`_global_unwind2(stop=NCC_EH3)` → `RtlUnwind(TargetFrame=NCC_EH3,
TargetIp=_gu_return)`.  `RtlUnwind` walks `fs:[0]` popping frames
via `RtlpUnlinkHandler` (`XCPTMISC.ASM:365`) until current ==
target, at which point it `ZwContinue`s without popping target.
`fs:[0]` should equal `NCC_EH3` at that moment.  Empirically it
doesn't.

### Three suspects (one of these)

1. **`libcntpr.lib`'s `_global_unwind2` differs from NT 3.5 source.**
   Our build links a pre-built binary `LIBCNTPR.LIB` for the kernel
   CRT (see `MAKEFILE.DEF:1454`); we don't have the asm source in
   the active tree.  NT 3.5 source for the function is in
   `stuff/.../PRIVATE/CRT32/MISC/I386/EXSUP.ASM:119` and is six
   instructions long; the binary may differ.

2. **`RtlUnwind` ordering bug.**
   `EXDSPTCH.C:495-502` reads `Frame->Next` BEFORE running the
   frame's handler via `RtlpExecuteHandlerForUnwind`.  If a handler
   itself raises-and-catches (modifying `fs:[0]`), the cached
   `Next` we then unlink against is stale.

3. **Compiler-emitted `specific_handler` thunk doesn't restore
   ESP / `fs:[0]` when JMPing back to post-`__try` code.**
   Standard MSC C8 thunks do `lea esp, [ebp - locals]`; ours might
   not, and might leave fs:[0] pointing at the spot the thunk
   itself pushed.

## Workaround

Manual `fs:[0]` save/restore around NCC's inner try/except.  Lives
in `src/NT/PRIVATE/NTOS/NTFS/CLEANUP.C`:

    #ifndef NTFS_NCC_FS0_WORKAROUND
    #define NTFS_NCC_FS0_WORKAROUND 0  /* TEMP: off so bug, when it
                                          returns, fires a clean
                                          KiSehDumpCorruption rather
                                          than silently corrupting */
    #endif

    #if NTFS_NCC_FS0_WORKAROUND
        ULONG _saved_fs0;
        __asm { mov eax, dword ptr fs:[0]
                mov _saved_fs0, eax }
    #endif
    ...
    #if NTFS_NCC_FS0_WORKAROUND
        __asm { mov eax, _saved_fs0
                mov dword ptr fs:[0], eax }
    #endif

Default is currently **0** (workaround OFF, canary mode) — the
bug doesn't reproduce on selftest after recent cleanup, so we keep
it unmasked.  If the underlying corruption returns we'll see a
clean bugcheck with full dump rather than a quiet wrong-state
mask.  Flip to `1` (or build with `-DNTFS_NCC_FS0_WORKAROUND=1`)
if the bug returns aggressively and you need a quick patch while
investigating.

## Diagnostic dump

`KiValidateExceptionChain` (`KE/I386/EXCEPTN.C`) calls
`KiSehDumpCorruption` before bugchecking.  Dumps to KD/serial:

  - bad Frame's EH3 layout (Next, Handler, scopetable, trylevel,
    saved \_ebp)
  - 8 stack DWORDs before and after the bad Frame
  - the full SEH chain walked from `fs:[0]`, with each Handler's
    loaded-module match (DllBase + offset, suitable for resolution
    against `.dwf` files)

Read the serial log starting at `*** SEH chain corruption:`
through `*** end SEH dump`.

### Disambiguation rubric

  - `stack-before:` shows return addresses pointing into
    `ntoskrnl.exe` near `_global_unwind2` / `RtlUnwind` — suspect
    #1 or #2.  The `_gu_return` label is at a known offset; resolve
    against `ntoskrnl.dwf`.
  - `stack-before:` shows what looks like NUDI's saved-register
    state (a frame-pointer that walks back into
    `NtfsUpdateDuplicateInfo` body) — suspect #3.  Pop fs:[0] is
    NUDI's old EH3 frame on dead stack.
  - The chain walk shows a frame whose `Handler` is in the
    expected module (`ntfs.sys` / `ntoskrnl.exe`) but whose
    `Next` points off-stack — confirms it's been popped by the
    compiler thunk somewhere we didn't expect.

## Current state (2026-05-08)

- Bug does NOT reproduce on selftest after the NT 4.0 byte-form
  `NtfsWriteLog` backport + diagnostic-guard cleanup (commit
  14ccee8) plus the kernel-side dump enhancement.
- Workaround is gated, currently default OFF (canary mode — see
  Workaround section).  Tests pass with workaround OFF in the
  current selftest run, suggesting either:
    a) recent changes accidentally fixed the underlying bug, or
    b) stack layout shifted enough to mask it (FS0_CHECK locals
       and asm guard ebp-walks both removed → smaller frames),
       and it'll come back when something pushes more stack into
       NCC's call tree.
- Re-verify any time we touch NCC's inner try/except, NUDI, the
  byte-form WriteLog path, or anything else that adjusts NTFS's
  stack-frame footprint.

## Why this likely lives outside NTFS

NT4's `CLEANUP.C` has no such workaround.  NT4's NTFS is the same
algorithm; the differences from NT 3.5 are in callees and in the
log-record byte form (already backported).  The bug therefore lives
in:

  - `LIBCNTPR.LIB` (pre-built kernel CRT) — supplies
    `_global_unwind2`, `_except_handler2`, `_local_unwind2` to
    `ntoskrnl.exe`; re-exported to drivers via
    `INIT/I386DEF.SRC:73-75`.
  - or `EXDSPTCH.C` `RtlUnwind` (we have source).
  - or the C8.50 compiler's `specific_handler` thunk emitted into
    each function with `__try`/`__except` (binary-only; we have no
    source).

## Build chain context (for whoever rebuilds libcntpr)

`libcntpr.lib` is itself imported as a binary in our tree.  Its
NT 3.5 source build (per `stuff/.../PRIVATE/CRTLIB/MAKEFILE`):

    libcntpr.lib = libcnt.lib + trannt.lib

  - `libcnt.lib` from `PRIVATE/CRT32NT/{MISC,STARTUP,HELPER,LOWIO,
    STDIO,STRING,TIME,DLLSTUFF}/`
  - `trannt.lib` from `PRIVATE/fp32nt/`

The relevant SEH primitives all live in `CRT32NT/MISC/i386/`:

  - `exsup.asm`   — `__except_list` symbol (= 0)
  - `exsup2.asm`  — `_except_handler2`, `_global_unwind2` (C8)
  - `exsup3.asm`  — `_except_handler3` (C9, our compiler doesn't
    emit this)
  - `sehsupp.c`   — C-side helpers

To investigate suspect #1 we'd need to either (a) extract the
`_global_unwind2` member from the binary `LIBCNTPR.LIB` and disasm
it, comparing to NT 3.5 source, or (b) bring `CRT32NT/` and
`fp32nt/` into the active build tree, build our own
`libcntpr.lib`, and link `ntoskrnl.exe` against it.  Option (b) is
substantial but yields source-debuggable kernel SEH.

## Quick-start when the bugcheck returns

1. Boot to selftest, capture serial output.
2. Confirm `*** SEH chain corruption:` block appears.
3. Disambiguate via the rubric above.
4. If suspect #1 or #2: extract / read source as appropriate.  If
   #3: build a tiny test function with `__try { __try { raise } __except (1) {} } __except (1) {}`, dump fs:[0] before/after each except, see if the inner-thunk's epilog leaves
   fs:[0] correct.

## Related

- `feedback memory project_seh_global_unwind2_bug` — concise
  version of this writeup for future-session context.
- `pack(2)` leak (top of `TODO.md`) — different bug, separate
  fix in `NTFSPROC.H:61`.  Not the current SEH issue but the
  mechanism (stack overrun via struct-size mismatch) was once a
  candidate.
