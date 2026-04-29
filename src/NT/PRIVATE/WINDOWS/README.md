# `WINDOWS/` — kernel32.dll port from NT 3.5 source

Lifted from `stuff/windows_nt_3_5_source_code/.../NT-782/PRIVATE/WINDOWS/`,
patched for MicroNT. Builds the only Win32 lib MicroNT ships:
`kernel32.dll` (no user32 / gdi32 / advapi32 / etc. planned). Goal is
running the original MS toolchain (`CL`, `LINK`, `RC`, `MC`) end-to-end
on top of MicroNT for self-hosting.

The port keeps the original source as close to verbatim as possible.
Where surgery was unavoidable the diff is annotated with `MicroNT:`
comment blocks so a future contributor can find every change with
`git grep "MicroNT:"`.

---

## Tree layout

```
WINDOWS/
├── INC/         shared headers (lifted as-is + patched private headers from nt35_patches)
├── BASE/
│   ├── INC/     base.h, basemsg.h, basertl.h, basevdm.h
│   ├── RTL/     baselib.lib — atom + handle tables (lifted unmodified)
│   └── CLIENT/  kernel32.dll source (surgery target)
├── WINNLS/      nlslib.lib — codepage / locale tables (lifted unmodified)
│   └── DATA/    .nls binary tables (canonical NT 3.5 location for the data)
└── NLSMSG/      WINERROR.MC — message catalog source
```

Trimmed siblings (per "import only what you build"):

- `BASE/CLIENT/{ALPHA,MIPS,DAYTONA}/` — non-i386 arch dirs, no NT 3.51
  flavor split.
- `WINNLS/{NLSTRANS,TEST}/` — translator + tests, not part of nlslib build.
- `BASE/CLIENT/{B*,T*,MTBNCH,PPERF,WMBNCH,KILLER,XX,DH,COUNT,GLOBCS,MFMT,UNBUFW,WINPERF}*.C`
  — 33 standalone benchmark / test programs, not in `SOURCES`.

---

## Build target

`src/build.sh windows_base_client` runs the chain:

1. `windows_base_rtl` → `BASE/obj/i386/baselib.lib`
2. `windows_winnls`   → `WINDOWS/obj/i386/nlslib.lib`
3. `windows_base_client` → `PUBLIC/SDK/LIB/i386/{kernel32.lib, kernel32.dll}`

Each leaf has `MAKEFILE` + `SOURCES`; CLIENT additionally has
`MAKEFILE.INC` (resource-compile + winerror.rc copy step). Linked
against `ntdll.lib` only — no conlib, no csrcli, no basesrv.

Result: ~562 KB kernel32.dll, ~1230 exports, single import (ntdll.dll).

---

## Layout adjustments (DAYTONA → flat)

Original NT 3.5 ran the kernel32 build from `BASE/CLIENT/DAYTONA/` — a
per-flavor leaf that `!include`d the parent `SOURCES.INC` and added a
DAYTONA-specific source. We don't keep DAYTONA; the build runs from
`BASE/CLIENT/` directly. Path levels shifted one up everywhere:

| Reference | Original (from DAYTONA) | MicroNT (from CLIENT) |
|---|---|---|
| Shared INC | `..\..\inc` | `..\inc` |
| Base INC   | `..\..\..\inc` (was BASE/INC?) — n/a | `..\..\inc` |
| baselib.lib | `..\..\obj\*\baselib.lib` | `..\obj\*\baselib.lib` |
| nlslib.lib | `..\..\..\obj\*\nlslib.lib` | `..\..\obj\*\nlslib.lib` |
| nlsmsg dir | `..\..\..\nlsmsg` | `..\..\nlsmsg` |
| winnls.rc  | `..\..\..\winnls\winnls.rc` | `..\..\winnls\winnls.rc` |
| DLLORDER   | `..\kernel32.prf` | `kernel32.prf` |
| conlib     | `..\..\..\obj\*\conlib.lib` | **dropped — WINCON not lifted** |

