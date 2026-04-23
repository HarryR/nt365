# MicroNT `cr` — mingw-w64 cross-build of LuaJIT as a native-subsystem
# NT 3.50 binary, linked only against ntdll via our own shim libs.
#
# Ubuntu packages:
#   gcc-mingw-w64-i686 binutils-mingw-w64-i686 mingw-w64-i686-dev
#   gcc-multilib libc6-dev-i386   (buildvm needs a 32-bit host compiler)
#
# Pipeline:
#   ntshim.c / kernel32.c --> libntshim.a + libkernel32.a
#   luajit/src (submodule) --> mingw cross-compiled against those libs
#                           --> luajit.exe (subsystem=native 3.50)
#
# The PE OptionalHeader is pinned to OS/Subsystem version 3.50 so the
# NT 3.5 image loader accepts it. ntdll imports are satisfied at link
# time by mingw's libntdll.a / libntdllcrt.a; if an export we use is
# missing from NT 3.5's real ntdll.dll (e.g. modern additions like
# _errno, strtoul) the shim provides its own definition that shadows
# the import. Top-level targets:
#   make                — build luajit.exe (default)
#   make boot           — build + stage on disk + run under QEMU/OVMF32
#   make boot LAUNCH=x  — run a different .exe as smss's InitialCommand
#   make info           — dump the PE header (subsystem/version/imports)
#   make clean          — wipe local .o / .a / .exe
#   make luajit-clean   — wipe LuaJIT's build tree too

CROSS    := i686-w64-mingw32-
CC       := $(CROSS)gcc
AR       := $(CROSS)ar
OBJDUMP  := $(CROSS)objdump

# -ffreestanding: no hosted CRT assumptions (no printf, no crt0).
# -fno-builtin:  stop gcc from rewriting calls into libc shapes.
# -fno-asynchronous-unwind-tables: strip .eh_frame — native NT has no
#     unwinder for mingw-style DWARF frames and it just wastes image
#     space.
CFLAGS := \
    -m32 -Os -Wall -Wextra \
    -ffreestanding -fno-builtin \
    -fno-asynchronous-unwind-tables \
    -fno-stack-protector \
    -Irt

# Linker flags, passed via -Wl:
#   --subsystem native           → IMAGE_SUBSYSTEM_NATIVE (1)
#   --entry _NtProcessStartup@4  → stdcall-decorated entry point
#   --major/minor-os-version     → OS 3.50 (must match the kernel's
#                                   expectation; a modern 6.0+ stamp
#                                   here causes NT 3.5's loader to
#                                   reject the image)
#   --major/minor-subsystem-version → subsystem 3.50 for the same reason
#   --image-base 0x00400000      → standard user-mode exe base
#   --file/section-alignment      → PE defaults (0x200 / 0x1000)
LDFLAGS := \
    -m32 -nostdlib -nostartfiles -nodefaultlibs \
    -Wl,--subsystem,native \
    -Wl,--entry,_NtProcessStartup@4 \
    -Wl,--major-os-version,3,--minor-os-version,50 \
    -Wl,--major-subsystem-version,3,--minor-subsystem-version,50 \
    -Wl,--image-base,0x00400000 \
    -Wl,--file-alignment,0x200,--section-alignment,0x1000

# libs, in link order:
#   -lntdllcrt — mingw's "CRT on ntdll" (memcpy, strlen, _vsnprintf, …)
#   -lntdll    — kernel Nt*/Rtl*/Ldr* exports
#   -lmingwex  — freestanding extras (log/exp/sqrt/atan2 backing our
#                math intrinsics in ntshim.c)
#   -lgcc      — compiler helpers (__divdi3 etc. for 64-bit math)
LIBS := -lntdllcrt -lntdll -lmingwex -lgcc

