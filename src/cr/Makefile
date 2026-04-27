# MicroNT `cr` — mingw-w64 cross-build of:
#
#   lua.dll  — the shared Lua environment.  Bundles the LuaJIT VM
#              (libluajit.a, all of lua_* / luaL_* / luaJIT_*),
#              the freestanding CRT shims (librt.a — internal),
#              and the cr_thread escape hatches (exported as
#              _cr_thread_*).  THE shared DLL: every native-subsystem
#              EXE that runs Lua links against this and benefits from
#              NT's automatic page-CoW of mapped sections.
#
#   run.exe  — the initial user-mode process the kernel spawns via
#              Control\Init\Exe.  Tiny trampoline: rt/entry.c parses
#              argv from the PEB → run.c's main() LoadLibrary's
#              lua.dll → calls LuajitMain.  No Lua VM here.
#
# Both link librt.a internally for their CRT/k32 shims; lua.dll uses
# its own copy for VM internals, run.exe uses its own copy for the
# LoadLibrary call in run.c.  The duplication is ~50 KB and keeps each
# binary self-contained.
#
# Ubuntu packages:
#   gcc-mingw-w64-i686 binutils-mingw-w64-i686 mingw-w64-i686-dev
#   gcc-multilib libc6-dev-i386   (buildvm needs a 32-bit host compiler)
#
# Subsystem on both: native (1).  Imports: ntdll.dll only (run.exe
# additionally imports lua.dll).  No csrss, no kernel32.dll,
# no user32/gdi32 — MicroNT has no Win32.
#
# Top-level targets:
#   make             — build lua.dll + run.exe (default)
#   make boot        — build + stage on disk + boot under QEMU/OVMF
#   make selftest    — same as boot but pointed at selftest.lua
#   make info        — dump PE headers (subsystem/version/imports)
#   make clean       — wipe local .o / .a / .exe / .dll
#   make luajit-clean — also wipe LuaJIT's build tree

CROSS    := i686-w64-mingw32-
CC       := $(CROSS)gcc
AR       := $(CROSS)ar
OBJDUMP  := $(CROSS)objdump

# -ffreestanding: no hosted CRT assumptions.
# -fno-builtin: stop gcc rewriting calls into libc shapes.
# -fno-asynchronous-unwind-tables: strip .eh_frame; native NT has no
#     mingw-style DWARF unwinder.
CFLAGS := \
    -m32 -Os -Wall -Wextra \
    -ffreestanding -fno-builtin \
    -fno-asynchronous-unwind-tables \
    -fno-stack-protector \
    -D_CRTBLD \
    -Irt

# RELEASE=1 enables size-oriented build:
#   -ffunction-sections / -fdata-sections + -Wl,--gc-sections drops
#     unreferenced functions/data at link time.
#   -fno-ident drops the GCC version string.
#   -Wl,-s strips the symbol table from the final image.
# Untoggled by default — dev iteration stays fast.
#
# Tried -flto: segfaults mingw-w64's ld in this freestanding +
# -nostdlib + custom-entry configuration. Section-GC + strip together
# give most of the benefit without LTO's fragility.
#
# Entry points and exported symbols still carry __attribute__((used))
# so a future LTO retry doesn't need to re-add them.
RELEASE       ?= 0
ifeq ($(RELEASE),1)
RELEASE_CFLAGS  := -ffunction-sections -fdata-sections -fno-ident
RELEASE_LDFLAGS := -Wl,--gc-sections -Wl,-s
CFLAGS          += $(RELEASE_CFLAGS)
else
RELEASE_CFLAGS  :=
RELEASE_LDFLAGS :=
endif

# Anchor paths to this Makefile's location.
MKFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO       := $(abspath $(MKFILE_DIR)/../..)
SRC        := $(REPO)/src
TOOLS      := $(SRC)/tools
BOOT_EFI   := $(SRC)/boot-efi
BUILD      := $(REPO)/build/disk

