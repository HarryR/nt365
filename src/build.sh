#!/bin/bash
#
# MicroNT Build Script
# Builds NT 3.5 kernel components using the original Microsoft toolchain under Wine
#
# Usage: ./build.sh [component]
#   component: ke, rtl, ex, ob, se, ps, mm, cache, config, init, hal, all
#   If no component specified, builds all
#
# Prerequisites: run ./createwineprefix.sh first
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NT_ROOT="$SCRIPT_DIR/NT"
NTOS="$NT_ROOT/PRIVATE/NTOS"

# Wine prefix with D:\ mapped to src/NT/
export WINEPREFIX="$SCRIPT_DIR/.wineprefix"

if [ ! -d "$WINEPREFIX" ]; then
    echo "ERROR: Wine prefix not found. Run ./createwineprefix.sh first."
    exit 1
fi

if ! command -v wine &>/dev/null; then
    echo "ERROR: wine is not installed"
    exit 1
fi

# Common NT build environment variables (passed to every nmake invocation)
NT_ENV="set _NTDRIVE=D:&& set _NTROOT=\\&& set BASEDIR=D:\\&& set NTMAKEENV=D:\\PUBLIC\\OAK\\BIN&& set 386=1&& set TARGETCPU=I386&& set NT_UP=1&& set NTDEBUG=&& set NTDEBUGTYPE=&& set PATH=D:\\PUBLIC\\OAK\\BIN\\I386"

# run_nmake <linux_dir> <description> [extra_nmake_args...]
run_nmake() {
    local linux_dir="$1"
    local desc="$2"
    shift 2
    local extra_args="$*"

    echo "========================================"
    echo "Building: $desc"
    echo "========================================"

    if [ ! -d "$linux_dir" ]; then
        echo "ERROR: directory not found: $linux_dir"
        return 1
    fi

    mkdir -p "$linux_dir/obj/i386"

    # Ensure the shared NTOS output directory exists (TARGETPATH=..\..\obj)
    mkdir -p "$NTOS/obj/i386"

    # Always regenerate _objects.mac to stay in sync with SOURCES
    "$SCRIPT_DIR/gen_objects.sh" "$linux_dir"

    # Convert Linux path to D:\ path
    local rel_path="${linux_dir#$NT_ROOT}"
    local win_dir="D:$(echo "$rel_path" | sed 's|/|\\|g')"

    cd "$linux_dir"

    WINEDEBUG=-all \
    wine cmd.exe /C \
        "$NT_ENV&& set MAKEDIR=$win_dir&& nmake /NOLOGO NTTEST= UMTEST= UMAPPL= $extra_args"

    local rc=$?
    cd "$SCRIPT_DIR"

    if [ $rc -eq 0 ]; then
        echo ">>> $desc: OK"
    else
        echo ">>> $desc: FAILED (rc=$rc)"
    fi
    return $rc
}

# run_wine_cmd <description> <win_cmd>
# Run an arbitrary command under the Wine NT build environment
run_wine_cmd() {
    local desc="$1"
    local win_cmd="$2"

    WINEDEBUG=-all \
    wine cmd.exe /C "$NT_ENV&& $win_cmd"

    local rc=$?
    if [ $rc -ne 0 ]; then
        echo ">>> $desc: FAILED (rc=$rc)"
    fi
    return $rc
}

# --- Generate struct offset headers (KS386.INC, HAL386.INC) ---
# These MUST match our compiler's struct layout or ASM/C code will disagree.