# Iteration loop: which of the built .exes runs as the initial user-mode
# process. LAUNCH is the host-side filename (the artifact from the build);
# INIT_EXE is its SystemRoot-relative path on disk. The lua\ dir is self-
# contained: run.exe + main.lua + nt\init.lua all live together.
# Control\InitArgs carries the argv tail — main.lua is what run.exe eats
# on startup.
LAUNCH     ?= luajit.exe
INIT_EXE   ?= lua\run.exe
INIT_ARGS  ?= \SystemRoot\lua\main.lua
INIT_STDIO ?= \Device\Serial0

# Pure-Lua standard library tree — whole dir staged at \SystemRoot\lua\
# via mkdisk's -x dir:dest mode. Set after MKFILE_DIR below.

# Anchor paths to this Makefile's own location so `make -f .../cr/Makefile`
# or `make -C src/cr` both work from any cwd.
MKFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO       := $(abspath $(MKFILE_DIR)/../..)
LUA_DIR    := $(MKFILE_DIR)lua
SRC        := $(REPO)/src
TOOLS      := $(SRC)/tools
BOOT_EFI   := $(SRC)/boot-efi
BUILD      := $(REPO)/build/micront

all: luajit

# librt.a — the whole runtime: NtProcessStartup trampoline (entry.o),
# CRT-shaped API (libc_*.o), and Win32 API shim (k32_*.o). Split by
# concern into small translation units inside rt/; see rt/nt.h,
# rt/libc_internal.h, rt/k32_internal.h for the shared internal layout.
RT_OBJS := rt/entry.o \
           $(patsubst %.c,%.o,$(wildcard rt/libc_*.c)) \
           $(patsubst %.c,%.o,$(wildcard rt/k32_*.c)) \
           $(patsubst %.c,%.o,$(wildcard rt/test_*.c))

librt.a: $(RT_OBJS)
	$(AR) rcs $@ $^

# ---------------- LuaJIT (mingw cross-build, native-NT target) --------
#
# LuaJIT's src/Makefile already supports CROSS= + TARGET_SYS=Windows.
# We drive it here, steering the target-phase link at our shims:
#
#   TARGET_SYS=Windows  — picks the peobj ljvm mode and the Windows
#                          clib path (which libkernel32.a backs).
#   BUILDMODE=static    — no lua51.dll. One monolithic luajit.exe.
#   TARGET_FLAGS         — macro overrides below that disable mingw's
#                          dllimport CRT/kernel32 decls; LuaJIT calls
#                          resolve to our static libs instead.
#   TARGET_LDFLAGS       — subsystem=native, entry = NtProcessStartup
#                          (from rt/entry.o), OS 3.50 stamp.
#   TARGET_XLIBS         — replace default `-lm` with our full link set.
LUAJIT_SRC   := $(MKFILE_DIR)luajit/src
LUAJIT_EXE   := $(LUAJIT_SRC)/luajit.exe

# Macro overrides that strip mingw's auto-injected dllimport attrs so
# LuaJIT's calls resolve to *our* static libs rather than msvcrt /
# kernel32.dll imports:
#   _CRTBLD    — pretend we're building the CRT itself. Kills _CRTIMP
#                markers AND the `__mingw_ovr` printf/fprintf inlines
#                that otherwise forward to __mingw_vfprintf (libmingwex).
#   _KERNEL32_ — kills WINBASEAPI dllimport in <windows.h> for
#                LoadLibrary/GetProcAddress/VirtualAlloc/etc.
#   _USER32_ / _GDI32_ — same for any user32/gdi32 decls LuaJIT may drag
#                in transitively (defensive; probably unused).
LUAJIT_TFLAGS := -m32 -Os -fno-builtin -fno-stack-protector \
                 -fno-asynchronous-unwind-tables \
                 -D_CRTBLD -D_KERNEL32_ -D_USER32_ -D_GDI32_ \
                 -DLUAJIT_DISABLE_LIB_IO -DLUAJIT_DISABLE_LIB_OS \
                 -DLUAJIT_DISABLE_PROFILE

