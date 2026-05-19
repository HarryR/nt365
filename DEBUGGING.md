# Debug recipes

Concrete, copy-paste workflows for the most common failure shapes a
MicroNT agent will hit.  Each recipe states: **the symptom you'd see**,
**the commands to run**, and **how to interpret the output**.  Where a
recipe relies on a tool that doesn't exist yet it's marked **(planned)**
with a sketch of the intended interface — so future iterations can
build the missing piece against a clear spec rather than re-deriving it
from scratch each time.

The bias throughout is deterministic command sequences over prose
explanation.  An agent following one of these shouldn't have to read
the prose to know what to do.

---

## TL;DR — the canonical agentic flow

Most debug loops are some variant of "boot a known machine config,
stop at a known symbol, dump some state, exit cleanly."
`tools/agent_run.sh` is the one-command harness for exactly that.

```sh
src/tools/agent_run.sh \
    --machine q35 --disk nvme \
    --break IopInitializeBootDrivers \
    --inspect 'nt modules' \
    --inspect 'info functions ^FatCommonRead' \
    --json
```

It allocates a random gdb port, spawns qemu with the right flags,
waits for the gdb stub, drives a scripted gdb session
(`add-symbol-file ntoskrnl.dwf` + helpers + `hbreak <SYM>` +
`continue` + your `--inspect` commands), then jumps to `KiAgentExit`
to terminate qemu cleanly.  Bounded in time, process-group isolated
(no zombie qemus on Ctrl-C), structured exit code an agent can
branch on.

Exit codes:

| rc | meaning |
|---|---|
| `0` | PASS — bp hit, inspection done, qemu exited via `KiAgentExit` (rc=1) |
| `1` | pre-flight failed (missing `.dwf` artifacts, port busy, gdb missing) |
| `2` | argument error |
| `3` | TIMEOUT — bp never hit within `--timeout` (default 120s) |
| `4` | qemu died unexpectedly (boot crash before gdb attached, or had to KILL) |
| `5` | BUGCHECK — qemu exited via `KeBugCheckEx`/`ExpSystemErrorHandler` (rc=133) |
| `6` | WALL — outer wall fence (`--wall`) fired |

The reliability contract is documented at the top of the script
itself.  Read `tools/agent_run.sh`'s comment header before
extending it — every property in there (random port, process-group
kill, escalating TERM/KILL) is load-bearing for the
no-hang-no-zombie guarantee that makes this rideable by future
agents.

If you need finer-grained control than agent_run.sh exposes, the
underlying primitives are documented as individual recipes below.

---

## QEMU exit codes (the agentic-loop contract)

`boot.sh` wires `-device isa-debug-exit,iobase=0xf4,iosize=0x04` and
the kernel writes `0x42` to that port at the end of `KeBugCheckEx`,
`KeEnterKernelDebugger`, and `ExpSystemErrorHandler` (the three
sites that used to spin forever after printing the bugcheck text).
QEMU then terminates with `(value << 1) | 1`, so:

| `$?` from `boot.sh` | Meaning |
|---|---|
| `0`     | clean shutdown — selftest reached its exit point |
| `0x85`  | kernel bugcheck or unhandled hard error — `qemu.log` has details |
| anything else | QEMU died unexpectedly (signal, OVMF reject, etc.) |

This is the contract the harness relies on.  An agent driving the
loop just inspects `$?`:

```sh
make -C src selftest 2>&1 | tee qemu.log
rc=${PIPESTATUS[0]}
case $rc in
    0)    echo "PASS" ;;
    133)  src/tools/decode_av.py qemu.log ;;       # 0x85 = 133
    *)    echo "QEMU died: rc=$rc" ;;
esac
```

If a kernel debugger is attached (`boot.sh --gdb`), the in-kernel
`DbgBreakPoint()` is caught by the debugger and the OUT to 0xf4
never executes — original "freeze for inspection" semantics
preserved exactly when they're useful.

## Pre-flight

Before debugging anything, confirm:

```sh
# .dwf files exist for the binaries you're about to inspect.
find src -name '*.dwf' -newer src/build.sh | head    # any recent ones?
ls src/NT/PUBLIC/SDK/LIB/I386/*.dwf | wc -l          # drivers
ls src/NT/PRIVATE/NTOS/INIT/UP/obj/i386/*.dwf 2>/dev/null  # ntoskrnl
```

