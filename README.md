# MicroNT (aka *"NT 3.65 Cloud Edition"*)

NT 3.50 "Daytona", built from source on Linux, booting under UEFI on QEMU, with a native-NT Lua runtime as `init`. No Win32, no `smss.exe`, no shell. What if we built NT again from Cutler's vision, without Windows baggage...

## Status

Implemented:

- [x] Self-hosting — booted MicroNT image rebuilds its own
  kernel, drivers, and userland from source
- [x] `gdb` powered kernel & driver debugging
- [x] 64-bit UEFI bootloader (`BOOTX64.EFI`, OVMF on qemu)
- [x] PCI-native HAL (BAR relocation above 4 GiB, no PC/AT assumptions)
- [x] Fast `SYSENTER`/`SYSEXIT` & Zw* kernel service dispatch
- [x] VirtIO transport (modern PCI, shared `virtio.lib`)
  - [x] virtio-blk
  - [x] virtio-net (NDIS 3 miniport)
  - [x] virtio-input (keyboard, mouse → kbdclass + mouclass)
  - [x] virtio-console, virtio-rng
- [x] NDIS 3 + TDI + AFD + TCP / UDP / ICMP / IP
- [x] NVMe (SCSI miniport on top of `scsiport.sys`)
- [x] Native-NT Lua userland (LuaJIT 2.1, FFI to `ntdll`)
- [x] kernel32 + lifted NT 3.5 cmd.exe (no csrss, no user32) — runs
  unmodified Microsoft NT 3.5 toolchain binaries
- [x] NTFS boot volume

Coming next:

- [ ] LAPIC + IOAPIC + HPET HAL (replace i8259 + i8254)
- [ ] SMP
- [ ] GPT partitions (currently MBR; partition format is FAT16 or NTFS)
- [ ] Modern display path (Bochs VBE miniport works; need GOP-handoff loader path)

## Self-host

The booted MicroNT image can rebuild itself.  `test.ntosbe`'s
`'full OS rebuild on guest'` selftest drives `ntosbe.build` (the same
Lua orchestrator the host uses) inside the running guest:

```
NMAKE.EXE  →  cmd.exe /c …
              ├── CL386 → CL → C1 → C2     (kernel/driver C compile)
              ├── RC → CVTRES               (resource compile)
              └── LINK -lib | LINK          (librarian + executable link)
```

…against our kernel32, ntdll, and the NT 3.5 toolchain binaries
staged at `\SystemRoot\pkg\msvc20\`.  Output is a fresh
`ntoskrnl.exe` + drivers + userland built entirely under the OS
that's running.  No Wine, no wibo, no host-side participation
beyond having previously built the image.

The build orchestrator (`src/pkg/ntosbe/build.lua`) is a regular Lua
package module — the *same* code runs on host (against the wibo PE
loader) and on guest (native NT spawn).  All file I/O, process
spawn, and codegen helpers route through `ntosbe.platform`, which
has both backends.

## Lua as init

The kernel spawns one user-mode process via `Control\Init\Exe` — the
Lua runtime (`src/cr/run.exe`, native NT subsystem, imports `ntdll`
only). This is the equivalent of Linux's `/sbin/init`. There is no
`smss.exe`, no `csrss.exe`, no `winlogon`, no GDI / USER. Everything
the system does post-kernel — driver loading, PnP, the test harness,
anything that would have lived in a service — is Lua under
`\SystemRoot\lua\`. FFI bindings to the NT syscall surface live under
`lua/nt/dll/`.

## Repository layout

```
src/NT/PRIVATE/         original NT source (kernel, drivers, sdktools)
src/NT/PUBLIC/          shipped headers + import libs + bootstrap binaries
src/boot-efi/           UEFI loader (gnu-efi, x86_64; long-mode → 32-bit kernel)
src/cr/                 native-NT LuaJIT runtime (run.exe + lua.dll + librt)
src/pkg/                Lua tree staged at \SystemRoot\lua\ on disk
src/pkg/ntosbe/         NT OS Build Environment (hive + disk + profiles)
src/cmd-stub/           minimal cmd.exe replacement for NMAKE
src/tools/              utility scripts (gdb.init, gdb_drivers, dumphive, …)
src/wibo-tools/         symlinks into PUBLIC/OAK/BIN/I386 (built first-run)
src/build.sh            host CLI entry — bootstraps LuaJIT + dispatches into ntosbe.build
src/bootstrap.sh        builds the host LuaJIT used by build.sh
```

The build orchestrator lives in `src/pkg/ntosbe/build.lua` (a regular
package module) so the same body runs on host and inside the booted
guest — the in-OS spawn backend lives in `ntosbe.platform`'s NT-side
implementation (NtCreateFile / ps.spawn).  No build code lives at
`src/` level any more — only the bash wrapper.

`stuff/` and `wibo/` are reference trees. CI fetches a prebuilt
`wibo-x86_64` from the [wibo fork's release
page](https://github.com/HarryR/wibo/releases) — the in-tree `wibo/` is
for diffing.

## Build

```sh
sudo apt install gcc gcc-multilib libc6-dev-i386 make gnu-efi \
                 gcc-mingw-w64-i686 binutils-mingw-w64-i686 \
                 mingw-w64-i686-dev qemu-system-x86 ovmf

