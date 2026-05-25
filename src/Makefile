# MicroNT — top-level orchestration.
#
# Compose disk image, run under QEMU, drive selftest / smoketest.
# Where build.sh produces artifacts, this Makefile operates them.
#
# Targets
#   make disk         — bake the boot disk image from current artifacts
#   make boot         — disk + boot under QEMU/OVMF (canonical: q35+nvme)
#   make selftest     — compose the `selftest` profile (lean disk, no NT
#                       source tree) + boot it; watch for ALL PASSED
#   make selfhost     — compose the `selfhost` profile (full disk: NT
#                       source tree + MS toolchain) + boot it
#   make smoke-disk   — compose the `smoke` profile disk (no boot)
#   make smoketest    — boot smoke disk (used by CI matrix per shape)
#   make gdb          — launch gdb with ntoskrnl + hal .dwf loaded
#                       and attach to a running boot.sh --gdb session
#                       (builds the .dwf files on demand if missing)
#   make coverage     — boot the selftest under the qcov TCG plugin and
#                       emit build/cov/coverage.info (lcov line coverage)
#   make coverage-html— render that .info to HTML with genhtml
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
#   - the EFI loader (BOOTX64.EFI) — that's `make -C src/boot/efi`.
#   - cr binaries (run.exe + lua.dll) — auto-built via `make -C cr`,
#     since the disk image stages them.
#
# Run `src/build.sh && make -C src/boot/efi && make -C src boot` for a
# cold-cache "build everything and boot" cycle.

REPO        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC         := $(REPO)/src
BOOT_EFI    := $(SRC)/boot/efi
BUILD       := $(REPO)/build/disk

.PHONY: cr disk boot selftest selfhost smoketest smoke-disk iprepro gdb qcov coverage coverage-html vmlinux ramdisk boot-ramdisk smoke-ramdisk smoke-ramdisk-disk

# ---------------- cr binaries (run.exe + lua.dll) -------------------
#
# Delegated to the cr Makefile.  Phony — let cr's own dependency
# tracking decide whether anything actually rebuilds.
cr:
	$(MAKE) -C $(SRC)/cr

# ---------------- disk image ----------------------------------------
#
# PROFILE picks the ntosbe profile — the layer set ntosbe composes.
# Empty → ntosbe's own default (`default`: the lean interactive disk
# booting main.lua), so a plain `make disk` / `make boot` bakes that.
# The selftest / selfhost / smoke* targets below set it explicitly.
PROFILE_FLAG ?=

# Init args override.  Each profile now declares its own entry script
# (profile.entry → Control\Init Args), so no default is needed — a bare
# `make boot` boots the `default` profile's main.lua.  This stays as an
# escape hatch: `make boot INIT_ARGS_FLAG='--init-args \SystemRoot\pkg\foo.lua'`
# overrides the entry for an ad-hoc run.
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
	$(SRC)/build.sh disk --output-dir=$(BUILD) $(PROFILE_FLAG) $(INIT_ARGS_FLAG) $(LAYOUT_FLAG)

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
# KVM=1 — run under hardware virtualization (-accel kvm -cpu host) instead
# of TCG.  Clock + IRQ delivery then go through KVM's in-kernel LAPIC/IOAPIC
# (kernel_irqchip) — the production cloud path.  Default (unset) is TCG, the
# only accelerator CI can use.  Composes with MACHINE / DISK / smoketest:
#   make smoketest KVM=1                     # EFI/disk smoke under KVM
#   make smoke-ramdisk MACHINE=microvm KVM=1 # PVH ramdisk under KVM
ifdef KVM
BOOT_FLAGS += --kvm
endif
# GDB=1 — pause qemu on the gdbstub on :1234 so `make gdb` (in another
# shell) can attach.  Composes with selftest / smoketest:
#   make boot GDB=1                      # interactive
#   make smoketest GDB=1                 # boots smoke.lua, gdb-attached
ifdef GDB
BOOT_FLAGS += --gdb
endif
# NET_HARNESS=PORT — swap QEMU's user-mode SLIRP NIC backend for a
# socket netdev (QEMU listens on 127.0.0.1:PORT) so the host-side
# packet harness (src/tools/netharness/) is the guest's network.
# SLIRP's built-in DHCP goes away; the harness serves it instead.
# Composes with selftest:  make selftest NET_HARNESS=5555
#   then in another shell:  python3 src/tools/netharness/harness.py --port 5555
ifdef NET_HARNESS
BOOT_FLAGS += --net-harness $(NET_HARNESS)
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