`SOURCES.INC` carries a one-line shim `SOURCES` next to it (`!include
sources.inc`) so the leaf-level layout matches what `makefile.def`
expects, while keeping the original `.INC` file unchanged in spirit.

---

## csrss-free surgery

MicroNT has no csrss / basesrv / consrv. Every csrss-bound code path
in lifted source is handled by one of three strategies:

### Strategy 1 — Surgical gut (BASEINIT.C)

`BaseDllInitialize` originally connected to BASESRV via
`CsrClientConnectToServer`, registered the initial thread via
`CsrNewThread`, called `ConDllInitialize` (conlib) for console binding,
and read OS metadata (Windows directory, version, named-object
directory path) from `BaseStaticServerData` (basesrv-published).

All four calls deleted. Replaced with:

- `BaseWindowsDirectory       = L"\SystemRoot"` (kernel-installed symlink)
- `BaseWindowsSystemDirectory = L"\SystemRoot\System32"`
- `BaseWindowsMajorVersion / MinorVersion / BuildNumber / CSDVersion`
  pulled from `<ntverp.h>` macros (`VER_PRODUCTMAJORVERSION` etc.,
  written by `tools/stamp-version.py` via `libversion.py` — see
  the version note below).
- `BaseGetNamedObjectDirectory()` uses the literal `\BaseNamedObjects`
  instead of the basesrv-published copy. The directory itself lives
  in the NT object namespace, not csrss; smss / our boot publisher
  creates it the same way it does `\NLS`.

`IconHackORama` (loads user32 for icon-bearing console apps) and
`QuickThreadCreateRoutine` (csrss-internal CreateThread fast path)
deleted entirely — both depend on infrastructure we don't have.

### Strategy 2 — Fail-loud / route-around per file

`BEEP.C:NotifySoundSentry` → silent no-op. The Beep device works
direct via `\Device\Beep` `NtDeviceIoControlFile`; the sentry
notification was a basesrv LPC for the accessibility "flash screen
when a beep would be inaudible" feature. No accessibility path in
MicroNT, no-op is fine.

