# MicroNT — top-level orchestration.
#
# Compose disk image, run under QEMU, drive selftest / smoketest.
# Where build.sh produces artifacts, this Makefile operates them.
#
# Targets
#   make disk         — bake the boot disk image from current artifacts
#   make boot         — disk + boot under QEMU/OVMF (canonical: q35+nvme)
#   make selftest     — boot pointed at selftest.lua, watch for ALL PASSED
#   make smoke-disk   — disk image flavoured for smoke.lua (no boot)
#   make smoketest    — boot smoke disk (used by CI matrix per shape)
#   make gdb          — launch gdb with ntoskrnl + hal .dwf loaded
#                       and attach to a running boot.sh --gdb session
#                       (builds the .dwf files on demand if missing)
#
# Hardware shape — flags pass through to boot.sh:
#   make boot MACHINE=q35 DISK=nvme            # canonical (default)
#   make boot MACHINE=pc  DISK=ide             # legacy classic shape
#   make smoketest MACHINE=pc DISK=nvme        # smoke on a specific shape
#
# Boot-time options (compose with any of the above):
#   GDB=1                  pause qemu on gdbstub :1234 for `make gdb`
#   SERIAL_LOG=/tmp/boot.log  pipe stdout+stderr to a file (no terminal flood)
#
#   make smoketest GDB=1 SERIAL_LOG=/tmp/boot.log
#       smoke.lua exits cleanly + everything's captured to /tmp/boot.log;
#       grep '^staged' on it to find each PE's runtime VA.
#
# What this Makefile does NOT build
#   - NT toolchain / kernel / drivers / userland — that's `src/build.sh`.
#   - boot-efi (BOOTX64.EFI) — that's `make -C src/boot-efi`.
#   - cr binaries (run.exe + lua.dll) — auto-built via `make -C cr`,
#     since the disk image stages them.
#
# Run `src/build.sh && make -C src/boot-efi && make -C src boot` for a
# cold-cache "build everything and boot" cycle.

REPO        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC         := $(REPO)/src
BOOT_EFI    := $(SRC)/boot-efi
BUILD       := $(REPO)/build/disk
HOST_LUAJIT := $(REPO)/build/host-tools/luajit

.PHONY: cr disk boot selftest smoketest smoke-disk gdb

# ---------------- cr binaries (run.exe + lua.dll) -------------------
#
# Delegated to the cr Makefile.  Phony — let cr's own dependency
# tracking decide whether anything actually rebuilds.
cr:
	$(MAKE) -C $(SRC)/cr

# ---------------- disk image ----------------------------------------
#
# Init args override.  ntosbe's profiles/ide.lua defaults point at
# lua\run.exe with \SystemRoot\lua\main.lua + stdio on \Device\Serial0;
# we override Init\Args here to swap main.lua for selftest.lua /
# smoke.lua per target below.  Empty by default → profile defaults
# stick (so plain `make boot` runs main.lua).
INIT_ARGS_FLAG ?=

# LAYOUT picks the disk shape: 'single' / 'split-fat' / 'split-ntfs'.
# Default = profile default (split-fat).  Same set of files; the
# orchestrator routes them between ESP and system partition by the
# `where` tag in the profile.
LAYOUT_FLAG :=
ifdef LAYOUT
LAYOUT_FLAG += --layout=$(LAYOUT)
endif

disk: cr
	mkdir -p $(BUILD)
	$(SRC)/bootstrap.sh
	$(HOST_LUAJIT) -e 'package.path="$(SRC)/pkg/?.lua;$(SRC)/pkg/?/init.lua;"..package.path; os.exit(require("ntosbe").main(arg))' -- ntosbe --src-root=$(SRC) --output-dir=$(BUILD) --efi-binary=$(BOOT_EFI)/BOOTX64.EFI $(INIT_ARGS_FLAG) $(LAYOUT_FLAG)