LUAJIT_SRC := $(MKFILE_DIR)luajit/src
LUAJIT_OUT := $(LUAJIT_SRC)/luajit.exe

# Init args override.  mkhive.py's defaults point at lua\run.exe and
# \SystemRoot\lua\main.lua with stdio on \Device\Serial0; the only
# thing we override here is the script the kernel hands to run.exe,
# so `make selftest` can swap main.lua for selftest.lua without
# re-running the rest of the toolchain.  Empty by default → mkhive
# bakes its own defaults.
INIT_ARGS_FLAG ?=

# Default target: build lua.dll + run.exe.  Must come before any other
# rule so `make` with no args picks it up.
all: lua.dll run.exe

# ---------------- librt + cr_thread ----------------------------------

# librt.a — full freestanding NT runtime: NtProcessStartup trampoline
# (entry.o), CRT-shape API (libc_*.o), Win32-shape kernel32 surface
# (k32_*.o backed by ntdll). There is no kernel32.dll on the target,
# so these shims are load-bearing for anything LuaJIT calls.
RT_OBJS := rt/entry.o \
           $(patsubst %.c,%.o,$(wildcard rt/libc_*.c)) \
           $(patsubst %.c,%.o,$(wildcard rt/k32_*.c))

librt.a: $(RT_OBJS)
	$(AR) rcs $@ $^

# cr_thread.o — the irreducible C trampoline that brings up a fresh
# lua_State on a spawned OS thread. Uses only ntdll + the LuaJIT C API.
cr_thread.o: cr_thread.c
	$(CC) $(CFLAGS) -I$(LUAJIT_SRC) -c -o $@ $<

# lua_main.o — DllMain + LuajitMain wrapper, plus the --wrap=luaL_openlibs
# shim that registers cr_thread into package.preload before user code
# runs.  Needs LuaJIT headers for the lua_State helpers in the wrapper.
lua_main.o: lua_main.c
	$(CC) $(CFLAGS) -I$(LUAJIT_SRC) -c -o $@ $<

# ---------------- LuaJIT cross-build (static lib only) --------------
#
# We invoke LuaJIT's own Makefile in BUILDMODE=static to produce
# libluajit.a — a single archive of every VM .o (lj_*, lib_*).  Our
# own link rules below then bundle that archive into lua.dll
# alongside cr_thread.o + librt.a.
#
# -D_KERNEL32_/_USER32_/_GDI32_ kill dllimport on kernel32/user32/gdi32
# prototypes so LuaJIT's calls resolve to librt's static implementations.
# _CRTBLD: pretend we're building the CRT itself, kills _CRTIMP markers
# + the __mingw_ovr printf inlines.

LUAJIT_TFLAGS := -m32 -Os -fno-builtin -fno-stack-protector \
                 -fno-asynchronous-unwind-tables \
                 -D_CRTBLD \
                 -DLUAJIT_DISABLE_LIB_IO -DLUAJIT_DISABLE_LIB_OS \
                 -DLUAJIT_DISABLE_PROFILE \
                 -D_KERNEL32_ -D_USER32_ -D_GDI32_ \
                 $(RELEASE_CFLAGS)