`DOSDEV.C:DefineDosDeviceW` → `SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
return FALSE;`. Mutating `\??\` symlinks needs a privileged daemon
which we don't have.

`PATHMISC.C:GetTempFileNameW` → drops the `BasepGetTempFile` LPC and
uses a process-local `InterlockedIncrement` counter seeded from
`GetTickCount`. Fine for single-process toolchain runs; cross-process
uniqueness is moot until we have parallel CL invocations.

`SUPPORT.C:BaseCreateStack` → drops the `BaseStaticServerData->SysInfo`
read; uses literal `4096` (i386 page size, exposed nowhere in the
public SDK headers).

### Strategy 3 — conlib replacement (STDIO.C, new)

`WINCON/CLIENT/` not lifted. `STDIO.C` provides every kernel32 export
that conlib used to satisfy. Three policy tiers:

- **Tier A — PEB-backed real impls.** `WriteConsoleA/W`, `ReadConsoleA/W`
  forward to `WriteFile`/`ReadFile` against the PEB std handle (which
  is a pipe or file in MicroNT — never a real console).
  `GetConsoleCP / GetConsoleOutputCP` return `GetACP() / GetOEMCP()`.
  `ConDllInitialize` is a no-op `return TRUE` (called from BASEINIT
  in stock NT, removed from our BASEINIT, kept exported for
  binary compat in case something does `GetProcAddress`).
- **Tier B — fail-loud.** ~85 console exports that toolchain doesn't
  call but kernel32.def lists. Each returns `FALSE`/`0`/`INVALID_HANDLE_VALUE`
  with `SetLastError(ERROR_INVALID_HANDLE)`. A non-toolchain consumer
  GetProcAddress'ing them gets a non-NULL pointer that fails predictably.
- **Tier C — silent OK.** `SetConsoleCtrlHandler` returns TRUE
  without storing the handler. Common Ctrl+C registration; failing
  it would break well-behaved EXEs.

`Get/SetStdHandle` stay where they were in the original — `FILEHOPS.C`
already had them PEB-backed (no csrss involvement). `STDIO.C` does
**not** redefine them.

`BackupRead / BackupSeek / BackupWrite` (KERNEL32.SRC exports, original
`BACKUP.C` dropped from our build) also fail-loud-stubbed in `STDIO.C`.

### Strategy 4 — Csr* internal stubs (CSRSTUB.C, new)

Eight Csr* primitives (`CsrClientCallServer`, `CsrAllocate*Buffer`,
`CsrFreeCaptureBuffer`, `CsrAllocateMessagePointer`,
`CsrCaptureMessageString`, `CsrNewThread`, `CsrIdentifyAlertableThread`,
`CsrSetPriorityClass`) are referenced by lifted source (`PROCESS.C`,
`THREAD.C`, `DLLATOM.C`, `DEBUG.C`, `VDM.C`, `SUPPORT.C` and nlslib's
`SECTION.C` / `TABLES.C`). Stock NT 3.5 ships them inside ntdll.dll
via `NTOS/DLL/CSR{INIT,UTIL,TASK,QUICK}.C`. We didn't lift those —
csrss-bound, no value without csrss.

Architectural note: **Csr\* never belonged in ntdll** anyway. ntdll
is kernel-boundary plumbing (syscall thunks + Rtl* primitives + the
loader); Csr* is subsystem-IPC machinery. They got bolted onto ntdll
historically because every Win32 DLL that needed them was already
linking ntdll.lib. The right home would be a separate `csrcli.dll`
loaded by Win32 DLLs that need to talk to csrss — non-Win32 binaries
(native apps, smss, csrss itself) never reference it.

`CSRSTUB.C` provides fail-stub bodies (`STATUS_PORT_DISCONNECTED` for
NTSTATUS returns, NULL/0 for buffers/lengths). All eight are kept as
**internal-only** symbols — not exported from kernel32, not in
`KERNEL32.SRC`. Mechanism:

- Both `BASE/CLIENT/SOURCES.INC` and `WINNLS/SOURCES` define
  `-D_NTSYSTEM_`. That flips `NTSYSAPI` from `__declspec(dllimport)`
  to nothing.
- Consumer .obj's then emit direct `call _CsrFoo@N` instead of
  `call [__imp__CsrFoo@N]` indirect through the IAT.
- `csrstub.obj` defines `_CsrFoo@N` directly; the linker resolves
  the consumer references to those local definitions.
- Without `_NTSYSTEM_`, two failures: csrstub.obj's compile would
  see dllimport-decorated declarations, MSVC would auto-promote the
  defs to dllexport (via a `.drectve` linker directive in the obj),
  and Csr* would leak into kernel32's export table; consumers would
  emit `__imp__` references that nothing satisfies (no csrcli.lib
  to link against).

Side effect of `_NTSYSTEM_`: every other `NTSYSAPI`-decorated symbol
in ntcsrdll.h / ntrtl.h / ntpsapi.h also loses its dllimport hint.
The linker satisfies them via ntdll.lib's non-decorated pseudo-thunks
(one extra `jmp` per call, negligible). ntdll itself uses `_NTSYSTEM_`
when defining the same symbols — we play the same role for Csr*.

### Why Csr* stubs are tolerable

Every kernel32 call site that hits a Csr* stub already handles failure
(legacy paths needed to cope with `STATUS_PORT_DISCONNECTED` if csrss
crashed). Concretely:

- `CsrClientCallServer` returns `STATUS_PORT_DISCONNECTED` →
  `BasepNotifyCsrOfThread` (in CreateProcess/CreateThread) ignores
  the return; `BasepDefineDosDevice` etc. propagate the failure to
  their caller; `BasepNlsPreserveSection` (nlslib) silently skips the
  section preserve (our Lua publisher already used `OBJ_PERMANENT`,
  no preserve needed).
- `CsrAllocateCaptureBuffer` returns NULL → every caller checks for
  NULL and bails with `ERROR_NOT_ENOUGH_MEMORY`.
- `CsrAllocateMessagePointer` returns 0 with `*Pointer = NULL` → most
  callers branch on the upstream NULL CaptureBuffer first; the few
  that don't would deref NULL, but only in code paths the toolchain
  doesn't exercise (VDM checks, DDE atoms, …).
- `CsrNewThread` / `CsrIdentifyAlertableThread` returns are **always
  ignored** by the call sites — they're notify-only.
- `CsrSetPriorityClass` failure → `SetPriorityClass` returns FALSE;
  callers that wrap it tend to log + continue at default priority.

This is "good enough until a real consumer hits a stub and breaks."
Toolchain (CL/LINK/RC/MC) never reaches any of these paths.

---

## NLS data files

`WINNLS/DATA/` holds the eight `.nls` data files that the kernel32
NLS runtime memory-maps:

| File | What it is | How it's used |
|---|---|---|
| `C_437.NLS` / `C_1252.NLS` | OEM + ANSI codepage tables | `RtlInitNlsTables` at kernel boot + per-process via `\NLS\NlsSectionCP*` |
| `L_INTL.NLS` | Default locale tables (US English) | `\NLS\NlsSectionLANG_INTL` |
| `UNICODE.NLS` | Upper/lower case maps + categories | `\NLS\NlsSectionUnicode` |
| `LOCALE.NLS` | Locale ID → property table | `\NLS\NlsSectionLocale` |
| `CTYPE.NLS` | Character-type table | `\NLS\NlsSectionCType` |
| `SORTKEY.NLS` | Default sort weight table | `\NLS\NlsSectionSortkey` |
| `SORTTBLS.NLS` | Sort tables (compose / digit / etc.) | `\NLS\NlsSectionSortTbls` |

All eight are read-only file-backed sections. The named-section
namespace is normally populated by basesrv at csrss start; MicroNT's
Lua boot replaces that role via `src/pkg/nt/nls.lua`'s `publish()`
entrypoint (creates `\NLS` directory + each section with
`OBJ_PERMANENT` so they outlive the publisher process).

The single still-csrss-bound NLS path is the per-locale RW scratch
section (`BasepNlsCreateSortSection`), only exercised when a process
sorts text in a locale that has *exception entries* relative to the
default sort weights (Czech, Slovak, Polish, traditional Spanish,
CJK ideograph orderings). LANG_INTL has no exceptions, so the
default `LCMAP_SORTKEY` / `CompareStringW` paths fall through to
the read-only weight tables we publish. Toolchain runs entirely in
the default locale; the RW scratch path stays as a `STATUS_PORT_DISCONNECTED`
return until a real exception-locale consumer demands it.

---

## Patched private headers

Lifted from `stuff/nt35_patches/NT/PRIVATE/WINDOWS/INC/` into our
`INC/`: `WINBASEP.H`, `WINNLSP.H`, `CONAPI.H`, `WINDEFP.H`,
`WINGDIP.H`, `WINSPRLP.H`, `WINUSERP.H`, `DDEMLP.H`. These are the
private `*p.h` extensions of public headers that BASE/CLIENT
internals reference but stock NT 3.5 had as separate auto-generated
artefacts; the patches set ships pre-generated copies.

---

## Version stamping

`BaseWindowsMajorVersion / MinorVersion / BuildNumber / CSDVersion`
in BASEINIT.C come from `<ntverp.h>` macros:

- `VER_PRODUCTMAJORVERSION` (3)
- `VER_PRODUCTMINORVERSION` (65)
- `VER_PRODUCTBUILD` (YYMM)
- `VER_PRODUCTBUILD_QFE` (DD)

Owned by `tools/libversion.py` + `tools/stamp-version.py`. The major /
minor split-out fields were added specifically for kernel32's
consumption — stock NT 3.5 only had the comma-tuple
`VER_PRODUCTVERSION 3,65,YYMM,DD` which can't be decomposed at the
C preprocessor without variadic macros (MSVC 8.x doesn't have them).

---

## Files unchanged from stock NT 3.5

The vast majority of lifted source compiles untouched. Notable:

- All of `WINNLS/` (12 .c files) — only the `_NTSYSTEM_` flag change in `SOURCES`.
- All of `BASE/RTL/` (atom.c + handle.c).
- `BASE/CLIENT/`: `compname.c`, `curdir.c`, `datetime.c`, `dir.c`,
  `dllini.c`, `error.c`, `filefind.c`, `filehops.c`, `filemap.c`,
  `filemisc.c`, `fileopcr.c`, `gmem.c`, `handle.c`, `lcompat.c`,
  `lmem.c`, `mailslot.c`, `message.c`, `module.c`, `namepipe.c`,
  `perfctr.c`, `pipe.c`, `synch.c`, `tapeapi.c`, `updres.c`,
  `comm.c`, `blddcb.c`, `debugint.c`, `ustubs.c`. Plus all of `I386/`.

Files with `MicroNT:` annotated edits (find via `git grep "MicroNT:"`):

- `BASE/CLIENT/BASEINIT.C` (the big one)
- `BASE/CLIENT/BEEP.C`
- `BASE/CLIENT/DOSDEV.C`
- `BASE/CLIENT/PATHMISC.C`
- `BASE/CLIENT/SUPPORT.C`
- `BASE/CLIENT/STDIO.C` (new — full file)
- `BASE/CLIENT/CSRSTUB.C` (new — full file)
- `BASE/CLIENT/SOURCES.INC` (LINKLIBS, INCLUDES, DLLORDER, C_DEFINES)
- `BASE/CLIENT/MAKEFILE.INC` (new — `mc` invocation)
- `WINNLS/SOURCES` (C_DEFINES `-D_NTSYSTEM_`)

---

## Future work (when toolchain hits a stub and breaks)

1. **Real `csrcli.dll` + `csrsrv.dll`**. Lift `stuff/.../PRIVATE/CSR/`
   (already in tree at `PRIVATE/CSR/SERVER/`), add a CLIENT subdir
   from MS sources or write a thin one. `CSRSTUB.C` deletes;
   `_NTSYSTEM_` comes off; consumer `__imp__Csr*` references resolve
   through csrcli.lib's import thunks. No kernel32 surgery needed.
2. **Real basesrv** — once csrcli works, basesrv brings back
   `BasepDefineDosDevice`, `BasepGetTempFile`, `BasepNotifyCsrOfThread`,
   etc. The strategy-2 stubs in BEEP.C / DOSDEV.C / PATHMISC.C /
   PROCESS.C / THREAD.C revert to their original bodies one by one.
3. **Real conlib** — only when something needs interactive console.
   `STDIO.C` deletes; `WINCON/CLIENT/` lifts; `LINKLIBS` re-adds
   `..\..\obj\*\conlib.lib`.
4. **Per-locale sortkey RW** — for non-default locale collation
   (`LCMapStringW(LCMAP_SORTKEY)` for Czech / Slovak / Spanish-trad
   / CJK). Needs basesrv-equivalent handling for the per-locale RW
   pagefile-backed section. Low priority.
5. **The `Csr*` internal-only architecture audit** — when csrcli
   lands, double-check that no out-of-tree binary depends on
   `GetProcAddress(ntdll, "CsrFoo")` returning non-NULL. (Stock NT
   3.5 exposed Csr* via ntdll exports; with csrcli we move them to
   csrcli.dll, which is the architectural-correct home but breaks
   that binary-compat surface.)