LUAJIT_TLDFLAGS := -nostdlib -nostartfiles -nodefaultlibs \
                   -Wl,--subsystem,native \
                   -Wl,--entry,_NtProcessStartup@4 \
                   -Wl,--major-os-version,3,--minor-os-version,50 \
                   -Wl,--major-subsystem-version,3,--minor-subsystem-version,50 \
                   -Wl,--image-base,0x00400000 \
                   -Wl,--file-alignment,0x200,--section-alignment,0x1000 \
                   -Wl,--kill-at \
                   -L$(MKFILE_DIR)

# --whole-archive on librt so selftest-only entries (rt/test_thread.c)
# get linked even without call-site references, so ffi.C can resolve
# them at runtime. mingwex / ntdll stay normal-linkage.
LUAJIT_TLIBS := -Wl,--whole-archive -lrt -Wl,--no-whole-archive \
                -lmingwex -lntdllcrt -lntdll -lgcc

$(LUAJIT_EXE): librt.a
	# Force relink: LuaJIT's Makefile sees its own luajit.exe as up-to-
	# date vs its own deps and won't notice that our static libs got
	# rebuilt. Blowing away the final target makes it relink (all
	# LuaJIT .o files stay cached — only the link step re-runs, fast).
	rm -f $(LUAJIT_EXE)
	$(MAKE) -C $(LUAJIT_SRC) \
	    HOST_CC="gcc -m32" \
	    HOST_CFLAGS="-DLUAJIT_DISABLE_PROFILE" \
	    CROSS=$(CROSS) \
	    TARGET_SYS=Windows \
	    BUILDMODE=static \
	    TARGET_LFSFLAGS= \
	    TARGET_FLAGS='$(LUAJIT_TFLAGS)' \
	    TARGET_LDFLAGS='$(LUAJIT_TLDFLAGS)' \
	    TARGET_XLIBS='$(LUAJIT_TLIBS)'

luajit: $(LUAJIT_EXE)
	cp $(LUAJIT_EXE) $(MKFILE_DIR)luajit.exe

luajit-clean:
	$(MAKE) -C $(LUAJIT_SRC) clean
	rm -f $(MKFILE_DIR)luajit.exe

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dump the PE header fields that matter for NT 3.5 compatibility —
# subsystem, OS version, subsystem version, and the ntdll imports.
info: luajit.exe
	@echo "=== luajit.exe ==="
	@$(OBJDUMP) -p luajit.exe | grep -E 'Subsystem|MajorOSVer|MinorOSVer|MajorSubsys|MinorSubsys'
	@echo "-- imports --"
	@$(OBJDUMP) -p luajit.exe | awk '/DLL Name:/{p=1} p' | head -30

clean:
	rm -f *.o *.exe *.a

# Full iteration loop: (re)build the exe, bake a micront hive + ESP image
# with $(LAUNCH) as smss's InitialCommand, then boot under OVMF32/QEMU.
# Reuses $(REPO)/build/micront/ — quick to rebuild, not treated as
# precious. Boot-EFI itself is assumed already built (via build.sh boot).
disk: $(LAUNCH)
	mkdir -p $(BUILD)
	python3 $(TOOLS)/mkhive.py --profile micront \
	    --init-exe '$(INIT_EXE)' \
	    --init-args '$(INIT_ARGS)' \
	    --init-stdio '$(INIT_STDIO)' \
	    $(BUILD)/SYSTEM
	python3 $(TOOLS)/mkdisk.py --profile micront \
	    --output-dir $(BUILD) \
	    --efi-binary $(BOOT_EFI)/BOOTX64.EFI \
	    -x $(LUA_DIR):lua \
	    -x $(MKFILE_DIR)$(LAUNCH):lua/run.exe

boot: disk
	$(BOOT_EFI)/boot.sh micront

# selftest: same as boot, but run.exe is pointed at selftest.lua
# instead of main.lua. INIT_ARGS is target-specific so the override
# propagates into disk/boot without affecting plain `make boot`.
selftest: INIT_ARGS = \SystemRoot\lua\selftest.lua
selftest: boot

.PHONY: all clean info disk boot selftest luajit luajit-clean