If a `.dwf` is missing for a binary you need, *that* is the first thing
to fix — without symbols every recipe below is just hex-staring.  See
[Recipe 0](#recipe-0-no-dwf-for-this-binary).

---

## Manual gdb session (interactive)

When `agent_run.sh` is too coarse — e.g. you want to step instructions
or set conditional breakpoints by hand — drive gdb directly:

```sh
src/boot.sh --gdb              # terminal 1: qemu paused on :1234
make -C src gdb                # terminal 2: gdb attached, ntoskrnl + hal symbols loaded
```

`make gdb` symbol-files `ntoskrnl.dwf` and `hal.dwf` (both linked at
canonical bases — no slide), sources `gdb.init` + `gdb_nt.py`, and
connects to `:1234`.  Drivers can't be loaded statically — their
runtime VA is chosen by the kernel's loader; after the first
kernel-side breakpoint hits past `IoInitSystem`, run `nt modules` to
walk `PsLoadedModuleList` and add each module's `.dwf` at its
`DllBase`.

```
(gdb) hbreak Phase1Initialization
(gdb) c
Breakpoint 1, Phase1Initialization (Context=<optimised out>) at init.c:1065
(gdb) tbreak Phase1Initialization      # advance past prologue (see caveats)
(gdb) c
Phase1Initialization (Context=0x8077c100) at init.c:1111
(gdb) info args                        # full state visible
Context = 0x8077c100
(gdb) nt modules                       # post-IoInitSystem
(gdb) hbreak FatCommonRead
```

### The `nt` namespace

All extension commands live under one `nt` prefix in `tools/gdb_nt.py`
(sourced automatically by `make gdb`).  Type `(gdb) nt <TAB>` to list
subcommands, `(gdb) help nt <name>` for usage.

State walks (read NT kernel structures):

| cmd | what it does |
|---|---|
| `nt modules` | walk `PsLoadedModuleList`, add-symbol-file each module's `.dwf` (kernel + drivers) |
| `nt process [count]` | walk `PsActiveProcessHead` → EPROCESS list (planned) |
| `nt thread <eproc>` | walk that process's thread list (planned) |
| `nt handles <eproc>` | walk that process's handle table (planned) |
| `nt objects [path]` | walk the object namespace from `\` (planned) |
| `nt devstack <devobj>` | follow `AttachedDevice` chain (planned) |

Decoders (no kernel access; static lookup):

| cmd | what it does |
|---|---|
| `nt status <code\|name>` | NTSTATUS → `STATUS_*` + description; severity-bit retry; substring name match |

CPU snapshot (formatted views of current state at the breakpoint):

| cmd | what it does |
|---|---|
| `nt regs` | EIP/ESP/EBP/CR2 + GP regs + segment regs, 32-bit-formatted from x86-64 gdbstub |
| `nt stack [N]` | N (default 32) dwords from current ESP |
| `nt frame` | manual EBP-chain unwind with symbol resolution (use when DWARF unwind misses) |
| `nt pcr` | KPCR fields (ExceptionList, StackLimit, Self, Prcb) |
| `nt seh` | walk the SEH chain from `KPCR.NtTib.ExceptionList` |
| `nt trapframe <addr>` | decode KTRAP_FRAME at `<addr>` |
| `nt iret` | decode the iret return frame at top of stack |
| `nt bugcheck` | dump KeBugCheckEx args at frame entry; resolves common codes |

Symbols (load `.dwf` at the right runtime address):

| cmd | what it does |
|---|---|
| `nt addsym <name\|path> <base>` | load PE symbols at runtime base (tree-scan by name, or explicit path) |
| `nt findsym <addr>` | reverse: which PE owns this address? |

Logs:

| cmd | what it does |
|---|---|
| `nt decode [logfile]` | shell out to `decode_av.py` against `qemu.log` (or supplied path) and print symbolicated frames inline |

### Caveats / gotchas

- **`hbreak` lands at `low_pc`, `b` lands at `prologue_end`.**  Hardware
  breakpoints stop at the function's literal entry address (offset 0,
  before `push ebp; mov ebp, esp` runs), where the BP-relative location
  list for formal parameters isn't yet in effect — `info args` shows
  `<optimised out>`.  Software breakpoints (`b` / `tbreak`) honour
  `DW_LNS_set_prologue_end` (emitted by `dbg2dwf`) and skip to
  body_start, where args and locals are fully visible.  `agent_run.sh`
  runs both: `hbreak` to catch the function entry (works
  pre-IoInitSystem before .text is fully mapped), then immediately
  `tbreak <SYM>; continue` to advance past the prologue.

- **gdb is in x86-64 mode** (qemu-system-x86_64 advertises target as
  `i386:x86-64`).  The `.dwf` works around this by emitting x86-64
  DWARF register numbers and 4-byte `DW_OP_deref_size` for stack reads.
  Don't `set architecture i386` — it breaks the gdbstub protocol.
  Convenience-variable names matching x86 register names (`$bp`, `$ip`)
  alias the 16-bit register and silently truncate on assignment; the
  `gdb.init` helpers use `$rNN` + 32-bit casts.

- **Source files don't auto-open** (`init.c: No such file or directory`).
  CV records mixed-case (`AcChkSup.c`) but the dump tooling DOS-flattened
  on-disk to uppercase (`ACCHKSUP.C`).  Linux is case-sensitive.  Until
  `dbg2dwf` gains case-insensitive resolution, gdb resolves all symbols/
  lines/types correctly; only the source-text display is missing.

### What's in a `.dwf`

Each PE built with `--syms` gets a sidecar `.DBG` (extracted by
in-tree `splitsym`) and a `.dwf` (CodeView 4 → DWARF, emitted by
in-tree `dbg2dwf`).  The `.dwf` is a regular ELF gdb can symbol-file,
containing:

- function names and source-line tables, with `DW_LNS_set_prologue_end`
  markers per function so `b <func>` lands at body_start
- BP-relative locals scoped to each function's body range
- the CV4 type table converted to DWARF type DIEs (struct fields,
  unions, arrays — `print pIrp->Tail.Overlay.Thread` walks nested
  structs natively)
- `.debug_aranges` for precise CU-by-PC lookup
- `.debug_frame` CFI for 32-bit-on-x86-64 unwinding so `bt` works

---

## Recipe 0: no `.dwf` for this binary

**Symptom**: gdb shows `??` for addresses in this module; or the file
just doesn't exist next to the `.exe`/`.dll`/`.sys`.

**Cause**: the build target wired splitsym + dbg2dwf for `PUBLIC/SDK/
LIB/I386/*` automatically, but binaries that land in their own
`obj/i386/` (link.exe, cmd-stub, mkmsg, run.exe, …) need explicit
wiring per target.  See `targets.link` in `src/pkg/ntosbe/build.lua`
for the pattern.

**Recipe**:

```sh
# (planned) — once `make syms <component>` lands:
make -C src syms link               # splitsym + dbg2dwf for one target

# Manual today:
cd src/wibo-tools
WIBO=/home/.../wibo/build/.../wibo
$WIBO SPLITSYM.EXE -a 'Z:\path\to\binary.exe'
$WIBO DBG2DWF.EXE 'Z:\path\to\binary.DBG' 'Z:\path\to\binary.dwf'
```

**Long-term** *(planned)*: extend `nmake_target` in `build.lua` to
auto-splitsym every PE in `<comp_dir>/obj/i386/` not just the shared
`PUBLIC/SDK/LIB/I386` dir.  Eliminates this recipe entirely.

---

## Recipe 1: serial log → source line

**Symptom**: `qemu.log` (or terminal output) shows a crash like:

```
UMODE EXC(1st): code=c0000005 addr=01002be0 p0=00000000 p1=feeefeee eip=01002be0
*** STOP: 0x0000001E (0xC0000005, 0x80168BFD, 0x00000000, 0x00000001)
```

**Recipe**:

```sh
src/tools/decode_av.py qemu.log
# or pipe:    src/boot.sh --trace 2>&1 | tee qemu.log | src/tools/decode_av.py
# or single:  src/tools/decode_av.py --addr 0x01002be0
```

**Sample output** (from the actual link.exe AV that motivated this tool):

```
=== UMODE EXC (1st-chance) ===
  code: 0xc0000005  (ACCESS_VIOLATION)
  fault: READ to 0xfeeefeee
    → HeapFree debug fill (CRT)
      (use-after-free or stale view; the value came from inside the
       process, not from the kernel)
  eip: 0x01002be0
    → link.exe (relocated +0xc00000)
        TransitionPFI at .../LINK/COFF/bufio.c:2378

paste-into-gdb:
    add-symbol-file src/NT/.../link.dwf -o 0xc00000
    hbreak *0x1002be0
    # alternative — only stop on the actual fault preimage:
    # hbreak *0x1002be0 if $eax == 0xfeeefeee || $edx == 0xfeeefeee || $ecx == 0xfeeefeee
```

The tool walks the tree finding every PE+.dwf pair, classifies each
crash address as kernel (>= 0x80000000) or user, tries common
relocations (link.exe lands at 0x01000000 = +0xC00000 slide), then
runs `addr2line` against the right `.dwf`.  If a binary's `.dwf` is
missing the recipe in [Recipe 0](#recipe-0-no-dwf-for-this-binary)
applies first.

**The 0xfeeefeee tell**: any faulting address with that pattern (or
`0xcccccccc`, `0xbaadf00d`, `0xfdfdfdfd`) is a user-mode CRT debug-fill
sentinel.  It came from inside the process, not from the kernel.
That's a UAF, not a kernel data-corruption bug.

---

## Recipe 2: gdb attached to a userland fault

**Symptom**: you have a serial-log AV in user mode and want to break
*before* the bad write so you can inspect args.

**Setup**:

```
(gdb) nt addsym link.exe 0x01000000              # tree-scan + slide + add-symbol-file
(gdb) nt addsym /abs/path/link.exe 0x01000000    # explicit path also works
(gdb) nt findsym 0x01002be0                      # reverse: which PE owns this addr?
(gdb) nt decode                                  # symbolicate qemu.log inline
```

`nt addsym` finds the binary (by name under `src/`, or by explicit
path), reads its PE ImageBase, computes `slide = runtime_base - pe_base`,
and runs `add-symbol-file`.  All commands are registered by
`tools/gdb_nt.py`, sourced automatically by `make gdb`.

**Break in the right place**:

```
# Stop only on the actual fault, not every list manipulation:
(gdb) hbreak *0x01002be0 if $edx == 0xfeeefeee

# Or break in the kernel exception path and filter to your process:
(gdb) hbreak KiDispatchException
(gdb) commands
> printf "exc=%#x at eip=%#x\n", \
        ((EXCEPTION_RECORD*)$arg0)->ExceptionCode, \
        ((CONTEXT*)$arg1)->Eip
> end
```

**Gotchas**:

- HW breakpoints (`hbreak`) work across CPL transitions — the CPU
  doesn't care whether the VA is kernel or user.  Don't use software
  `b` for user-mode addresses; the page may not be present yet on
  process load.
- `bt` works through user frames *if* `dbg2dwf` emitted `.debug_frame`
  CFI for the binary.  If it didn't, `info reg` + manual
  `[ebp+offset]` reads still get you args.
- gdb's `next`/`step` will follow user→kernel transitions; expect to
  step through `KiSystemService` if the user code makes a syscall.

---

## Recipe 3: kernel bugcheck

**Symptom**:

```
*** STOP: 0xCAFE5E1F (0x..., 0x..., 0x..., 0x...)
```

or any `0xXXXXXXXX` from `KeBugCheckEx`.

**Recipe**:

```sh
# In one terminal:
src/boot.sh --gdb

# In another:
make -C src gdb
(gdb) hbreak KeBugCheckEx
(gdb) c
... wait for bugcheck ...
Breakpoint, KeBugCheckEx (BugCheckCode=0xcafe5e1f, ...) at bugcheck.c:LINE
(gdb) nt bugcheck                      # decodes args + names common codes
(gdb) bt
(gdb) nt modules                       # if past IoInitSystem
(gdb) bt full                          # locals at every frame
(gdb) nt status 0xc0000005             # decode any NTSTATUS in the args
```

**For specific bugcheck codes**:

| Code | Helper | Where to look |
|---|---|---|
| `0x1E` `KMODE_EXCEPTION_NOT_HANDLED` | arg1=ExceptionCode, arg2=ExceptionAddress | resolve arg2 against module .dwf |
| `0x50` `PAGE_FAULT_IN_NONPAGED_AREA` | arg1=faulting va | check if user pointer not probed |
| `0x7B` `INACCESSIBLE_BOOT_DEVICE` | arg1=DEVICE_OBJECT or NTSTATUS | usually disk-driver path |
| `0xCAFE5E1F` `KI_SEH_GUARD_BUGCHECK` | arg2 ∈ {1..4} = guard reason | see [SEH-PROBLEMS.md](SEH-PROBLEMS.md), Recipe 6 |

---

## Recipe 4: catch the next exception of any type

**Symptom**: something is going wrong but you don't know what or where.

**Recipe** *(planned: ship as `nt trap` in `gdb_nt.py`)*:

```
(gdb) hbreak KiDispatchException
(gdb) commands
> silent
> printf "*** EXC code=%#x va=%#x eip=%#x\n", \
        *(unsigned*)$arg0, \
        *((unsigned*)$arg0+4), \
        ((unsigned*)$arg1)[44]   # CONTEXT.Eip
> bt 5
> end
(gdb) c
```

This catches both kernel-mode and user-mode dispatched exceptions.
For first-chance vs second-chance: NT calls `KiDispatchException` for
each, with the `FirstChance` arg distinguishing.  Filter:

```
(gdb) condition 1 ((unsigned*)$arg2)[0] != 0     # second-chance only
```

---

## Recipe 5: NTFS file invisible to enumeration

**Symptom**: `CreateFile` on `\foo\bar` succeeds, the file is
readable/writable, but `FindFirstFile` / `NtQueryDirectoryFile` walking
`\foo` doesn't include `bar`.

**Recipe**:

1. Run [`pkg/test/fs.lua`](src/pkg/test/fs.lua)'s **multi-create + enumeration** test:
   ```sh
   make -C src selftest TESTS=fs
   ```
   Look for `Bulk create + read-back roundtrip` and `Mixed insert/delete`
   results.  Any "missing entry" / "expected N got M" is this class.

2. Confirm cluster vs FRS sizing:
   ```sh
   tools/dump_ntfs.py build/disk/sys.img | grep -E 'Cluster|FileRecord'
   ```
   If `BytesPerCluster > BytesPerFileRecordSegment`, you're in
   sub-cluster-FRS territory.
   The fix is in tree (NT 4.0 byte-form `NtfsWriteLog` backport); if
   the bug recurs, it means a new caller of `NtfsWriteLog` is using
   the cluster-form helpers `LfsClusterCount` / `LfsTargetVcn` instead
   of the byte-form `StreamOffset` / `StructureSize`.

3. **(planned)** `tools/ntfs_walk.py <volume>.img <path>` — dumps
   every $INDEX_ROOT / $INDEX_ALLOCATION entry under `<path>` and
   cross-checks each entry's `FileReference` against the corresponding
   FRS.  Mismatches = ghost entries.

---

## Recipe 6: SEH chain corruption (`0xCAFE5E1F`)

Full writeup: [SEH-PROBLEMS.md](SEH-PROBLEMS.md).

**One-screen recipe**:

```sh
# Repro is layout-sensitive.  Don't expect determinism — re-run a few
# times if it doesn't fire on the first selftest:
make -C src selftest

# When it fires, the kernel calls KiSehDumpCorruption before
# bugchecking.  Capture qemu.log and look for:
grep -A 60 'SEH chain corruption:' qemu.log
```

**Disambiguation rubric** (full version in SEH-PROBLEMS.md):

| `stack-before:` shows | Suspect |
|---|---|
| Return addresses near `_global_unwind2` / `RtlUnwind` | libcntpr's `_global_unwind2` differs from NT 3.5 source, *or* `RtlUnwind` ordering bug |
| Saved-register state walking back into `NtfsUpdateDuplicateInfo` | C8 compiler's `specific_handler` thunk not restoring `fs:[0]` on JMP-back |
| Frame whose `Handler` is in expected module but `Next` points off-stack | Compiler thunk popped the wrong frame |

**Workaround** *(currently default OFF, canary mode)*: in
`CLEANUP.C`, define `NTFS_NCC_FS0_WORKAROUND=1` to enable the manual
`fs:[0]` save/restore around NCC's inner try/except.  Treat it as a
patch while investigating, not a fix.

---

## Recipe 7: boot hangs / no serial output

**Symptom**: `boot.sh` runs, qemu starts, no output appears in serial /
qemu console.

**Recipe**:

```sh
# 1. Confirm UEFI loader runs at all:
src/boot.sh --trace 2>&1 | head -200    # qemu.log will contain
                                         # OVMF + boot-efi BXLOG output

# 2. If boot-efi never starts: ESP layout broken, OVMF can't find BOOTX64.EFI.
#    Check the disk image:
tools/dump_esp.py build/disk/esp.img | grep -i bootx64

# 3. If boot-efi runs but kernel never starts: serial output should
#    show "[boot-efi] entering kernel" right before the handoff.
#    Missing means LoaderBlock build failed — check BXLOG for errors.

# 4. If kernel runs but stalls early: attach gdb and break at
#    Phase1Initialization or KeBugCheckEx (whichever fires first):
src/boot.sh --gdb &
make -C src gdb
(gdb) hbreak Phase1Initialization
(gdb) hbreak KeBugCheckEx
(gdb) c
```

**Common causes ordered by frequency**:

1. ESP doesn't have `EFI/BOOT/BOOTX64.EFI`. (`make` didn't run all
   the way through; rebuild the disk image.)
2. Kernel deadlocked in Phase 0 (rare; usually shows in `--trace`
   output as a tight `int 1`/`int 13` loop).
3. HAL spinning on a missing PCI device — break at
   `HalpInitializeBus` and step through.

---

## Tools matrix

| Tool | Status | Purpose |
|---|---|---|
| `src/tools/agent_run.sh` | **exists** | full-driving harness: boot+breakpoint+inspect+exit, bounded, structured rc / `--json` |
| `src/tools/gdb_nt.py` | **exists** | the `nt` namespace: 17 subcommands grouped state-walks / decoders / CPU-snapshot / symbols / logs |
| `src/tools/gdb.init` | exists | session config (disassembly flavour, `$kpcr` convenience var) — no user commands |
| `src/tools/decode_av.py` | **exists** | parses serial log → resolved frames + paste-into-gdb commands |
| `KiAgentExit` (NTOS) | **exists** | exported kernel function whose body is the qemu-exit OUT; gdb does `set $pc = KiAgentExit; continue` to terminate cleanly |
| `isa-debug-exit` (boot.sh) | **exists** | qemu device wired in boot.sh; OUT to 0xf4 → qemu exits with `(val<<1)|1` |
| **`nt process` / `thread` / `handles` / `objects` / `devstack`** | **planned** | EPROCESS / ETHREAD / object-namespace walks (subcommands stubbed in `gdb_nt.py`) |
| **`make syms <comp>`** | **planned** | run splitsym + dbg2dwf on a target's `obj/i386/*.{exe,dll}` (today: per-target wiring in `build.lua`) |
| **`tools/ntfs_walk.py`** | **planned** | offline NTFS volume dumper for ghost-entry / corruption diagnosis |
| **`tools/dump_esp.py`** | **planned** | offline ESP / FAT16 dumper |
| **`KiDispatchException` default trap** | **planned** | always-on first-chance exception logger as `nt trap` or similar |
| **PEB-walk auto-base for `nt addsym`** | **planned** | drop the `<runtime_base>` argument by walking active process's `PEB->Ldr->InLoadOrderModuleList` |

The "planned" entries are the highest-leverage missing pieces.  None
require deep architectural work — each is a small Python or Lua script
or a gdb command class — they just haven't been written yet.

---

## Recipe template (for adding new ones)

When you debug something new and the workflow generalises, add it here
in this shape:

```markdown
## Recipe N: <short symptom>

**Symptom**: <what you literally see in logs / on screen>

**Recipe**:
    <copy-paste commands>

**Interpretation**:
    <what the output means, and what to do next>
```

Keep recipes ≤ 30 lines.  If a workflow needs more, it's probably
two recipes.
