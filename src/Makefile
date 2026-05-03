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
#   make symbols      — produce gdb-loadable ELF symbol files for
#                       ntoskrnl + hal under build/gdb/
#   make gdb          — launch gdb with kernel+hal symbols loaded and
#                       attach to a running boot.sh --gdb session
#
# Hardware shape — flags pass through to boot.sh:
#   make boot MACHINE=q35 DISK=nvme        # canonical (default)
#   make boot MACHINE=pc  DISK=ide         # legacy classic shape
#   make smoketest MACHINE=pc DISK=nvme    # smoke on a specific shape
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
GDB_DIR     := $(REPO)/build/gdb
HOST_LUAJIT := $(REPO)/build/host-tools/luajit

.PHONY: cr disk boot selftest smoketest smoke-disk symbols gdb

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

disk: cr
	mkdir -p $(BUILD)
	$(SRC)/bootstrap.sh
	$(HOST_LUAJIT) -e 'package.path="$(SRC)/pkg/?.lua;$(SRC)/pkg/?/init.lua;"..package.path; os.exit(require("ntosbe").main(arg))' -- ntosbe --src-root=$(SRC) --output-dir=$(BUILD) --efi-binary=$(BOOT_EFI)/BOOTX64.EFI $(INIT_ARGS_FLAG)

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

boot: disk
	$(SRC)/boot.sh $(BOOT_FLAGS)

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

# ---------------- gdb symbols + interactive session -----------------
#
# Convert PE binaries to ELF symbol files for native gdb debugging.
# Can't use `objcopy -I pei-i386` directly: LINK 2.50 stores COFF
# symbol Value as RVA-from-ImageBase, but objcopy interprets it as
# section-relative — symbols outside .text come out off-by-section-RVA
# and gdb breakpoints land on never-executed addresses.  pe2elf.py
# parses the COFF symbol table directly with the correct LINK 2.50
# convention, strips the cdecl `_` prefix and stdcall `@N` suffix
# (gas doesn't accept `@` in symbol names anyway), and emits an ELF
# stub via `as`.
#
# Result: ~4600 symbols for ntoskrnl (functions in every section,
# internals included) at correct kernel addresses.  In gdb,
# `b KeBugCheckEx` and `b Phase1Initialization` work natively.
#
# Drivers also work this way but their bases vary per boot
# (boot-efi's PE loader allocates them dynamically); covering them
# is a follow-up that needs boot-efi to emit a runtime base manifest.

NTOSKRNL_PE := $(REPO)/src/NT/PRIVATE/NTOS/INIT/UP/obj/i386/ntoskrnl.exe
HAL_PE      := $(REPO)/src/NT/PRIVATE/NTOS/NTHALS/HAL/obj/i386/hal.dll
GDB_INIT    := $(SRC)/boot-efi/gdb.init
PE2ELF      := $(SRC)/tools/pe2elf.py

$(GDB_DIR):
	mkdir -p $(GDB_DIR)

$(GDB_DIR)/ntoskrnl.elf: $(NTOSKRNL_PE) $(PE2ELF) | $(GDB_DIR)
	python3 $(PE2ELF) $< $@

$(GDB_DIR)/hal.elf: $(HAL_PE) $(PE2ELF) | $(GDB_DIR)
	python3 $(PE2ELF) $< $@

symbols: $(GDB_DIR)/ntoskrnl.elf $(GDB_DIR)/hal.elf

# `make gdb` — interactive: start QEMU paused on :1234 in another
# terminal first (`make boot --gdb`-equivalent: actually run
# `src/boot.sh --gdb` separately, since it needs to be in a different
# shell so this gdb can attach).  This target loads kernel+hal
# symbols, sources the helper definitions, and connects.
#
# To boot + attach in one command: `make gdb-boot` (TODO follow-up
# that backgrounds boot.sh, waits for :1234, then attaches).
gdb: symbols
	gdb \
	  -iex 'set confirm off' \
	  -iex 'set pagination off' \
	  -ex 'symbol-file $(GDB_DIR)/ntoskrnl.elf' \
	  -ex 'add-symbol-file $(GDB_DIR)/hal.elf' \
	  -ex 'source $(GDB_INIT)' \
	  -ex 'target remote :1234'