ifdef NETDUMP
BOOT_FLAGS += --netdump
endif

boot: disk
	$(SRC)/boot.sh --disk-image $(BUILD)/esp.img $(BOOT_FLAGS) $(BOOT_REDIRECT)

# Selftest — the lean disk: full driver config but no NT source tree
# or MS toolchain, so it composes fast.  test.fs's NTFS directory-index
# stress tests want a real NTFS volume, so the layout defaults to
# split-ntfs; override with `make selftest LAYOUT=split-fat`.
# Each profile carries its own Init\Args, so INIT_ARGS_FLAG is cleared.
selftest: PROFILE_FLAG := --profile selftest
selftest: INIT_ARGS_FLAG :=
selftest: BUILD := $(REPO)/build/disk-selftest
selftest: LAYOUT_FLAG := --layout=$(or $(LAYOUT),split-ntfs)
# --netdump dumps every virtio-net frame to ./vionet.pcap so DHCP
# (and other network-layer test) traffic can be inspected with
# Wireshark / tshark when a test misbehaves.
selftest: BOOT_FLAGS += --netdump
selftest: boot

# DDK 3.51 ABI smoke — boot NT 3.51 DDK pre-compiled CLI utilities
# (DRIVERS.EXE / OBJDIR.EXE in v1) against our kernel + kernel32 and
# observe pass/fail.  selftest is self-referential; this is the drift
# detector — an unmodified Microsoft binary either runs or it doesn't.
# Lean layer set (same as selftest), no NT source / MSVC toolchain.
testddk351: PROFILE_FLAG := --profile ddk351
testddk351: INIT_ARGS_FLAG :=
testddk351: BUILD := $(REPO)/build/disk-ddk351
testddk351: LAYOUT_FLAG := --layout=$(or $(LAYOUT),split-ntfs)
testddk351: boot

# iprepro — the IP-stack hardening reproducer target.  Composes the
# lean `iprepro` profile (same disk as selftest, no NT source tree)
# but boots straight into iprepro.lua: a minimal network endpoint, no
# test suite.  The host-side packet harness drives per-finding
# reproducers against it.  Pair with NET_HARNESS=PORT:
#   make -C src iprepro NET_HARNESS=5555
#   then:  python3 src/tools/netharness/repro/h012.py --port 5555
# See docs-wip/IPSTACK-HARDENING.md §5 and src/tools/netharness/.
iprepro: PROFILE_FLAG := --profile iprepro
iprepro: INIT_ARGS_FLAG :=
iprepro: BUILD := $(REPO)/build/disk-iprepro
iprepro: BOOT_FLAGS += --netdump
iprepro: boot

# Selfhost — the full disk: NT source tree + MS toolchain.  Exercises
# the in-OS rebuild path which drives NMAKE.  NMAKE's mtime comparison
# is `>=` and FAT16 has 2-second granularity, so a fresh disk image
# where SOURCES and ntosbe-generated _objects.mac land in the same FAT
# timestamp slot triggers the legacy `build.exe -O` rebuild rule (which
# we don't ship).  NTFS has 100ns mtime resolution, so the collision
# can't happen — hence the split-ntfs default.
selfhost: PROFILE_FLAG := --profile selfhost
selfhost: INIT_ARGS_FLAG :=
selfhost: BUILD := $(REPO)/build/disk-selfhost
selfhost: LAYOUT_FLAG := --layout=$(or $(LAYOUT),split-ntfs)
selfhost: boot