git clone --recursive https://github.com/HarryR/nt365
cd nt365
curl -fL https://github.com/HarryR/wibo/releases/download/v1.1.0-micront.2/wibo-x86_64 -o wibo-x86_64 && chmod +x wibo-x86_64
./src/build.sh                                # builds everything (auto-runs bootstrap.sh)
```

Three toolchains coexist:

- **wibo** runs the original MS toolchain (CL 8.50, ML 6.11d, LINK 2.50, NMAKE). No Wine.
- **gcc + gnu-efi** for the UEFI loader.
- **mingw-w64 i686** for the cr testbed (LuaJIT cross-compiled for
  native-NT subsystem).

Output lands in `build/disk/` (`esp.img`, `SYSTEM` hive).

## Run

```sh
make -C src boot                 # canonical: q35 + NVMe (modern PCIe)
make -C src boot MACHINE=pc DISK=ide   # legacy fallback shape
make -C src selftest             # boot, run selftest.lua, shut down (CI signal)
make -C src smoketest            # ~10 s "did it boot?" smoke
```

`src/boot.sh` (next to `build.sh`) wraps QEMU directly — never invoke
`qemu-system-*` by hand.  `--machine` (default `q35`) and `--disk`
(default `nvme`) pick the hardware shape; the same disk image boots
every supported combo:

```sh
boot.sh                              # default: q35 + nvme
boot.sh --machine pc  --disk ide     # legacy classic shape
boot.sh --machine pc  --disk nvme    # NVMe on i440fx
boot.sh --machine pc  --disk virtio-blk
boot.sh --machine q35 --disk ide     # piix3-ide bridge on q35
boot.sh --gdb                        # freeze CPU, listen on :1234 for gdb
boot.sh --trace                      # -d int,cpu_reset,in_asm → ./qemu.log
boot.sh --vga                        # add a VGA window
boot.sh --mem 256                    # bump guest RAM (default 128 MiB)
```

## Iteration

- `src/build.sh <component>` — single component, e.g. `ke`, `mm`,
  `virtio`, `ntdll`. Run with no args or unknown name to list targets.
- `src/build.sh virtio_lib` rebuilds just `virtio.lib`; `virtio`
  rebuilds the lib + every consumer `.sys`.
- `src/build.sh clean:<component>` drops just that component's
  `obj/`; `clean:<group>` (ntoskrnl / drivers / userland / tools)
  recurses; bare `clean` nukes everything.
- The build skips unchanged objects on `.c` mtime alone. Editing a `.h`
  doesn't trigger dependents — touch the `.c` or run `clean:<comp>`.

## Debugging under gdb

`build.sh` defaults to `--syms`: every PE gets a sidecar `.DBG`
(extracted by the in-tree `splitsym`) and a `.dwf` (CodeView 4 → DWARF,
emitted by the in-tree `dbg2dwf`).  The `.dwf` carries function names,
source-line tables, BP-relative locals scoped to each function's body
range, the CV4 type table converted to DWARF type DIEs, `.debug_aranges`
for precise CU-by-PC lookup, and `.debug_frame` CFI for 32-bit-on-x86-64
unwinding.

Build, then in two terminals:

```sh
src/boot.sh --gdb              # boots paused, listens on :1234
make -C src gdb                # loads ntoskrnl.dwf + hal.dwf, attaches
```

`make gdb` symbol-files `ntoskrnl.dwf` and `hal.dwf` (both linked at
canonical bases — no slide).  Drivers can't be loaded statically since
their runtime VA is chosen by the kernel's loader; after the first
kernel-side breakpoint hits, run `loaddrivers` (registered by
`tools/gdb_drivers.py`) to walk `PsLoadedModuleList` and
`add-symbol-file` each driver `.dwf` at its actual `DllBase`.

```
(gdb) hbreak Phase1Initialization      # hbreak for pre-IoInitSystem syms
(gdb) c
Breakpoint 1, Phase1Initialization (Context=0x8077c100) at init.c:1065
(gdb) bt
#0  Phase1Initialization (Context=0x8077c100) at init.c:1065
#1  0x801b2e48 in KiInitializeKernel (Process=0x8019c3b0 <KiIdleProcess>,
    Thread=0x8019c5a0 <P0BootThread>, ...) at kernlini.c:547