build_geni386() {
    echo "========================================"
    echo "Building: GENI386 (struct offset generator)"
    echo "========================================"

    local geni_src="$NT_ROOT/PRIVATE/NTOS/KE/I386/GENI386.C"
    local geni_obj="/tmp/geni386.obj"
    local geni_exe="/tmp/geni386.exe"

    if [ ! -f "$geni_src" ]; then
        echo "ERROR: GENI386.C not found"
        return 1
    fi

    run_wine_cmd "GENI386 compile" \
        "cl386 -nologo -c -Zp8 -Gz -Di386=1 -D_X86_=1 -DNT_UP=1 -DSTD_CALL -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -D_NTSYSTEM_ -DDBG=0 -DDEVL=1 -ID:\\PRIVATE\\NTOS\\INC -ID:\\PRIVATE\\NTOS\\KE -ID:\\PRIVATE\\INC -ID:\\PUBLIC\\OAK\\INC -ID:\\PUBLIC\\SDK\\INC -ID:\\PUBLIC\\SDK\\INC\\CRT D:\\PRIVATE\\NTOS\\KE\\I386\\GENI386.C -FoZ:\\tmp\\geni386.obj"

    run_wine_cmd "GENI386 link" \
        "link -nologo -subsystem:console -out:Z:\\tmp\\geni386.exe Z:\\tmp\\geni386.obj D:\\PUBLIC\\SDK\\LIB\\I386\\LIBC.LIB D:\\PUBLIC\\SDK\\LIB\\I386\\KERNEL32.LIB"

    run_wine_cmd "GENI386 run" \
        "Z:\\tmp\\geni386.exe D:\\PUBLIC\\SDK\\INC\\KS386.INC D:\\PRIVATE\\NTOS\\INC\\HAL386.INC"

    echo ">>> GENI386: KS386.INC and HAL386.INC regenerated"
}

# --- Kernel library components (each produces a .lib in NTOS/obj/i386/) ---

build_ke()     { run_nmake "$NTOS/KE/UP"      "KE - Kernel Core"; }
build_rtl()    { run_nmake "$NTOS/RTL/UP"      "RTL - Runtime Library"; }
build_ex()     { run_nmake "$NTOS/EX/UP"       "EX - Executive"; }
build_ob()     { run_nmake "$NTOS/OB/UP"       "OB - Object Manager"; }
build_se()     { run_nmake "$NTOS/SE/UP"       "SE - Security"; }
build_ps()     { run_nmake "$NTOS/PS/UP"       "PS - Process Structure"; }
build_mm()     { run_nmake "$NTOS/MM/UP"       "MM - Memory Manager"; }
build_cache()  { run_nmake "$NTOS/CACHE/UP"    "CACHE - Cache Manager"; }
build_config() { run_nmake "$NTOS/CONFIG/UP"   "CONFIG - Registry"; }
build_lpc()    { run_nmake "$NTOS/LPC/UP"      "LPC - Local Procedure Call"; }
build_dbgk()   { run_nmake "$NTOS/DBGK/UP"    "DBGK - Debug Subsystem"; }
build_io()     { run_nmake "$NTOS/IO/UP"       "IO - I/O Manager"; }
build_kd()     { run_nmake "$NTOS/KD/UP"       "KD - Kernel Debugger"; }
build_fsrtl()  { run_nmake "$NTOS/FSRTL/UP"    "FSRTL - File System RTL"; }
build_raw()    { run_nmake "$NTOS/RAW/UP"      "RAW - Raw File System"; }
build_vdm()    { run_nmake "$NTOS/VDM/UP"      "VDM - Virtual DOS Machine"; }

# --- INIT: links all libs into NTOSKRNL.EXE ---

build_init() {
    # INIT is special: NTTEST=ntoskrnl builds the kernel EXE via MAKEFILE.DEF
    # We must NOT override NTTEST for this component
    local linux_dir="$NTOS/INIT/UP"
    local desc="INIT - NTOSKRNL.EXE"

    echo "========================================"
    echo "Building: $desc"
    echo "========================================"

    mkdir -p "$linux_dir/obj/i386"
    "$SCRIPT_DIR/gen_objects.sh" "$linux_dir"

    local rel_path="${linux_dir#$NT_ROOT}"
    local win_dir="D:$(echo "$rel_path" | sed 's|/|\\|g')"

    cd "$linux_dir"

    WINEDEBUG=-all \
    wine cmd.exe /C \
        "$NT_ENV&& set MAKEDIR=$win_dir&& nmake /NOLOGO UMTEST= UMAPPL="

    local rc=$?
    cd "$SCRIPT_DIR"

    if [ $rc -eq 0 ]; then
        echo ">>> $desc: OK"
        ls -la "$linux_dir/obj/i386/ntoskrnl.exe"
    else
        echo ">>> $desc: FAILED (rc=$rc)"
    fi
    return $rc
}