# Smoke test — minimal "did the system boot on this hardware shape?"
# check.  The `smoke` profile points Init\Args at smoke.lua, which
# prints 'SMOKE OK' and powers off.  Used across the (MACHINE, DISK)
# matrix in CI for ~10 s coverage per combo, vs ~4 min for selftest.
#
# `smoke-disk` builds just the smoke-flavour disk image so CI can
# build once and reboot it across the matrix without rebuilding
# the disk per combo.
smoke-disk: PROFILE_FLAG := --profile smoke
smoke-disk: INIT_ARGS_FLAG :=
smoke-disk: BUILD := $(REPO)/build/disk-smoke
smoke-disk: disk

smoketest: PROFILE_FLAG := --profile smoke
smoketest: INIT_ARGS_FLAG :=
smoketest: BUILD := $(REPO)/build/disk-smoke
smoketest: boot

# ---------------- PVH ramdisk (vmlinuz + initrd) --------------------
#
# The firmware-less boot path: the 32-bit PVH loader (boot/vmlinuz),
# booted via QEMU -kernel with an MBR/FAT16 ramdisk via -initrd. Mirrors
# the disk/boot pair but for the ramdisk layout, and routes through
# boot.sh's PVH mode so it runs across pc / q35 / microvm:
#
#   make boot-ramdisk                    # q35 (default)
#   make boot-ramdisk MACHINE=pc
#   make boot-ramdisk MACHINE=microvm    # the no-firmware shape
#   make smoke-ramdisk MACHINE=microvm   # smoke profile (CI matrix)
BOOT_VMLINUZ  := $(SRC)/boot/vmlinuz
VMLINUX       := $(BOOT_VMLINUZ)/vmlinux
RAMDISK_BUILD := $(REPO)/build/disk-ramdisk

# RAMDISK_FORMAT=zip|fat|split-fat|split-ntfs — the initrd wire format.
# zip (default) ships a STORED zip the loader turns into a RAM FAT16 (and
# upstream edits with standard zip tools); fat is a raw single MBR+FAT16
# image; split-fat / split-ntfs are content-sized MBR images with a FAT16 ESP
# (boot files) + a FAT16/NTFS system volume.  Only zip is named initrd.zip;
# the raw formats are initrd.img.
RAMDISK_FORMAT ?= zip
ifeq ($(RAMDISK_FORMAT),zip)
RAMDISK_FILE := initrd.zip
else
RAMDISK_FILE := initrd.img
endif

vmlinux:
	$(SRC)/build.sh vmlinux

# Bake the ramdisk image from current artifacts via the ntosbe 'ramdisk'
# layout; also builds the PVH loader so boot-ramdisk has both halves.
ramdisk: cr vmlinux
	$(SRC)/build.sh ramdisk --output-dir=$(RAMDISK_BUILD) --ramdisk-format=$(RAMDISK_FORMAT) $(PROFILE_FLAG) $(INIT_ARGS_FLAG)

boot-ramdisk: ramdisk
	$(SRC)/boot.sh --ramdisk $(RAMDISK_BUILD)/$(RAMDISK_FILE) --vmlinux $(VMLINUX) $(BOOT_FLAGS) $(BOOT_REDIRECT)

# Smoke profile over the ramdisk path — the CI matrix entry per machine.
smoke-ramdisk: PROFILE_FLAG := --profile smoke
smoke-ramdisk: INIT_ARGS_FLAG :=
smoke-ramdisk: RAMDISK_BUILD := $(REPO)/build/disk-smoke-ramdisk
smoke-ramdisk: boot-ramdisk

