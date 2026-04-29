#!/bin/bash
#
# build.sh — convenience wrapper around build.lua.
#
# Bootstraps the host LuaJIT (build/host-tools/luajit) if missing, then
# forwards every argument to build.lua.  Equivalent to running:
#
#     [src/bootstrap.sh]
#     build/host-tools/luajit src/build.lua "$@"
#
# The build orchestration itself lives in build.lua — this script
# exists only so contributors can type `src/build.sh kernel32` without
# remembering the absolute path to luajit or which step builds it.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
LUAJIT="$REPO_ROOT/build/host-tools/luajit"

if [ ! -x "$LUAJIT" ]; then
    echo ">>> host LuaJIT missing — running bootstrap.sh"
    "$SCRIPT_DIR/bootstrap.sh"
fi

exec "$LUAJIT" "$SCRIPT_DIR/build.lua" "$@"