# ---------------- boot / run targets --------------------------------
#
# MACHINE / DISK pass through to boot.sh as --machine / --disk so
# `make boot MACHINE=q35 DISK=nvme` exercises a specific shape.
# Unset defaults fall through to boot.sh's own defaults (q35 + nvme).
BOOT_FLAGS :=
ifdef MACHINE
BOOT_FLAGS += --machine $(MACHINE)
endif
ifdef DISK
BOOT_FLAGS += --disk $(DISK)
endif
# GDB=1 — pause qemu on the gdbstub on :1234 so `make gdb` (in another
# shell) can attach.  Composes with selftest / smoketest:
#   make boot GDB=1                      # interactive
#   make smoketest GDB=1                 # boots smoke.lua, gdb-attached
ifdef GDB
BOOT_FLAGS += --gdb
endif
# SERIAL_LOG=/path — redirect both stdout and stderr to a file instead
# of flooding the terminal.  Useful when main.lua's output scrolls past
# scrollback and you want to grep for boot-efi BXLOG lines (e.g. the
# 'staged ntoskrnl at phys ... base=0x...' line that reveals the
# kernel's runtime VA).  Empty = print to terminal as usual.
ifdef SERIAL_LOG
BOOT_REDIRECT := > $(SERIAL_LOG) 2>&1
else
BOOT_REDIRECT :=
endif

boot: disk
	$(SRC)/boot.sh $(BOOT_FLAGS) $(BOOT_REDIRECT)

selftest: INIT_ARGS_FLAG = --init-args '\SystemRoot\lua\selftest.lua'
selftest: boot

# Smoke test — minimal "did the system boot on this hardware shape?"
# check.  Disk has Init\Args pointed at smoke.lua, which prints
# 'SMOKE OK' and powers off.  Used across the (MACHINE, DISK) matrix
# in CI for ~10 s coverage per combo, vs ~4 min for the full selftest.
#
# `smoke-disk` builds just the smoke-flavour disk image so CI can
# build once and reboot it across the matrix without rebuilding
# the disk per combo.
smoke-disk: INIT_ARGS_FLAG = --init-args '\SystemRoot\lua\smoke.lua'
smoke-disk: disk

smoketest: INIT_ARGS_FLAG = --init-args '\SystemRoot\lua\smoke.lua'
smoketest: boot

# ---------------- gdb DWARF + interactive session -------------------
#
# build.sh runs splitsym + dbg2dwf as part of every build (--syms is
# default), producing <name>.dwf next to each PE.  Those carry full
# DWARF — function names, source/line tables, locals (BP-rel + reg),
# and the CV4 type table converted to DWARF type DIEs.  gdb consumes
# them directly via symbol-file / add-symbol-file.
#
# Drivers ship their .dwf alongside the PE under PUBLIC/SDK/LIB/I386.
# Their runtime VA isn't known until the kernel's loader places them,
# so we don't add-symbol-file them at gdb startup — instead the
# `loaddrivers` command (registered by gdb_drivers.py) walks
# PsLoadedModuleList at runtime, reads each LDR_DATA_TABLE_ENTRY's
# DllBase, and add-symbol-file's the matching .dwf with the right
# slide.  Run it after the kernel reaches a stop point past
# IoInitSystem.

NTOSKRNL_DWF := $(REPO)/src/NT/PRIVATE/NTOS/INIT/UP/obj/i386/ntoskrnl.dwf
HAL_DWF      := $(REPO)/src/NT/PRIVATE/NTOS/NTHALS/HAL/obj/i386/hal.dwf
GDB_INIT     := $(SRC)/tools/gdb.init
GDB_DRIVERS  := $(SRC)/tools/gdb_drivers.py

# `make gdb` — interactive: start `src/boot.sh --gdb` in another
# terminal first; that boots under qemu paused on :1234.  This target
# loads kernel+hal DWARF and connects.
#
# Symbol source: <name>.dwf produced by `src/build.sh` — full DWARF
# (functions, source lines, locals, types).  build.sh defaults to
# --syms so a normal build emits these alongside the .sys/.dll/.exe.
# If the .dwf isn't present this target builds it on demand.
#
# Drivers can't be loaded statically — their runtime VA is chosen by
# the kernel's loader.  `loaddrivers` is registered by gdb_drivers.py
# and walks PsLoadedModuleList to add-symbol-file each driver's .dwf
# at its actual DllBase.  Run it after the kernel has progressed past
# IoInitSystem (after the first kernel-side breakpoint hits).

$(NTOSKRNL_DWF):
	$(SRC)/build.sh init

$(HAL_DWF):
	$(SRC)/build.sh hal

gdb: $(NTOSKRNL_DWF) $(HAL_DWF)
	gdb \
	  -iex 'set confirm off' \
	  -iex 'set pagination off' \
	  -ex 'symbol-file $(NTOSKRNL_DWF)' \
	  -ex 'add-symbol-file $(HAL_DWF)' \
	  -ex 'source $(GDB_INIT)' \
	  -ex 'source $(GDB_DRIVERS)' \
	  -ex 'target remote :1234'