# Build-only smoke ramdisk image (smoke profile, no boot), parallel to
# smoke-disk.  The CI PVH smoke matrix bakes this once into
# build/disk-smoke-ramdisk/initrd.zip, then boots it across machines.
smoke-ramdisk-disk: PROFILE_FLAG := --profile smoke
smoke-ramdisk-disk: INIT_ARGS_FLAG :=
smoke-ramdisk-disk: RAMDISK_BUILD := $(REPO)/build/disk-smoke-ramdisk
smoke-ramdisk-disk: ramdisk

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
GDB_NT       := $(SRC)/tools/gdb_nt.py

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
	  -ex 'source $(GDB_NT)' \
	  -ex 'target remote :1234'

# ---------------- coverage instrumentation --------------------------
#
# qcov is a QEMU TCG plugin (src/tools/qcov.c) that records a per-block
# execution histogram of NT kernel-space code.  It needs no QEMU headers
# — it declares the version-1 plugin API itself, and QEMU resolves the
# qemu_plugin_* symbols at dlopen time — so a plain host gcc builds it,
# and the resulting .so loads on any plugin-enabled QEMU regardless of
# the version CI or a developer happens to have.
#
# `boot.sh --coverage <out>` loads the .so; cov2lcov.py later joins its
# histogram against the serial log's `staged` lines and each module's
# .dwf to emit lcov.  `make qcov` builds just the plugin.
QCOV_SRC := $(SRC)/tools/qcov.c
QCOV_SO  := $(REPO)/build/host-tools/qcov.so

$(QCOV_SO): $(QCOV_SRC)
	mkdir -p $(dir $@)
	gcc -O2 -fPIC -shared -Wall -Wextra -o $@ $<

qcov: $(QCOV_SO)

# `make coverage` — instrumented selftest run + lcov.  Composes and boots
# the selftest profile (same disk as `make selftest`) with the qcov TCG
# plugin loaded, then turns the captured histogram into an lcov .info:
#
#   build/cov/qcov.trace    — per-block execution histogram (qcov plugin)
#   build/cov/serial.log    — boot output; its IOSYS lines are the module
#                             load map cov2lcov.py joins the trace against
#   build/cov/coverage.info — lcov line coverage for the kernel + drivers
#
# `make coverage-html` renders that .info to browsable HTML via genhtml.
#
# The boot is tee'd: visible live and saved.  The selftest powers off via
# isa-debug-exit (a non-zero QEMU exit), so the run goes through `tee`,
# whose own exit status keeps `make` from flagging that as a failure.
COV_DIR   := $(REPO)/build/cov
COV_TRACE := $(COV_DIR)/qcov.trace
COV_LOG   := $(COV_DIR)/serial.log
COV_INFO  := $(COV_DIR)/coverage.info
COV_HTML  := $(COV_DIR)/html

coverage: PROFILE_FLAG := --profile selftest
coverage: INIT_ARGS_FLAG :=
coverage: BUILD := $(REPO)/build/disk-selftest
coverage: LAYOUT_FLAG := --layout=$(or $(LAYOUT),split-ntfs)
coverage: $(QCOV_SO) disk
	mkdir -p $(COV_DIR)
	$(SRC)/boot.sh --disk-image $(BUILD)/esp.img \
	    --coverage $(COV_TRACE) 2>&1 | tee $(COV_LOG)
	python3 $(SRC)/tools/cov2lcov.py \
	    --trace $(COV_TRACE) --serial $(COV_LOG) --out $(COV_INFO)
	@echo "[coverage] lcov → $(COV_INFO)   (run 'make coverage-html' to render)"

# `make coverage-html` — render the last `make coverage` .info to HTML.
# Standalone: it does not re-boot, so HTML can be regenerated cheaply.
# genhtml resolves the repo-relative SF paths from the repo root; CRT /
# compiler-runtime files carry their original build paths and have no
# repo source, so missing-source is downgraded to a warning.
coverage-html: coverage
	cd $(REPO) && genhtml $(COV_INFO) \
	    --output-directory $(COV_HTML) \
	    --title "MicroNT kernel coverage" \
	    --legend --ignore-errors source
	@echo "[coverage-html] → $(COV_HTML)/index.html"
