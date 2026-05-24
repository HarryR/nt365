# MicroNT (aka *"NT 3.65 Cloud Edition"*)

NT 3.50 "Daytona", built from source on Linux, booting under UEFI on QEMU, with a native-NT Lua runtime as `init`. No Win32, no `smss.exe`, no shell. What if we built NT again from Cutler's vision, without Windows baggage...

## Status

Implemented:

- [x] Self-hosting — booted MicroNT image rebuilds its own
  kernel, drivers, and userland from source
- [x] Kernel-level code coverage during selftest [![Coverage Status](https://coveralls.io/repos/github/HarryR/nt365/badge.svg?branch=main)](https://coveralls.io/github/HarryR/nt365?branch=main)
- [x] `gdb` powered kernel & driver debugging
- [x] 64-bit UEFI bootloader (`BOOTX64.EFI`, OVMF on qemu)
- [x] Firmware-less PVH boot (`qemu -kernel` + RAM-disk initrd, mounted via `ramscsi`)
- [x] 32-bit userland (no VDM, V86, WoW16 or 16-bit paths)
- [x] PCI-native HAL (BAR relocation above 4 GiB, no PC/AT assumptions)
- [x] LAPIC + IOAPIC interrupt delivery (graceful 8259 fallback) — boots
  default `microvm` and runs under KVM (in-kernel irqchip)
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

- [ ] HPET + LAPIC-timer clock (retire the i8254 PIT tick)
- [ ] SMP
- [ ] GPT partitions (currently MBR; partition format is FAT16 or NTFS)
- [ ] Modern display path (Bochs VBE miniport works; need GOP-handoff loader path)

## Lua as init

The kernel spawns one user-mode process via `Control\Init\Exe` — the
Lua runtime (`src/cr/run.exe`, native NT subsystem, imports `ntdll`
only). This is the equivalent of Linux's `/sbin/init`. There is no
`smss.exe`, no `csrss.exe`, no `winlogon`, no GDI / USER. Everything
the system does post-kernel — driver loading, PnP, the test harness,
anything that would have lived in a service — is Lua under
`\SystemRoot\pkg\`. FFI bindings to the NT syscall surface live under
`pkg/nt/dll/`.

## Repository layout

```
src/NT/PRIVATE/         original NT source (kernel, drivers, sdktools)
src/NT/PUBLIC/          shipped headers + import libs + bootstrap binaries
src/boot/               shared boot core (PE loader, mmu, lpb, hwtree, FAT reader)
src/boot/efi/           UEFI loader (gnu-efi, x86_64; long-mode → 32-bit kernel)
src/boot/vmlinuz/       PVH loader (32-bit; qemu -kernel + RAM-disk initrd, no firmware)
src/cr/                 native-NT LuaJIT runtime (run.exe + lua.dll + librt)
src/pkg/                Lua tree staged at \SystemRoot\pkg\ on disk
src/pkg/ntosbe/         NT OS Build Environment (build + hive + layered disk composition)
src/cmd-stub/           minimal cmd.exe replacement for NMAKE
src/tools/              utility scripts (gdb_nt, agent_run, decode_av, dumphive, …)
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
                 mingw-w64-i686-dev qemu-system-x86 ovmf lcov

git clone --recursive https://github.com/HarryR/nt365
cd nt365
curl -fL https://github.com/HarryR/wibo/releases/download/v1.1.0-micront.2/wibo-x86_64 -o wibo-x86_64 && chmod +x wibo-x86_64
./src/build.sh                                # builds all artifacts, no disk image (auto-runs bootstrap.sh)
```

Three toolchains coexist:

- [**wibo**](https://github.com/HarryR/wibo) runs the original MS toolchain (CL 8.50, ML 6.11d, LINK 2.50, NMAKE). No Wine.
- **gcc + gnu-efi** for the UEFI loader.
- **mingw-w64 i686** for the cr testbed (LuaJIT cross-compiled for
  native-NT subsystem).

`build.sh` builds artifacts only — kernel, drivers, userland, `cr`,
`BOOTX64.EFI`, and the PVH loader (`vmlinux`).  Composing a bootable disk
image is a separate step: the
`make` targets below each bake their own (see **Run**).

## Run

```sh
make -C src boot                 # canonical: q35 + NVMe (modern PCIe)
make -C src boot MACHINE=pc DISK=ide   # legacy fallback shape
make -C src boot-ramdisk         # firmware-less PVH (q35/pc/microvm)
make -C src smoketest            # ~10 s "did it boot?" smoke
make -C src smoke-ramdisk        # PVH smoke
make -C src selftest             # boot the kernel + fuzz test suites
make -C src selfhost             # boot the in-OS self-build (rebuilds itself)
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
boot.sh --kvm                        # KVM accel (in-kernel LAPIC/IOAPIC; needs /dev/kvm)
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

`build.sh` defaults to `--syms`: every PE gets a `.dwf` (CodeView 4 →
DWARF) next to it, with full type info, source lines, and prologue_end
markers.  One-shot demo of the agentic debug loop:

```sh
src/tools/agent_run.sh --break Phase1Initialization \
                       --inspect 'info args' \
                       --inspect 'bt 4'
```

```
=== inspection commands (post-prologue) ===
Context = 0x8077c100
#0  Phase1Initialization (Context=0x8077c100) at init.c:1111
#1  PspSystemThreadStartup (StartRoutine=0x801b9f3b, ...) at create.c:1641
#2  KiThreadStartup ()
status=PASS  exit_rc=0  qemu_rc=1
```

That's the canonical flow: boot a chosen machine config, break at a
kernel symbol, dump state, exit cleanly with a structured rc.  No
human in the loop, no infinite spin on bugcheck (we exit qemu via
`isa-debug-exit` at the end of `KeBugCheckEx`/`ExpSystemErrorHandler`,
so `boot.sh` returns `rc=0x85` on a STOP and `0` on clean shutdown).

Full reference — exit codes, the bang-command battery, recipes for
every common failure shape (kernel bugcheck, user-mode AV, NTFS
corruption, SEH chain damage, hung boot), tool matrix, dbg2dwf
internals — lives in [DEBUGGING.md](DEBUGGING.md).

