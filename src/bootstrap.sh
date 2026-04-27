#!/bin/bash
#
# bootstrap.sh — build a host-native LuaJIT binary from the in-tree
# submodule at src/cr/luajit, used by build.lua.
#
# Same Lua source MicroNT itself runs — keeps the host build script and
# the in-MicroNT runtime bit-identical for phase 2 (kernel32 shim →
# self-hosting CL build).
#
# cr/Makefile cross-compiles the same source to i386 mingw-w64 for
# MicroNT.  Each invocation cleans the LuaJIT tree first, so leaving
# host-build artefacts in the source dir is fine — but we wipe after
# anyway out of paranoia.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LUAJIT_SRC="$SCRIPT_DIR/cr/luajit"
HOST_OUT_DIR="$(dirname "$SCRIPT_DIR")/build/host-tools"
HOST_OUT="$HOST_OUT_DIR/luajit"

if [ ! -d "$LUAJIT_SRC/src" ]; then
    echo "ERROR: LuaJIT submodule missing at $LUAJIT_SRC" >&2
    echo "Run: git submodule update --init src/cr/luajit" >&2
    exit 1
fi

mkdir -p "$HOST_OUT_DIR"

#
# Skip if the existing host binary is newer than every LuaJIT source
# file — saves the ~5-second rebuild on no-op runs.
#
if [ -x "$HOST_OUT" ]; then
    newest_src="$(find "$LUAJIT_SRC/src" "$LUAJIT_SRC/Makefile" \
                       \( -name '*.c' -o -name '*.h' -o -name 'Makefile*' \) \
                       -newer "$HOST_OUT" -print -quit 2>/dev/null)"
    if [ -z "$newest_src" ]; then
        exit 0
    fi
fi

echo ">>> Bootstrapping host LuaJIT from $LUAJIT_SRC"

#
# The fork excludes lib_io.o/lib_os.o from LJLIB_O for MicroNT (which
# disables io/os via -DLUAJIT_DISABLE_LIB_IO/-DLUAJIT_DISABLE_LIB_OS in
# cr's compile flags).  For build.lua on the host we need both — io and
# os are how every build script reads files and spawns processes.
# Override LJLIB_O on this single make invocation; leaves the fork
# untouched.
#
LJLIB_O_HOST="lib_base.o lib_math.o lib_bit.o lib_string.o lib_table.o \
              lib_io.o lib_os.o lib_package.o lib_debug.o lib_jit.o \
              lib_ffi.o lib_buffer.o"

#
# Clean before AND after — cr's Makefile expects a clean tree on each
# build (its mingw cross-compile would otherwise see x86_64 host objects
# and fail on link).  We do the same courtesy in reverse.
#
make -C "$LUAJIT_SRC" clean >/dev/null
make -C "$LUAJIT_SRC" -j"$(nproc 2>/dev/null || echo 4)" \
    LJLIB_O="$LJLIB_O_HOST" >/dev/null

cp "$LUAJIT_SRC/src/luajit" "$HOST_OUT"

make -C "$LUAJIT_SRC" clean >/dev/null

echo ">>> Host LuaJIT at $HOST_OUT"
"$HOST_OUT" -v 2>&1 | head -1
