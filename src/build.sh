#!/bin/bash
#
# build.sh — host CLI entry into the ntosbe build system.
#
# Bootstraps the host LuaJIT (build/host-tools/luajit) if missing, then
# invokes pkg/ntosbe/build.main(...) with the script's directory as
# script_dir.  The build orchestration itself lives in pkg/ntosbe/build.lua;
# nothing build-related lives at src/ level any more.
#
# Examples:
#     src/build.sh                  # builds 'all' (every group + cr + boot-efi; no disk)
#     src/build.sh null             # one component
#     src/build.sh --wibo-trace ke  # WIBO_DEBUG=1 (host syscall trace) + single component
#     src/build.sh --no-syms ke     # opt out of /Z7 + sidecar .DBG/.dwf (default is on)
#     src/build.sh clean:disk       # drop build/disk/

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
LUAJIT="$REPO_ROOT/build/host-tools/luajit"

if [ ! -x "$LUAJIT" ]; then
    echo ">>> host LuaJIT missing — running bootstrap.sh"
    "$SCRIPT_DIR/bootstrap.sh"
fi

# Pass SCRIPT_DIR + REPO_ROOT through env — `luajit -e CODE arg1 ...`
# exposes args via `arg` but arg[0] doesn't tell us where pkg/ lives.
export NTOSBE_SCRIPT_DIR="$SCRIPT_DIR"
export NTOSBE_REPO_ROOT="$REPO_ROOT"

## luajit -e CODE -- ARG0 ARG1 ARG2 ... assigns ARG0 → arg[0] and ARG1.. → arg[1..].
## Pass "build.sh" as a sentinel arg[0] so the user's "$@" lands at arg[1..].
exec "$LUAJIT" -e '
    local script_dir = os.getenv("NTOSBE_SCRIPT_DIR")
    local repo_root  = os.getenv("NTOSBE_REPO_ROOT")
    package.path = script_dir .. "/pkg/?.lua;"
                .. script_dir .. "/pkg/?/init.lua;"
                .. package.path
    os.exit(require("ntosbe.build").main{
        script_dir = script_dir,
        repo_root  = repo_root,
        args       = arg,
    })
' -- "build.sh" "$@"