# --- HAL: builds lib, then links HAL.DLL ---

build_hal() {
    local hal_dir="$NTOS/NTHALS/HAL"

    # Step 1: Build the HAL as a library (via nmake/MAKEFILE.DEF)
    run_nmake "$hal_dir" "HAL - MicroNT HAL (lib)"

    echo "========================================"
    echo "Building: HAL - MicroNT HAL (DLL link)"
    echo "========================================"

    mkdir -p "$hal_dir/obj/i386"
    cd "$hal_dir"

    # Link HAL.DLL (no RC file for now — no version resources needed)
    run_wine_cmd "HAL LINK" \
        "D:&& cd \\PRIVATE\\NTOS\\NTHALS\\HAL&& link -OUT:obj\\i386\\hal.dll -DLL -MACHINE:i386 -BASE:0x80400000 -SUBSYSTEM:NATIVE -ENTRY:HalInitSystem@8 -NODEFAULTLIB -RELEASE -DEBUG:MINIMAL -DEBUGTYPE:COFF -OPT:REF obj\\i386\\*.obj D:\\PUBLIC\\SDK\\LIB\\I386\\ntoskrnl.lib D:\\PUBLIC\\SDK\\LIB\\I386\\libcntpr.lib D:\\PUBLIC\\SDK\\LIB\\I386\\int64.lib D:\\PUBLIC\\SDK\\LIB\\I386\\hal.exp"

    cd "$SCRIPT_DIR"

    if [ -f "$hal_dir/obj/i386/hal.dll" ]; then
        echo ">>> HAL - MicroNT HAL (DLL): OK"
        ls -la "$hal_dir/obj/i386/hal.dll"
    else
        echo ">>> HAL - MicroNT HAL (DLL): FAILED"
        return 1
    fi
}

# --- Main ---

COMPONENT="${1:-all}"

case "$COMPONENT" in
    ke)     build_ke ;;
    rtl)    build_rtl ;;
    ex)     build_ex ;;
    ob)     build_ob ;;
    se)     build_se ;;
    ps)     build_ps ;;
    mm)     build_mm ;;
    cache)  build_cache ;;
    config) build_config ;;
    lpc)    build_lpc ;;
    dbgk)   build_dbgk ;;
    io)     build_io ;;
    kd)     build_kd ;;
    fsrtl)  build_fsrtl ;;
    raw)    build_raw ;;
    vdm)    build_vdm ;;
    init)   build_init ;;
    hal)    build_hal ;;
    geni386) build_geni386 ;;
    all)
        build_geni386
        build_ke
        build_rtl
        build_ex
        build_ob
        build_se
        build_ps
        build_mm
        build_cache
        build_config
        build_lpc
        build_dbgk
        build_io
        build_kd
        build_fsrtl
        build_raw
        build_vdm
        build_init
        build_hal
        echo ""
        echo "========================================"
        echo "Build complete."
        echo "  NTOSKRNL: $NTOS/INIT/UP/obj/i386/ntoskrnl.exe"
        echo "  HAL:      $NTOS/NTHALS/HAL/obj/i386/hal.dll"
        echo "========================================"
        ;;
    *)
        echo "Unknown component: $COMPONENT"
        echo "Usage: $0 [ke|rtl|ex|ob|se|ps|mm|cache|config|lpc|dbgk|io|kd|fsrtl|raw|vdm|init|hal|all]"
        exit 1
        ;;
esac
