#!/bin/bash
#
# createwineprefix.sh - Create an isolated Wine prefix for building MicroNT
#
# Sets up:
#   D:\  ->  src/NT/   (the NT build root, i.e. BASEDIR=D:\)
#
# The prefix is stored in src/.wineprefix and is fully self-contained.
# Re-run this script to recreate from scratch.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/.wineprefix"
NT_DIR="$SCRIPT_DIR/NT"

if [ ! -d "$NT_DIR" ]; then
    echo "ERROR: NT directory not found at $NT_DIR"
    exit 1
fi

# Remove existing prefix if present
if [ -d "$PREFIX" ]; then
    echo "Removing existing prefix..."
    rm -rf "$PREFIX"
fi

echo "Creating Wine prefix at $PREFIX ..."
export WINEPREFIX="$PREFIX"
WINEDEBUG=-all wineboot -i 2>/dev/null

# Map D: to the NT source root
ln -sf "$NT_DIR" "$PREFIX/dosdevices/d:"

# Disable crash/debug dialogs — headless builds should never pop up GUI
WINEDEBUG=-all wine reg add 'HKCU\Software\Wine\WineDbg' \
    /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null 2>&1
# Point the AeDebug debugger at nothing so no winedbg spawns
WINEDEBUG=-all wine reg add 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug' \
    /v Debugger /t REG_SZ /d "false" /f >/dev/null 2>&1
WINEDEBUG=-all wine reg add 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug' \
    /v Auto /t REG_SZ /d "1" /f >/dev/null 2>&1

# Verify the mapping works
echo "Verifying D: drive mapping..."
WINEDEBUG=-all wine cmd.exe /C "if exist D:\PUBLIC\OAK\BIN\I386\CL.EXE (echo OK) else (echo FAIL)" 2>&1 | grep -q OK
if [ $? -eq 0 ]; then
    echo "  D:\\ -> $NT_DIR  [OK]"
else
    echo "  D:\\ mapping FAILED"
    exit 1
fi

# Verify the toolchain runs
echo "Verifying toolchain..."
TOOLS_OK=true

for tool in CL.EXE ML.EXE LINK.EXE NMAKE.EXE; do
    VER=$(WINEDEBUG=-all wine "D:\\PUBLIC\\OAK\\BIN\\I386\\$tool" 2>&1 | head -1)
    if [ -n "$VER" ]; then
        echo "  $tool: $VER"
    else
        echo "  $tool: FAILED"
        TOOLS_OK=false
    fi
done

if $TOOLS_OK; then
    echo ""
    echo "Wine prefix created successfully."
    echo "  WINEPREFIX=$PREFIX"
    echo "  D:\\ = $NT_DIR"
    echo ""
    echo "Paths for the NT build system:"
    echo "  _NTDRIVE=D:"
    echo "  _NTROOT=\\"
    echo "  BASEDIR=D:\\"
    echo "  NTMAKEENV=D:\\PUBLIC\\OAK\\BIN"
    echo "  PATH=D:\\PUBLIC\\OAK\\BIN\\I386"
else
    echo ""
    echo "WARNING: Some tools failed verification."
    exit 1
fi