(gdb) loaddrivers                       # post-IoInitSystem
(gdb) hbreak FatCommonRead
```

Caveats:

- **`hbreak` not `b`** for any pre-`IoInitSystem` kernel symbol —
  software bps set before the kernel's pages are mapped won't arm.
- **gdb is in x86-64 mode** (qemu-system-x86_64 advertises target as
  `i386:x86-64`).  The `.dwf` works around this by emitting x86-64
  DWARF register numbers and 4-byte `DW_OP_deref_size` for stack reads.
  Don't `set architecture i386` — it breaks the gdbstub protocol.
- **FPO functions show params as `<optimized out>`** in their bodies.
  Most kernel funcs compile with FPO under `/Oxs` despite `/Oy-`; CV
  records BP-relative offsets that aren't valid at runtime, so dbg2dwf
  emits an empty location list rather than wrong values.  Proper
  ESP-relative tracking would need `.debug$F` FPO_DATA + esp-rel CFI.
- **Source files don't auto-open** (`init.c: No such file or directory`).
  CV records mixed-case (`AcChkSup.c`) but the dump tooling DOS-flattened
  on-disk to uppercase (`ACCHKSUP.C`).  Linux is case-sensitive.  Until
  dbg2dwf gains case-insensitive resolution, gdb resolves all symbols/
  lines/types correctly; only the source-text display is missing.

For ad-hoc poking: `src/tools/gdb.init` defines helpers — `regs`, `stk`,
`pcr`, `seh`, `trapframe <addr>`, `iret`, `bugcheck` — sourced
automatically by `make gdb`.  Break at `KeBugCheckEx` to catch every
bugcheck and run `bugcheck` to dump the args.

For user-mode crashes: `src/tools/gdb_users.py` adds `loaduser <name>
<runtime_base>` (mirror of `loaddrivers` but for `link.exe`,
`run.exe`, etc.), `loaduserpath`, `findpe <addr>` (reverse lookup),
and `decodeav` (symbolicate `qemu.log` inline without leaving gdb).
Hardware breakpoints (`hbreak`) work across CPL transitions — once
the user binary's `.dwf` is symbol-loaded, debugging is identical to
kernel-mode.

For one-shot symbolication outside gdb: `src/tools/decode_av.py
qemu.log` parses every `UMODE EXC` / `STOP` line, classifies each
address, runs `addr2line` against the right `.dwf`, annotates
faulting-address heap-fill patterns, and emits paste-ready gdb
commands.  Use `--addr 0xVALUE` for a single-address lookup.

**Bounded exit on bugcheck.**  Stock NT 3.5 spins forever after
printing the "STOP:" text (the operator was meant to transcribe it
from VGA and call Microsoft).  MicroNT exits QEMU cleanly via
`isa-debug-exit` (port 0xf4) at the end of `KeBugCheckEx`,
`KeEnterKernelDebugger`, and `ExpSystemErrorHandler` — `boot.sh`
returns `rc = 0x85` (= 133) on bugcheck, `0` on clean shutdown.
Lets an agentic harness terminate deterministically and read the
bugcheck text from the serial log instead of staring at a stuck
console.  When a kernel debugger is attached (`boot.sh --gdb`)
`DbgBreakPoint()` is caught by gdb and the OUT never executes —
original freeze-for-inspection semantics preserved.

**Full-driving harness for agents.**  `src/tools/agent_run.sh`
boots a chosen machine config under gdb, breaks at a symbol, runs
inspection commands, and exits cleanly with a structured rc — one
shell command from "code on disk" to "I have the symbolicated
state at `<breakpoint>`".  Bounded in time (no infinite spins),
process-group isolated (no zombie qemus on Ctrl-C), and
JSON-emittable for agent consumption.  Uses an exported
`KiAgentExit` kernel function as a deterministic gdb-driven exit
point; gdb does `set $pc = KiAgentExit; continue` after inspection
and qemu terminates with rc=1.  Example:

```sh
src/tools/agent_run.sh --machine q35 --disk nvme \
    --break IopInitializeBootDrivers \
    --inspect 'loaddrivers' \
    --inspect 'info functions ^Iop' \
    --json
```

Exit-code matrix and the recipes for common debug shapes are in
[DEBUG-RECIPES.md](DEBUG-RECIPES.md) — keep that doc honest as the
loop evolves.

Recipes for common crash shapes (kernel bugcheck, user-mode AV, NTFS
ghost entries, SEH chain corruption, hung boot) live in
[DEBUG-RECIPES.md](DEBUG-RECIPES.md), updated as we hone the loop.