LUAJIT_SOURCES := $(wildcard $(LUAJIT_SRC)/*.c $(LUAJIT_SRC)/*.h)
LUAJIT_LIB     := $(LUAJIT_SRC)/libluajit.a

# LuaJIT static lib: all VM objects archived.  We ask its Makefile for
# `libluajit.a` directly so it doesn't try to link an EXE we'd discard.
$(LUAJIT_LIB): $(LUAJIT_SOURCES)
	$(MAKE) -C $(LUAJIT_SRC) clean
	$(MAKE) -C $(LUAJIT_SRC) libluajit.a \
	    HOST_CC="gcc -m32" \
	    HOST_CFLAGS="-DLUAJIT_DISABLE_PROFILE" \
	    CROSS=$(CROSS) \
	    TARGET_SYS=Windows \
	    BUILDMODE=static \
	    TARGET_LFSFLAGS= \
	    TARGET_FLAGS='$(LUAJIT_TFLAGS)'

# ---------------- lua.dll --------------------------------------------
#
# Shared environment DLL: LuaJIT VM + cr_thread + librt (internal).
# Linked native-subsystem just like run.exe so the NT loader maps it
# alongside the EXE under the same constraints.
#
# Image base 0x10000000 keeps it out of the EXE's typical 0x00400000
# range so processes that load both don't see overlap requiring a
# rebase (the ntdll loader can rebase, but giving it a sane preferred
# base means the common case stays page-shareable).
#
# Exports: --export-all-symbols for now.  Phase B follow-up: tighten
# with a lua.def to expose only lua_* / luaL_* / luaJIT_* / _cr_thread_*
# / LuajitMain and hide librt internals.
#
# DllMain (in lua_main.c) is the entry; it calls ntshim_init() once on
# DLL_PROCESS_ATTACH so this DLL's libc state is live before any
# consumer reaches in.  Each binary (lua.dll, run.exe) carries its own
# librt copy → its own libc state — no cross-binary aliasing.

LUA_DLL_OBJS := lua_main.o cr_thread.o luajit_main.o

# luajit_main.o — LuaJIT's standalone luajit.c (the interpreter `main`),
# compiled with the same TFLAGS as the VM.  libluajit.a is just the VM
# library; the standalone front-end lives in luajit.c and we'd otherwise
# have an undefined `main` reference at lua.dll link time.  Built into
# our own cr/ dir so a future `make -C luajit clean` doesn't sweep it.
luajit_main.o: $(LUAJIT_SRC)/luajit.c $(LUAJIT_LIB)
	$(CC) $(LUAJIT_TFLAGS) -I$(LUAJIT_SRC) -c -o $@ $<

LUA_LDFLAGS := -nostdlib -nostartfiles -nodefaultlibs \
               -Wl,--major-os-version,3,--minor-os-version,50 \
               -Wl,--major-subsystem-version,3,--minor-subsystem-version,50 \
               -Wl,--file-alignment,0x200,--section-alignment,0x1000 \
               -Wl,--subsystem,native \
               -L$(MKFILE_DIR) \
               $(RELEASE_LDFLAGS)

LUA_DLL_LDFLAGS := $(LUA_LDFLAGS) \
                   -shared \
                   -Wl,--image-base,0x10000000 \
                   -Wl,--out-implib,$(MKFILE_DIR)lua.dll.a \
                   -Wl,--entry,_DllMain@12 \
                   -Wl,--wrap=luaL_openlibs

# lua.def — explicit export list.  Without it `--export-all-symbols`
# leaks librt internals (LoadLibraryA, atexit, …) which run.exe then
# imports across the lua.dll boundary instead of using its own librt
# copy.  Generated from nm output of every object/archive contributing
# to lua.dll, filtering to the four prefix families that constitute
# the public surface:
#
#   lua_*  luaL_*  luaJIT_*    — the Lua C API
#   _cr_thread_*                — escape hatches consumed by nt.thread
#   LuajitMain                  — the run.exe entry point
#
# mingw 32-bit cdecl symbols carry a leading `_` in object form; we
# strip one underscore for the .def name (so `__cr_thread_spawn` in
# nm becomes `_cr_thread_spawn` in the .def, the actual symbol seen
# by callers).
lua.def: $(LUA_DLL_OBJS) $(LUAJIT_LIB)
	@echo "; Auto-generated by cr/Makefile — do not edit." > $@
	@echo "LIBRARY lua.dll" >> $@
	@echo "EXPORTS" >> $@
	@$(CROSS)nm --defined-only $^ 2>/dev/null \
	    | awk '/ T _/ { sym=$$3; sub(/^_/, "", sym); \
	          if (sym ~ /^(lua_|luaL_|luaJIT_|_cr_thread_|LuajitMain$$)/) print "    " sym }' \
	    | sort -u >> $@

lua.dll: $(LUA_DLL_OBJS) librt.a $(LUAJIT_LIB) lua.def
	$(CC) $(LUA_DLL_LDFLAGS) -m32 \
	    -o $@ \
	    lua.def \
	    $(LUA_DLL_OBJS) \
	    $(LUAJIT_LIB) \
	    -Wl,--start-group \
	      -lrt -lmingwex -lntdllcrt -lntdll -lgcc \
	    -Wl,--end-group

# lua.dll.a (import lib) is produced as a side effect of the lua.dll link.
lua.dll.a: lua.dll

# ---------------- run.exe (thin trampoline) --------------------------
#
# rt/entry.o (NtProcessStartup) → main() in run.c → LoadLibrary lua.dll
# → GetProcAddress LuajitMain → tail-call.  Imports ntdll + lua.dll.
# librt is linked in for its own k32 shims (LoadLibraryA, GetProcAddress).

RUN_LDFLAGS := $(LUA_LDFLAGS) \
               -Wl,--image-base,0x00400000 \
               -Wl,--entry,_NtProcessStartup@4

run.exe: run.o librt.a lua.dll.a
	$(CC) $(RUN_LDFLAGS) -m32 \
	    -o $@ \
	    run.o \
	    -Wl,--start-group \
	      -L$(MKFILE_DIR) -l:lua.dll.a -lrt \
	      -lmingwex -lntdllcrt -lntdll -lgcc \
	    -Wl,--end-group

# Force a fresh rebuild (e.g. when LuaJIT submodule moves but its .c
# file timestamps don't, or to recover from a half-built state).
rebuild:
	rm -f $(MKFILE_DIR)run.exe $(MKFILE_DIR)lua.dll $(MKFILE_DIR)lua.dll.a
	$(MAKE) all

luajit-clean:
	$(MAKE) -C $(LUAJIT_SRC) clean
	rm -f $(MKFILE_DIR)run.exe $(MKFILE_DIR)lua.dll $(MKFILE_DIR)lua.dll.a

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dump the PE header fields that matter for NT 3.5 compatibility.
info: all
	@echo "=== lua.dll ==="
	@$(OBJDUMP) -p $(MKFILE_DIR)lua.dll | grep -E 'Subsystem|MajorOSVer|MinorOSVer|MajorSubsys|MinorSubsys|ImageBase'
	@echo "-- imports --"
	@$(OBJDUMP) -p $(MKFILE_DIR)lua.dll | awk '/DLL Name:/{p=1} p' | head -10
	@echo
	@echo "=== run.exe ==="
	@$(OBJDUMP) -p $(MKFILE_DIR)run.exe | grep -E 'Subsystem|MajorOSVer|MinorOSVer|MajorSubsys|MinorSubsys|ImageBase'
	@echo "-- imports --"
	@$(OBJDUMP) -p $(MKFILE_DIR)run.exe | awk '/DLL Name:/{p=1} p' | head -10

clean:
	rm -f *.o *.exe *.dll *.a rt/*.o

# Iteration loop: build run.exe, bake a hive (Control\Init pointing at
# our run.exe + args overrides from this Makefile), boot under QEMU/OVMF.
# mkdisk's _runtime_tree_files stages run.exe + the whole src/pkg/ tree
# under \SystemRoot\lua\.
disk: all
	mkdir -p $(BUILD)
	python3 $(TOOLS)/mkhive.py $(INIT_ARGS_FLAG) $(BUILD)/SYSTEM
	python3 $(TOOLS)/mkdisk.py \
	    --output-dir $(BUILD) \
	    --efi-binary $(BOOT_EFI)/BOOTX64.EFI

boot: disk
	$(SRC)/boot.sh

selftest: INIT_ARGS_FLAG = --init-args '\SystemRoot\lua\selftest.lua'
selftest: boot

.PHONY: all rebuild clean info disk boot selftest luajit-clean
