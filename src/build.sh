#!/bin/bash
#
# MicroNT Build Script
# Builds NT 3.5 kernel components using the original Microsoft toolchain
# under wibo (a minimal Win32 PE runner).
#
# Usage: ./build.sh [--debug] [component]
#   --debug:    enable WIBO_DEBUG tracing
#   component:  ke, rtl, ex, ob, se, ps, mm, cache, config, init, hal, all
#   If no component specified, builds all
#
# Prerequisites: wibo-x86_64 binary at repo root, src/wibo-tools/ populated
# with symlinks to PUBLIC/OAK/BIN/I386 + CRTDLL.DLL.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NT_ROOT="$SCRIPT_DIR/NT"
NTOS="$NT_ROOT/PRIVATE/NTOS"

WIBO_BIN="$(dirname "$SCRIPT_DIR")/wibo-x86_64"
WIBO_TOOLS="$SCRIPT_DIR/wibo-tools"

if [ ! -x "$WIBO_BIN" ]; then
    echo "ERROR: wibo binary not found or not executable: $WIBO_BIN"
    echo "Download from https://github.com/HarryR/wibo/releases (the MicroNT-patched fork)"
    echo "and place as $WIBO_BIN, then chmod +x."
    exit 1
fi

# Populate wibo-tools/ if missing. Needs to happen before the NT_ENV_ARR
# init below, since PATH / WIBO_PATH point into it. The directory holds
# symlinks to every tool in PUBLIC/OAK/BIN/I386 plus CRTDLL.DLL and the
# cmd.exe / MC.EXE we rebuild later (via build_cmdstub / build_mc).
if [ ! -d "$WIBO_TOOLS" ]; then
    echo ">>> setting up $WIBO_TOOLS (first-time)"
    mkdir -p "$WIBO_TOOLS"
    for f in "$NT_ROOT"/PUBLIC/OAK/BIN/I386/*; do
        ln -srf "$f" "$WIBO_TOOLS/$(basename "$f")"
    done
    # CRTDLL.DLL lives in the SDK LIB tree, not in OAK/BIN. NMAKE and most
    # MSVC-era tools import from it, so wibo needs to find it alongside
    # the host binaries (via WIBO_PATH or cwd).
    ln -srf "$NT_ROOT/PUBLIC/SDK/LIB/I386/CRTDLL.DLL" "$WIBO_TOOLS/CRTDLL.DLL"
fi

# Wibo only strips "Z:" and "C:" prefixes — everything is routed through Z: as
# Z:\<host-abs-path>\... Build the Windows-style equivalents once.
path_to_win() { printf 'Z:%s' "$(echo "$1" | sed 's|/|\\|g')"; }
NT_ROOT_WIN="$(path_to_win "$NT_ROOT")"
WIBO_TOOLS_WIN="$(path_to_win "$WIBO_TOOLS")"
# _NTROOT is the path component under the drive, e.g. "\home\user\...\NT".
# NT makefiles concatenate $(_NTDRIVE)$(_NTROOT) to re-form the absolute path.
_NTROOT_WIN="$(echo "$NT_ROOT" | sed 's|/|\\|g')"

# NT tool env vars. Previously a single "set X=Y&& ..." string fed to cmd.exe;
# now a bash array so we can set them directly in wibo's env with no shell.
NT_ENV_ARR=(
    "_NTDRIVE=Z:"
    "_NTROOT=${_NTROOT_WIN}"
    "BASEDIR=${NT_ROOT_WIN}"
    "NTMAKEENV=${NT_ROOT_WIN}\\PUBLIC\\OAK\\BIN"
    "386=1"
    "TARGETCPU=I386"
    "NT_UP=1"
    "NTDEBUG="
    "NTDEBUGTYPE="
    "PATH=${WIBO_TOOLS_WIN}"
    "WIBO_PATH=${WIBO_TOOLS}"
    "COMSPEC=${WIBO_TOOLS_WIN}\\cmd.exe"
    "TEMP=Z:\\tmp"
    "TMP=Z:\\tmp"
    "INCLUDE=${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC;${NT_ROOT_WIN}\\PUBLIC\\OAK\\INC;${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC\\CRT"
    "LIB=${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386"
)

# Resolve a tool name ("cl386", "mc", "NMAKE.EXE") to an absolute path in
# wibo-tools, case-insensitively. Returns empty if not found.
wibo_tool_path() {
    local name="$1"
    local match
    match="$(find "$WIBO_TOOLS" -maxdepth 1 -iname "$name" 2>/dev/null | head -1)"
    if [ -z "$match" ] && [[ "$name" != *.* ]]; then
        match="$(find "$WIBO_TOOLS" -maxdepth 1 -iname "${name}.exe" 2>/dev/null | head -1)"
    fi
    echo "$match"
}

# run_nmake <linux_dir> <description> [extra_nmake_args...]
#
# By default clears NTTEST / UMTEST / UMAPPL so they don't accidentally turn
# a kernel-lib build into an EXE build. For components that are intentionally
# UMAPPL builds (like cowtest), set `KEEP_UMAPPL=1` in the caller env to
# preserve the SOURCES file's UMAPPL= directive.
run_nmake() {
    local linux_dir="$1"
    local desc="$2"
    shift 2
    local extra_args=("$@")

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
    python3 "$SCRIPT_DIR/tools/gen_objects.py" "$linux_dir"

    # NMAKE-based dep tracking is unreliable for NT components: source
    # files in the parent dir of the build subdir (e.g. SE/UP builds .c
    # from SE/) and header edits anywhere don't always trigger a recompile.
    # Pre-pass: stat-compare sources against the .obj files in obj/i386 and
    # remove any stale outputs so NMAKE rebuilds them.
    #
    # Two passes:
    #   1) Per-source: if foo.c is newer than foo.obj, rm foo.obj.
    #   2) Per-header: if any .h/.hxx/.inc is newer than the OLDEST .obj,
    #      nuke ALL .obj in obj/i386 (we can't tell which TUs include
    #      which header without a real dependency graph; coarse but
    #      correct). Also covers asm/include changes.
    #
    # Doesn't cover cross-component header changes (editing a header
    # under PUBLIC/SDK/INC won't reach components that build elsewhere
    # unless we explicitly invoke their build_* target).
    {
        local _obj_dir="$linux_dir/obj/i386"
        local _src_dirs=("$linux_dir" "$linux_dir/.." "$linux_dir/i386")
        local _d _src _base _obj _stem
        local _nuked=0

        # Pass 1: stale .obj per source file
        for _d in "${_src_dirs[@]}"; do
            [ -d "$_d" ] || continue
            for _src in "$_d"/*.c "$_d"/*.cxx "$_d"/*.cpp "$_d"/*.asm \
                        "$_d"/*.C "$_d"/*.CXX "$_d"/*.CPP "$_d"/*.ASM; do
                [ -f "$_src" ] || continue
                _base="$(basename "$_src")"
                _stem="${_base%.*}"
                # Match case-insensitive against existing .obj
                _obj="$(find "$_obj_dir" -maxdepth 1 -iname "${_stem}.obj" 2>/dev/null | head -1)"
                if [ -n "$_obj" ] && [ "$_src" -nt "$_obj" ]; then
                    echo "  stale: $_base (newer than $(basename "$_obj"))"
                    rm -f "$_obj"
                fi
            done
        done

        # Pass 2: header edits invalidate everything (can't tell which TU
        # includes which .h without a real dep scanner)
        if [ -d "$_obj_dir" ] && ls "$_obj_dir"/*.obj >/dev/null 2>&1; then
            local _oldest_obj
            _oldest_obj="$(ls -t "$_obj_dir"/*.obj 2>/dev/null | tail -1)"
            for _d in "${_src_dirs[@]}"; do
                [ -d "$_d" ] || continue
                for _src in "$_d"/*.h "$_d"/*.hxx "$_d"/*.hpp "$_d"/*.inc \
                            "$_d"/*.H "$_d"/*.HXX "$_d"/*.HPP "$_d"/*.INC; do
                    [ -f "$_src" ] || continue
                    if [ -n "$_oldest_obj" ] && [ "$_src" -nt "$_oldest_obj" ]; then
                        echo "  header changed: $(basename "$_src") (newer than $(basename "$_oldest_obj")) — nuking $_obj_dir/*.obj"
                        rm -f "$_obj_dir"/*.obj
                        _nuked=1
                        break 2
                    fi
                done
            done
        fi
    }

    local rel_path="${linux_dir#$NT_ROOT}"
    local makedir_win="${NT_ROOT_WIN}$(echo "$rel_path" | sed 's|/|\\|g')"

    local umappl_override="UMAPPL="
    if [ "${KEEP_UMAPPL:-}" = "1" ]; then
        umappl_override=""
    fi

    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "${NT_ENV_ARR[@]}" "MAKEDIR=${makedir_win}" \
        "$WIBO_BIN" --chdir "$linux_dir" \
            "${WIBO_TOOLS}/NMAKE.EXE" /NOLOGO NTTEST= UMTEST= \
            ${umappl_override:+"$umappl_override"} "${extra_args[@]}"

    local rc=$?

    if [ $rc -eq 0 ]; then
        echo ">>> $desc: OK"
    else
        echo ">>> $desc: FAILED (rc=$rc)"
    fi
    return $rc
}

# run_wibo_cmd <description> <win_cmd>
#
# Runs a NT-style command under wibo with the standard NT env. Accepts the
# same single-string form the old cmd.exe-based wrapper took, including
# optional leading "X:&& cd \PATH\TO\DIR&& ..." sequences which we translate
# into --chdir on the host.
run_wibo_cmd() {
    local desc="$1"
    local win_cmd="$2"
    local cwd="$SCRIPT_DIR"

    # Strip optional "X:&& cd \WIN\PATH&& " prefix (cmd.exe drive+cd).
    if [[ "$win_cmd" =~ ^[A-Za-z]:\&\&[[:space:]]*cd[[:space:]]+\\([^\&]+)\&\&[[:space:]]*(.*)$ ]]; then
        local winpath="${BASH_REMATCH[1]}"
        win_cmd="${BASH_REMATCH[2]}"
        # Trim trailing whitespace from winpath.
        winpath="${winpath%"${winpath##*[![:space:]]}"}"
        cwd="$NT_ROOT/${winpath//\\/\/}"
    fi

    # Split on whitespace; args in this codebase don't contain spaces.
    read -ra tokens <<<"$win_cmd"
    local tool_name="${tokens[0]}"
    local tool_path
    tool_path="$(wibo_tool_path "$tool_name")"
    if [ -z "$tool_path" ]; then
        # Not a wibo-tools binary — e.g. Z:\tmp\geni386.exe. Use as-is.
        tool_path="$(echo "$tool_name" | sed "s|^Z:|| ; s|\\\\|/|g")"
        if [ ! -f "$tool_path" ]; then
            echo ">>> $desc: tool not found: $tool_name" >&2
            return 1
        fi
    fi
    local args=("${tokens[@]:1}")

    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "${NT_ENV_ARR[@]}" \
        "$WIBO_BIN" --chdir "$cwd" "$tool_path" "${args[@]}"

    local rc=$?
    if [ $rc -ne 0 ]; then
        echo ">>> $desc: FAILED (rc=$rc)"
    fi
    return $rc
}

# run_wibo_tool <cwd> <tool> [args...]
#
# Invoke a single tool directly (no cmd.exe-style string parsing). Call sites
# that used `wine cmd /c "$env&& tool args"` are simpler to express this way
# because PATH/INCLUDE are already set via NT_ENV_ARR.
run_wibo_tool() {
    local cwd="$1" tool_name="$2"; shift 2
    local tool_path
    tool_path="$(wibo_tool_path "$tool_name")"
    if [ -z "$tool_path" ]; then
        echo "ERROR: tool not found in wibo-tools: $tool_name" >&2
        return 1
    fi
    local args=("$@")
    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "${NT_ENV_ARR[@]}" \
        "$WIBO_BIN" --chdir "$cwd" "$tool_path" "${args[@]}"
}

# --- Generate struct offset headers (KS386.INC, HAL386.INC) ---
# These MUST match our compiler's struct layout or ASM/C code will disagree.

build_geni386() {
    echo "========================================"
    echo "Building: GENI386 (struct offset generator)"
    echo "========================================"

    local geni_src="$NT_ROOT/PRIVATE/NTOS/KE/I386/GENI386.C"
    local geni_dir="$NTOS/INIT/UP/obj/i386"
    mkdir -p "$geni_dir"
    local geni_dir_win; geni_dir_win="$(path_to_win "$geni_dir")"

    if [ ! -f "$geni_src" ]; then
        echo "ERROR: GENI386.C not found"
        return 1
    fi

    run_wibo_tool "$SCRIPT_DIR" cl386 \
        -nologo -c -Zp8 -Gz -Di386=1 -D_X86_=1 -DNT_UP=1 -DSTD_CALL \
        -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -D_NTSYSTEM_ -DDBG=0 -DDEVL=1 \
        "-I${NT_ROOT_WIN}\\PRIVATE\\NTOS\\INC" \
        "-I${NT_ROOT_WIN}\\PRIVATE\\NTOS\\KE" \
        "-I${NT_ROOT_WIN}\\PRIVATE\\INC" \
        "-I${NT_ROOT_WIN}\\PUBLIC\\OAK\\INC" \
        "-I${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC" \
        "-I${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC\\CRT" \
        "${NT_ROOT_WIN}\\PRIVATE\\NTOS\\KE\\I386\\GENI386.C" \
        "-Fo${geni_dir_win}\\geni386.obj" || return 1

    run_wibo_tool "$SCRIPT_DIR" link \
        -nologo -subsystem:console \
        "-out:${geni_dir_win}\\geni386.exe" \
        "${geni_dir_win}\\geni386.obj" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\LIBC.LIB" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\KERNEL32.LIB" || return 1

    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "${NT_ENV_ARR[@]}" \
        "$WIBO_BIN" --chdir "$SCRIPT_DIR" \
            "$geni_dir/geni386.exe" \
            "${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC\\KS386.INC" \
            "${NT_ROOT_WIN}\\PRIVATE\\NTOS\\INC\\HAL386.INC" || return 1

    echo ">>> GENI386: KS386.INC and HAL386.INC regenerated"
}

# --- Kernel library components (each produces a .lib in NTOS/obj/i386/) ---

build_ke()     { run_nmake "$NTOS/KE/UP"      "KE - Kernel Core"; }
# RTL needs NTSTATUS→DOS error tables (error.h) generated from GENERR.C
# before error.c compiles. tools/generr.py is a Python port of the
# original generr.exe.
_ensure_error_h() {
    python3 "$SCRIPT_DIR/tools/generr.py" "$NTOS/RTL/error.h"
}
build_rtl()    { _ensure_error_h; run_nmake "$NTOS/RTL/UP"      "RTL - Runtime Library"; }
# bugcodes.rc / bugcodes.h are generated from NTOS/NLS/BUGCODES.MC by
# mc.exe. ntoskrnl.rc #includes bugcodes.rc; NTOS/INC/bugcodes.h is
# referenced by NTOS.H / NTDDK.H / NTHAL.H / NTIFS.H.
_ensure_bugcodes() {
    local nls="$NTOS/NLS"
    if [ -f "$nls/bugcodes.rc" ] && [ -f "$NTOS/INC/bugcodes.h" ] \
       && [ "$nls/bugcodes.rc" -nt "$nls/BUGCODES.MC" ]; then
        return 0
    fi
    echo ">>> mc bugcodes.mc -> bugcodes.h/.rc"
    run_wibo_tool "$nls" mc BUGCODES.MC \
        || { echo "!!! mc on BUGCODES.MC failed"; return 1; }
    # mc.exe emits BUGCODES.rc / BUGCODES.h in the case of the .MC input; Linux
    # is case-sensitive, so match explicitly.
    cp -f "$nls/BUGCODES.rc" "$NTOS/INIT/bugcodes.rc"
    cp -f "$nls/BUGCODES.h"  "$NTOS/INC/bugcodes.h"
    cp -f "$nls/MSG00001.bin" "$NTOS/INIT/msg00001.bin"
}
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

# --- Boot device / filesystem drivers (TARGETTYPE=DRIVER) ---
# serial.sys has its own message catalog (serlog.mc) that mc.exe compiles
# into serlog.rc + serlog.h + msg00001.bin. serial.rc #includes serlog.rc,
# and initunlo.c #includes serlog.h (error codes). Run mc before nmake —
# same pattern as _ensure_bugcodes for ntoskrnl.
_ensure_serlog() {
    local dir="$NTOS/DD/SERIAL"
    if [ -f "$dir/serlog.rc" ] && [ -f "$dir/serlog.h" ] \
       && [ "$dir/serlog.rc" -nt "$dir/SERLOG.MC" ]; then
        return 0
    fi
    echo ">>> mc SERLOG.MC -> serlog.h/.rc"
    run_wibo_tool "$dir" mc SERLOG.MC \
        || { echo "!!! mc on SERLOG.MC failed"; return 1; }
    # mc.exe emits case-matching names on Windows; normalise for our
    # case-sensitive Linux FS so #include "serlog.rc" / "serlog.h" resolve.
    [ -f "$dir/SERLOG.rc" ] && cp -f "$dir/SERLOG.rc" "$dir/serlog.rc"
    [ -f "$dir/SERLOG.h" ]  && cp -f "$dir/SERLOG.h"  "$dir/serlog.h"
}
build_atdisk() { run_nmake "$NTOS/DD/HARDDISK" "ATDISK - IDE disk driver"; }
build_serial() { _ensure_serlog && run_nmake "$NTOS/DD/SERIAL" "SERIAL - NT 3.5 serial port driver"; }

# --- SCSI subsystem ---------------------------------------------------------
# Three-way build chain pulled from the NT 3.5 source dump:
#   class.lib      - shared SCSI class-driver helper code
#   scsiport.sys   - the framework miniports register against
#   scsidisk.sys   - disk class driver (\Device\Harddisk<N>\Partition<P>)
# scsidisk depends on class.lib + scsiport.lib, so build in this order.
# Each component needs its own obj/i386 to land .obj / .res; nmake doesn't
# create these for us when the parent dir is fresh.
build_dd_class()    {
    mkdir -p "$NTOS/DD/CLASS/obj/i386"
    run_nmake "$NTOS/DD/CLASS"    "CLASS - SCSI class-driver helper lib"
}
build_dd_scsiport() {
    mkdir -p "$NTOS/DD/SCSIPORT/obj/i386"
    # makedll=1 -> MAKEFILE.DEF does the second link step that produces
    # scsiport.sys (the loadable driver) on top of scsiport.lib + .exp.
    # Same flag we use for videoprt.sys (the other EXPORT_DRIVER in the tree).
    run_nmake "$NTOS/DD/SCSIPORT" "SCSIPORT - SCSI miniport framework" makedll=1
}
build_dd_scsidisk() {
    mkdir -p "$NTOS/DD/SCSIDISK/obj/i386"
    run_nmake "$NTOS/DD/SCSIDISK" "SCSIDISK - SCSI disk class driver"
}

# nvme2k.sys — NVMe storage controller, ported from
# https://github.com/techomancer/nvme2k (BSD-3). SCSI miniport on top
# of scsiport.sys; SCSIDISK presents the resulting device as
# \Device\Harddisk<N>\Partition<P>.
build_dd_nvme2k() {
    mkdir -p "$NTOS/DD/NVME2K/obj/i386"
    run_nmake "$NTOS/DD/NVME2K" "NVME2K - NVMe storage controller (SCSI miniport)"
}
build_null()   { run_nmake "$NTOS/DD/NULL"     "NULL - null device driver"; }
build_fastfat(){ run_nmake "$NTOS/FASTFAT"     "FASTFAT - FAT filesystem driver"; }
build_npfs()   { run_nmake "$NTOS/NPFS"       "NPFS - Named Pipe filesystem driver"; }
build_msfs()   { run_nmake "$NTOS/MAILSLOT"  "MSFS - Mailslot filesystem driver"; }
build_hello()  { run_nmake "$NTOS/DD/HELLO"    "HELLO - MicroNT visibility driver"; }
build_cowtest(){ KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/TESTS/cowtest" "COWTEST - COW test program"; }

# --- Host tools (sdktools bootstrap phase) -----------------------------------
# Wine-executable host tools consumed by later build steps — not targets
# shipped in the disk image. They land in PUBLIC/OAK/BIN/I386 so nmake
# rules can invoke them by bare name.
install_host_tool() {
    local built="$1"
    local name="$2"
    if [ -f "$built" ]; then
        cp "$built" "$NT_ROOT/PUBLIC/OAK/BIN/I386/$name"
        # Ensure wibo-tools has a symlink — the tool may not have existed
        # when wibo-tools was first provisioned (MC.EXE and RC.EXE are
        # built from source and only appear after their build step).
        ln -srf "$NT_ROOT/PUBLIC/OAK/BIN/I386/$name" "$WIBO_TOOLS/$name"
        echo ">>> installed $name"
    else
        echo "!!! $name: expected output $built not found" >&2
        return 1
    fi
}
# LINK (linker). Rebuilt from source to:
#   - Compile with -DNDEBUG (bufio.c:2134 assert compiles out)
#   - Include errmsg.i STRINGTABLE resources via UMRES= (error messages)
# Dependencies: PDB/DBI, CVTOMF, DISASM, DISASM68.
build_link() {
    local link_dir="$NT_ROOT/PRIVATE/SDKTOOLS/VCTOOLS/LINK"
    local pdb_dir="$NT_ROOT/PRIVATE/SDKTOOLS/VCTOOLS/PDB"
    echo "========================================"
    echo "Building: LINK.EXE (patched for wibo)"
    echo "========================================"
    run_nmake "$pdb_dir/DBI"        "pdb/dbi.lib"           || return 1
    run_nmake "$link_dir/CVTOMF"    "link/cvtomf.lib"       || return 1
    run_nmake "$link_dir/DISASM"    "link/disasm.lib"       || return 1
    run_nmake "$link_dir/DISASM68"  "link/disasm68.lib"     || return 1
    run_nmake "$link_dir/COFF"      "link/coff (link.exe)"  || return 1
    install_host_tool "$link_dir/COFF/obj/i386/link.exe" "LINK.EXE"
    # Refresh wibo-tools symlink.
    ln -srf "$NT_ROOT/PUBLIC/OAK/BIN/I386/LINK.EXE" "$WIBO_TOOLS/LINK.EXE"
    echo ">>> LINK.EXE rebuilt with error message resources"
}

# MC (message compiler). Two patches live in our source tree relative to
# stock NT 3.5 mc.c:
#   - MC.C drops user32!CharToOem (wibo has no stub; safe for ASCII .mc).
#   - MCUTIL.C hardcodes CodePage=437 (wibo has no GetOEMCP).
#
# We *cannot* drive this build through nmake: nmake's LINK invocation uses
# -MERGE:, -SECTION:, -base:@coffbase.txt,usermode and -debug:MINIMAL, one
# of which trips an internal assertion in LINK.EXE's bufio.c:2134 under
# wibo (not reproduced with minimal flags). So we compile + link directly
# via our own run_wibo_tool helpers with the minimum flags needed.
# cmd-stub: minimal cmd.exe replacement used as NMAKE's COMSPEC. Compiles
# src/cmd-stub/cmd.c under wibo via CL + LINK (self-hosted; no mingw or
# wine required). Output lands in wibo-tools/cmd.exe so the COMSPEC env
# var in NT_ENV_ARR resolves to it.
build_cmdstub() {
    local src_dir="$SCRIPT_DIR/cmd-stub"
    if [ ! -f "$src_dir/cmd.c" ]; then
        echo "ERROR: cmd-stub source not found at $src_dir/cmd.c"
        return 1
    fi

    echo "========================================"
    echo "Building: cmd-stub (NMAKE COMSPEC replacement)"
    echo "========================================"

    rm -f "$src_dir/cmd.obj" "$src_dir/cmd.exe"
    # Use inline env — we can't call run_wibo_tool yet; COMSPEC isn't wired
    # until after cmd.exe exists. CL's combined compile+link via -link
    # handles both stages in a single wibo invocation.
    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "INCLUDE=${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC;${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC\\CRT" \
        "LIB=${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386" \
        "PATH=${WIBO_TOOLS_WIN}" \
        "WIBO_PATH=${WIBO_TOOLS}" \
        "$WIBO_BIN" --chdir "$src_dir" \
            "${WIBO_TOOLS}/CL.EXE" -nologo cmd.c \
            -link -subsystem:console -out:cmd.exe \
            "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\libc.lib" \
            "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\kernel32.lib" \
        || { echo ">>> cmd-stub: FAILED"; return 1; }

    cp "$src_dir/cmd.exe" "$WIBO_TOOLS/cmd.exe"
    echo ">>> cmd-stub: $(ls -l "$WIBO_TOOLS/cmd.exe" | awk '{print $5}') bytes -> wibo-tools/cmd.exe"
}

build_mc() {
    local mc_dir="$NT_ROOT/PRIVATE/SDKTOOLS/VCTOOLS/MC"
    local obj_dir="$mc_dir/obj/i386"

    echo "========================================"
    echo "Building: MC - message compiler (patched for wibo)"
    echo "========================================"

    mkdir -p "$obj_dir"

    # Flags mirror what nmake would pass through MAKEFILE.DEF, minus anything
    # that would only matter at link time. WIN32_LEAN_AND_MEAN keeps objbase
    # / OLE off the windows.h dependency chain.
    local cflags=(
        -nologo -c
        -I .  # mc.h is in the source dir and #included with angle brackets
        -D_X86_=1 -Di386=1 -DWIN32_LEAN_AND_MEAN=1 -DWIN32=100
        -DCOMMAND=1 -DENABLE_NLS=0
        -DUNICODE -D_UNICODE
        -DSTD_CALL -DCONDITION_HANDLING=1
        -DDBG=0 -DDEVL=1
    )

    local src
    for src in mc mclex mcparse mcout mcutil; do
        echo ">>> CL $src.c"
        run_wibo_tool "$mc_dir" CL "${cflags[@]}" \
            -Fo"obj/i386/$src.obj" "$src.c" || return 1
    done

    echo ">>> LINK mc.exe"
    run_wibo_tool "$mc_dir" LINK -nologo -subsystem:console -machine:i386 \
        -out:obj/i386/mc.exe \
        obj/i386/mc.obj obj/i386/mclex.obj obj/i386/mcparse.obj \
        obj/i386/mcout.obj obj/i386/mcutil.obj \
        user32.lib libc.lib kernel32.lib advapi32.lib || return 1

    install_host_tool "$obj_dir/mc.exe" "MC.EXE"
    # wibo-tools/MC.EXE is a symlink into OAK/BIN/I386; the install above
    # overwrote the target, so the symlink now resolves to our build.
    echo ">>> $(basename "$obj_dir/mc.exe"): $(ls -l "$obj_dir/mc.exe" | awk '{print $5}') bytes"
}
build_rc() {
    local rcdll_dir="$NT_ROOT/PRIVATE/SDKTOOLS/VCTOOLS/RCDLL"
    local rc_dir="$NT_ROOT/PRIVATE/SDKTOOLS/VCTOOLS/RC"
    echo "========================================"
    echo "Building: RC.EXE + RCDLL.DLL (resource compiler from source)"
    echo "========================================"
    run_nmake "$rcdll_dir" "RCDLL - rcdll.dll" makedll=1 || return 1
    KEEP_UMAPPL=1 run_nmake "$rc_dir" "RC - rc.exe" || return 1
    install_host_tool "$rcdll_dir/obj/i386/rcdll.dll" "RCDLL.DLL"
    install_host_tool "$rc_dir/obj/i386/rc.exe" "RC.EXE"
}
# --- Drivers (input + video) -------------------------------------------------
# Input: PS/2 port driver (i8042prt) sits under the class drivers
# (kbdclass + mouclass). kbdclass/mouclass are the public NT driver
# interface; i8042prt is the hardware-specific back-end.
build_i8042prt() { run_nmake "$NTOS/DD/I8042PRT" "I8042PRT - PS/2 port driver (kb + mouse)"; }
build_kbdclass() { run_nmake "$NTOS/DD/KBDCLASS" "KBDCLASS - keyboard class driver"; }
build_mouclass() { run_nmake "$NTOS/DD/MOUCLASS" "MOUCLASS - mouse class driver"; }

# Video: videoprt.sys is the common miniport framework that VGA.SYS
# (and all other video drivers in real NT) links against. Build order
# matters — videoprt first because vga imports videoprt.lib.
build_videoprt()    { run_nmake "$NTOS/VIDEO/PORT" "VIDEOPRT - video miniport framework" makedll=1; }
build_vga_miniport(){
    build_videoprt
    run_nmake "$NTOS/VIDEO/VGA" "VGA - VGA miniport driver"
}
build_bochsvga(){
    build_videoprt
    run_nmake "$NTOS/VIDEO/BOCHSVGA" "BOCHSVGA - Bochs VGA miniport (QEMU stdvga)"
}

# --- Virtio shared library --------------------------------------------------
# Shared bus + ring + PCI legacy transport. Linked into all virtio device
# drivers (viorng.sys, vioser.sys, future virtionet.sys, ...). Adapted
# from Unikraft (BSD-3); algorithms from the virtio spec.
build_virtio() { run_nmake "$NTOS/VIRTIO" "VIRTIO - bus + ring + PCI legacy (virtio.lib)"; }

# Virtio device drivers — each links against virtio.lib.
build_viorng()   { run_nmake "$NTOS/DD/VIORNG"   "VIORNG - virtio-rng entropy driver"; }
build_vioser()   { run_nmake "$NTOS/DD/VIOSER"   "VIOSER - virtio-console driver"; }
build_vioinput() { run_nmake "$NTOS/DD/VIOINPUT" "VIOINPUT - virtio-input keyboard/mouse driver"; }

# --- Userland (native NT) ----------------------------------------------------

build_rtl_user() {
    _ensure_error_h
    # TARGETPATH=..\obj puts rtl.lib at RTL/obj/i386/ — ensure it exists.
    mkdir -p "$NTOS/RTL/obj/i386"
    run_nmake "$NTOS/RTL/USER" "RTL_USER - user-mode runtime library"
}
build_ntdll()  {
    # DAYTONA build dir needs an i386 subdir for generated usrstubs.asm.
    mkdir -p "$NTOS/DLL/DAYTONA/i386"
    # makedll=1 tells MAKEFILE.DEF to actually link the DLL (not just import lib)
    run_nmake "$NTOS/DLL/DAYTONA" "NTDLL - user-mode runtime library" makedll=1
}
build_urtl()   { run_nmake "$NT_ROOT/PRIVATE/URTL" "URTL - native-app startup library (nt.lib)"; }

# --- INIT: links all libs into NTOSKRNL.EXE ---

build_hal_stubs() {
    # Build hal.lib + hal.exp from HAL.SRC before INIT so NTOSKRNL.EXE's
    # link can resolve Ke*Irql and other Hal* imports. The stubs-only
    # part of HAL's build is driven by MAKEFILE.INC's lib rule, which
    # runs lib -def:hal.def obj\i386\*.obj. We invoke just that rule.
    local hal_dir="$NTOS/NTHALS/HAL"
    mkdir -p "$hal_dir/obj/i386"
    # Running full nmake here is fine — it'll also compile the HAL objs
    # (needed by lib -def for @N decoration). Link happens later in build_hal.
    run_nmake "$hal_dir" "HAL - stubs lib (for ntoskrnl link)"
}

build_init() {
    build_hal_stubs || return 1
    _ensure_bugcodes || return 1
    # INIT is special: NTTEST=ntoskrnl builds the kernel EXE via MAKEFILE.DEF
    # We must NOT override NTTEST for this component
    local linux_dir="$NTOS/INIT/UP"
    local desc="INIT - NTOSKRNL.EXE"

    echo "========================================"
    echo "Building: $desc"
    echo "========================================"

    mkdir -p "$linux_dir/obj/i386"
    python3 "$SCRIPT_DIR/tools/gen_objects.py" "$linux_dir"

    local rel_path="${linux_dir#$NT_ROOT}"
    local makedir_win="${NT_ROOT_WIN}$(echo "$rel_path" | sed 's|/|\\|g')"

    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "${NT_ENV_ARR[@]}" "MAKEDIR=${makedir_win}" \
        "$WIBO_BIN" --chdir "$linux_dir" \
            "${WIBO_TOOLS}/NMAKE.EXE" /NOLOGO UMTEST= UMAPPL=

    local rc=$?

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

    # Link HAL.DLL (no RC file — no version resources needed)
    run_wibo_tool "$hal_dir" link \
        -OUT:obj\\i386\\hal.dll -DLL -MACHINE:i386 -BASE:0x80400000 \
        -SUBSYSTEM:NATIVE -ENTRY:HalInitSystem@8 -NODEFAULTLIB \
        -RELEASE -DEBUG:MINIMAL -DEBUGTYPE:COFF -OPT:REF \
        obj\\i386\\*.obj \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\ntoskrnl.lib" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\libcntpr.lib" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\int64.lib" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\hal.exp"

    if [ -f "$hal_dir/obj/i386/hal.dll" ]; then
        echo ">>> HAL - MicroNT HAL (DLL): OK"
        ls -la "$hal_dir/obj/i386/hal.dll"
    else
        echo ">>> HAL - MicroNT HAL (DLL): FAILED"
        return 1
    fi
}

# --- Main ---

# Self-bootstrap: if cmd-stub hasn't been built yet, do it now. Safe to call
# at every invocation — no-op when the EXE is already present and fresh.
if [ ! -f "$WIBO_TOOLS/cmd.exe" ] \
   || [ "$SCRIPT_DIR/cmd-stub/cmd.c" -nt "$WIBO_TOOLS/cmd.exe" ]; then
    build_cmdstub || exit $?
fi

# Parse flags.
while [[ "${1:-}" == -* ]]; do
    case "$1" in
        --debug) export WIBO_DEBUG=1; shift ;;
        *) echo "Unknown flag: $1"; exit 1 ;;
    esac
done

# --- Group targets -----------------------------------------------------------
#
# Adding a new component: add its build_foo function above, then add it to
# exactly one of the arrays below. `all` is just the union.
#
# Order matters within each array (deps build first).

# Single-profile target split: kernel + drivers + native NT userland +
# cr (LuaJIT) runtime. No Win32 subsystem, no csrss, no winlogon, no
# user32/gdi32. The kernel spawns run.exe directly via Control\Init\Exe
# (see NTOS/INIT/INIT.C) and everything above that is Lua.
#
# Order inside each array matters — deps first.

# Build toolchain: the irreducible wibo-hosted tools every NT build
# needs. Must run before any component that invokes them via nmake
# rules. LINK (linker, used by everything), MC (message compiler,
# .mc → .bin/.h/.rc), RC (resource compiler, .rc → PE .rsrc section).
# None are Win32-specific — they're PE/build infrastructure, used by
# the kernel itself as much as any userland component.
TOOL_TARGETS=(
    link
    mc
    rc
)

NTOSKRNL_TARGETS=(
    geni386
    ke rtl ex ob se ps mm cache config lpc dbgk io kd fsrtl raw vdm
    init
    hal
)

# Every driver we ship. Core storage/FS/COM plus the input + video
# stack the eventual pure-Lua UI will drive directly via
# NtDeviceIoControlFile on \Device\{KeyboardClass0,PointerClass0}
# and the framebuffer miniport. virtio is a shared static library
# (virtio.lib) that future virtio device drivers link against — built
# here so dependents always see a current copy.
DRIVER_TARGETS=(
    atdisk null fastfat npfs msfs serial
    i8042prt kbdclass mouclass
    vga_miniport bochsvga
    virtio
    viorng
    vioser
    vioinput
)

# Userland: just the native-NT runtime. rtl_user (user-mode RTL),
# ntdll.dll (native NT syscall surface), urtl (nt.lib — startup +
# CRT shape for native binaries). No kernel32, no csrss, no Win32
# subsystem of any kind. run.exe links against these and the kernel
# spawns it directly via Control\Init\Exe.
USERLAND_TARGETS=(
    rtl_user
    ntdll
    urtl
)

build_group() {
    local group_name="$1"; shift
    echo ""
    echo "########################################"
    echo "# Group: $group_name"
    echo "########################################"
    for t in "$@"; do
        "build_$t"
    done
}

build_tools()    { build_group tools    "${TOOL_TARGETS[@]}"; }
build_ntoskrnl() { build_group ntoskrnl "${NTOSKRNL_TARGETS[@]}"; }
build_drivers()  { build_group drivers  "${DRIVER_TARGETS[@]}"; }
build_userland() { build_group userland "${USERLAND_TARGETS[@]}"; }

build_efi() {
    echo ""
    echo "========================================"
    echo "Building: UEFI bootloader (BOOTX64.EFI)"
    echo "========================================"
    make -C "$SCRIPT_DIR/boot-efi" BOOTX64.EFI
}

# cr — LuaJIT-on-NT runtime. The kernel spawns run.exe (native
# subsystem, imports ntdll only) directly via Control\Init\Exe; runc
# and runw still build for developer convenience but are no longer
# wired into any boot path. The full Lua tree at \SystemRoot\lua\ is
# staged by mkdisk's _lua_tree_files().
build_cr() {
    echo ""
    echo "========================================"
    echo "Building: cr (LuaJIT runtime + lua/ tree)"
    echo "========================================"
    make -C "$SCRIPT_DIR/cr"
}

build_disk() {
    local out_dir="$(dirname "$SCRIPT_DIR")/build/disk"
    local efi_bin="$SCRIPT_DIR/boot-efi/BOOTX64.EFI"

    echo ""
    echo "========================================"
    echo "Building boot disk image"
    echo "========================================"

    # Build EFI loader if not already present.
    if [ ! -f "$efi_bin" ]; then
        build_efi
    fi

    mkdir -p "$out_dir"
    python3 "$SCRIPT_DIR/tools/mkhive.py" "$out_dir/SYSTEM"
    python3 "$SCRIPT_DIR/tools/mkdisk.py" \
        --output-dir "$out_dir" --efi-binary "$efi_bin"
}

# Compile + assemble. Single flow — there's only one profile now.
build_all() {
    build_tools
    build_ntoskrnl
    build_drivers
    build_userland
    build_cr
    build_disk
}

# --- Main dispatch -----------------------------------------------------------

_dispatch_one() {
    local comp="$1"
    case "$comp" in
        all)               build_all ;;
        tools)             build_tools ;;
        ntoskrnl)          build_ntoskrnl ;;
        drivers)           build_drivers ;;
        userland)          build_userland ;;
        disk)              build_disk ;;
        *)
            if declare -F "build_$comp" > /dev/null; then
                "build_$comp"
            else
                echo "Unknown component: $comp"
                echo ""
                echo "Top-level targets: all, tools, ntoskrnl, drivers,"
                echo "                   userland, cr, efi, disk"
                echo ""
                echo "Individual components (in build order):"
                echo "  tools:     ${TOOL_TARGETS[*]}"
                echo "  ntoskrnl:  ${NTOSKRNL_TARGETS[*]}"
                echo "  drivers:   ${DRIVER_TARGETS[*]}"
                echo "  userland:  ${USERLAND_TARGETS[*]}"
                exit 1
            fi
            ;;
    esac
}

if [ $# -eq 0 ]; then
    _dispatch_one all
else
    for comp in "$@"; do
        _dispatch_one "$comp"
    done
fi
