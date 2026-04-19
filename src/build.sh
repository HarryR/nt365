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
# UMAPPL builds (like gensrv, smss), set `KEEP_UMAPPL=1` in the caller env
# to preserve the SOURCES file's UMAPPL= directive.
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
build_atdisk() { run_nmake "$NTOS/DD/HARDDISK" "ATDISK - IDE disk driver"; }
build_null()   { run_nmake "$NTOS/DD/NULL"     "NULL - null device driver"; }
build_fastfat(){ run_nmake "$NTOS/FASTFAT"     "FASTFAT - FAT filesystem driver"; }
build_npfs()   { run_nmake "$NTOS/NPFS"       "NPFS - Named Pipe filesystem driver"; }
build_msfs()   { run_nmake "$NTOS/MAILSLOT"  "MSFS - Mailslot filesystem driver"; }
build_hello()  { run_nmake "$NTOS/DD/HELLO"    "HELLO - MicroNT visibility driver"; }

# --- RPC stack ---------------------------------------------------------------
# NT 3.5's RPC runtime is 180k LoC across NDRLIB + NDR20 + RUNTIME.
# Builds in dependency order: NDRLIB (NDR marshaling primitives, the
# smallest piece) → NDR20 (NDR 2.0 client/server stub support) →
# RUNTIME (full rpcrt4.dll with transports + endpoint mapper).
#
# rpcndrp.lib is the "ndr private" lib linked into rpcrt4.dll itself.
build_rpcndrp() { run_nmake "$NT_ROOT/PRIVATE/RPC/NDRLIB" "RPC/NDRLIB - NDR marshaling primitives"; }
build_rpcndr()  { run_nmake "$NT_ROOT/PRIVATE/RPC/NDRMEM" "RPC/NDRMEM - NDR 1.0 stub helpers (rpcndr.lib)"; }
build_rpcndr20(){ run_nmake "$NT_ROOT/PRIVATE/RPC/NDR20"  "RPC/NDR20 - NDR 2.0 client/server support"; }
build_rpcrt4_idls() {
    # Run our home-bootstrapped midl on the RTIFS interfaces. Generates
    # {nbase,conv,epmp,mgmt}.h + _c.c + _s.c. Order matters: conv/epmp/mgmt
    # are processed first while nbase.h does not yet exist in the RTIFS dir,
    # so midl falls back to ../mtrt/nbase.h (the hand-written, properly-
    # guarded one). nbase.idl is processed last to emit its own nbase.h.
    local rtifs="$NT_ROOT/PRIVATE/RPC/RUNTIME/RTIFS"
    echo ">>> RPC/RTIFS midl: generating conv/epmp/mgmt/nbase stubs"
    rm -f "$rtifs"/nbase.h "$rtifs"/{conv,epmp,mgmt}.h "$rtifs"/{conv,epmp,mgmt}_{c,s}.c
    for idl in conv epmp mgmt nbase; do
        run_wibo_tool "$rtifs" midl /ms_ext /c_ext /app_config /D MIDL_PASS /I ..\\mtrt "$idl.idl" \
            || { echo "!!! RPC/RTIFS midl gen failed ($idl)"; return 1; }
    done
    # Drop generated headers + stubs into MTRT (where SOURCES expects them).
    # MTRT keeps its hand-written NBASE.H (has guards midl's doesn't), so we
    # don't copy nbase.h.
    local mtrt="$NT_ROOT/PRIVATE/RPC/RUNTIME/MTRT"
    cp -f "$rtifs"/{conv,epmp,mgmt}.h "$mtrt/"
    cp -f "$rtifs"/{conv,mgmt,epmp}_c.c "$mtrt/"
    cp -f "$rtifs"/{conv,mgmt}_s.c "$mtrt/"
    echo ">>> RPC/RTIFS midl: OK (stubs copied into MTRT)"
}
build_rpcrt4()  {
    build_rpcrt4_idls || return 1
    run_nmake "$NT_ROOT/PRIVATE/RPC/RUNTIME/MTRT" "RPC/RUNTIME/MTRT - rpcrt4.dll (main RPC runtime)" makedll=1
}
build_rpclts1() { run_nmake "$NT_ROOT/PRIVATE/RPC/RUNTIME/TRANS/WIN32/SVRNP" "RPC transport - rpclts1.dll (named pipe server)" makedll=1; }
build_rpcltc1() { run_nmake "$NT_ROOT/PRIVATE/RPC/RUNTIME/TRANS/WIN32/CLNTNP" "RPC transport - rpcltc1.dll (named pipe client)" makedll=1; }

# --- advapi32 stack ----------------------------------------------------------
# advapi32.dll is a façade over four subsystems:
#   - LSA (lsacomm, lsaudll, sys003)
#   - EventLog (elfapi)
#   - SCM (sclib, svcctrl)
#   - Registry (winreg, wrlib, perflib, localreg)
# Built bottom-up: each piece is a static .lib that advapi32 then aggregates.
# Shared MIDL invocation for advapi32-stack interfaces. /D _M_IX86 /D _X86_
# is needed so winnt.h's CONTEXT block becomes visible during midl pass
# (same MIDL_PASS gate as MTRT/RTIFS).
_midl_advapi_idl() {
    local dir="$1"; shift
    local extra_inc="${1:-}"; shift
    local extra_inc_args=()
    # extra_inc may look like "/I inc" — split it into separate tokens.
    if [ -n "$extra_inc" ]; then read -ra extra_inc_args <<<"$extra_inc"; fi
    for idl in "$@"; do
        run_wibo_tool "$dir" midl /ms_ext /c_ext /app_config /D MIDL_PASS /D _M_IX86 /D _X86_ \
            "${extra_inc_args[@]}" "$idl.idl" || return 1
    done
}
build_winreg_idl(){
    # -oldnames so MIDL generates winreg_ServerIfHandle (not winreg_v1_0_s_ifspec)
    # which is what WINREG/SERVER/INIT.C references.
    local dir="$NT_ROOT/PRIVATE/WINDOWS/SCREG/WINREG"
    run_wibo_tool "$dir" midl /ms_ext /c_ext /app_config /D MIDL_PASS /D _M_IX86 /D _X86_ \
        -oldnames regrpc.idl || return 1
}
build_wrlib()    { build_winreg_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/WINREG/LIB"  "WINREG/LIB - wrlib.lib"; }
build_perflib()  { run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/WINREG/PERFLIB" "WINREG/PERFLIB - perflib.lib"; }
build_localreg() { build_winreg_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/WINREG/LOCAL"    "WINREG/LOCAL - localreg.lib"; }
build_winreg()   { build_winreg_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/WINREG/CLIENT"   "WINREG/CLIENT - winreg.lib"; }
build_winregsrv(){ build_winreg_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/WINREG/SERVER"   "WINREG/SERVER - winreg.lib (server)"; }
build_sc_idl()   {
    _midl_advapi_idl "$NT_ROOT/PRIVATE/WINDOWS/SCREG/SC" "/I inc" svcctl || return 1
    # sclib + svcctrl #include <svcctl.h>; their INCLUDES point at SC/INC but
    # midl emits into SC/ — copy the header over.
    cp -f "$NT_ROOT/PRIVATE/WINDOWS/SCREG/SC/svcctl.h" "$NT_ROOT/PRIVATE/WINDOWS/SCREG/SC/INC/"
}
build_sclib()    { build_sc_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/SC/LIB"    "SC/LIB - sclib.lib"; }
build_svcctrl()  { build_sc_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/SCREG/SC/CLIENT" "SC/CLIENT - svcctrl.lib"; }
build_elf_idl()  { _midl_advapi_idl "$NT_ROOT/PRIVATE/EVENTLOG" "" elf; }
build_elfapi()   { build_elf_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/EVENTLOG/ELFCLNT" "EVENTLOG/ELFCLNT - elfapi.lib"; }
build_lsa_idl()  {
    # lsarpc.idl imports lsaimp.idl, which #include's <lsaimp.h> — the
    # shipped header in PRIVATE/INC that pulls in the full NT SDK for
    # its typedefs. Those SDK headers contain C function prototypes with
    # PVOID/HANDLE params; stock MIDL 2.00.71 rejected them, but our
    # patched MIDL silently skips the NON_RPC_PARAM_VOID check when the
    # proc came from an imported file (FRONT/SEMANTIC.CXX). So we can
    # just drive MIDL the way LSA/MAKEFIL0 intended — separate client
    # and server passes with their own ACFs.
    local dir="$NT_ROOT/PRIVATE/LSA"
    local flags=(/D MIDL_PASS /D _M_IX86 /D _X86_ /D _WCHAR_T_DEFINED
        -mode c_port -oldnames -error allocation -error ref /I inc /I ..\\inc)
    run_wibo_tool "$dir" midl "${flags[@]}" -acf lsacli.acf -header lsarpc_c.h lsarpc.idl || return 1
    run_wibo_tool "$dir" midl "${flags[@]}" -acf lsasrv.acf -header lsarpc.h   lsarpc.idl || return 1
    # lsarpc_c.h is consumed by UCLIENT + SERVER via #include "lsarpc_c.h"
    # (LSA/INC is on their /I ..\inc path). lsarpc.h (server header) is
    # pulled via <lsarpc.h> from NEWSAM and other components that talk
    # to the LSA server interface, so drop both into PRIVATE/INC too.
    cp -f "$NT_ROOT/PRIVATE/LSA/lsarpc_c.h" "$NT_ROOT/PRIVATE/LSA/INC/"
    cp -f "$NT_ROOT/PRIVATE/LSA/lsarpc.h"   "$NT_ROOT/PRIVATE/INC/"
}
build_lsacomm()  { build_lsa_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/LSA/COMMON" "LSA/COMMON - lsacomm.lib"; }
build_lsaudll()  { build_lsa_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/LSA/UCLIENT" "LSA/UCLIENT - lsaudll.lib"; }
build_lsadll()   { run_nmake "$NT_ROOT/PRIVATE/LSA/CLIENT/USER" "LSA/CLIENT/USER - lsadll.lib"; }
# LSA crypto. CRYPT/ENGINE shipped only as .OBJ in the original leak
# (export-controlled); DES/RC4/ECB source restored from the nt35_patches
# tree. CRYPT/DLL is the public wrapper exposed as sys003.lib (alias for
# crypt.lib, per its MAKEFILE.INC).
build_crypt_engine() { run_nmake "$NT_ROOT/PRIVATE/LSA/CRYPT/ENGINE" "LSA/CRYPT/ENGINE - engine.lib (DES/RC4/ECB/MD4)"; }
build_sys003()       { build_crypt_engine || return 1; run_nmake "$NT_ROOT/PRIVATE/LSA/CRYPT/DLL" "LSA/CRYPT/DLL - sys003.lib (crypt wrapper)"; }
build_rpcutil()  { run_nmake "$NT_ROOT/PRIVATE/RPCUTIL" "RPC/RPCUTIL - rpcutil.lib (MIDL user helpers)"; }
build_nlrepl()   {
    build_sam_idl || return 1
    run_nmake "$NT_ROOT/PRIVATE/NLSECUTL" "NLSECUTL - nlrepl.lib (NetLogon helpers)"
}
# SAM (Security Account Manager). Dual MIDL pass per NEWSAM/MAKEFIL0 —
# client + server each with their own ACF. samrpc_c.h / samrpc.h are
# consumed by NLSECUTL, LSA/SERVER, NEWSAM/CLIENT and /SERVER.
build_sam_idl()  {
    local dir="$NT_ROOT/PRIVATE/NEWSAM"
    local flags=(/D MIDL_PASS /D _M_IX86 /D _X86_ /D _WCHAR_T_DEFINED
        -mode c_port -oldnames -error allocation -error ref /I ..\\inc)
    run_wibo_tool "$dir" midl "${flags[@]}" -acf samcli.acf -header samrpc_c.h samrpc.idl || return 1
    run_wibo_tool "$dir" midl "${flags[@]}" -acf samsrv.acf -header samrpc.h   samrpc.idl || return 1
    # Drop samrpc headers into PRIVATE/INC where other components expect them.
    cp -f "$NT_ROOT/PRIVATE/NEWSAM/samrpc_c.h" "$NT_ROOT/PRIVATE/INC/"
    cp -f "$NT_ROOT/PRIVATE/NEWSAM/samrpc.h"   "$NT_ROOT/PRIVATE/INC/"
}
build_samlib()   { build_sam_idl || return 1; run_nmake "$NT_ROOT/PRIVATE/NEWSAM/CLIENT" "NEWSAM/CLIENT - samlib.dll" makedll=1; }
build_samsrv()   {
    build_sam_idl || return 1
    build_lsasrv_imports || return 1  # samsrv links against lsasrv.lib
    run_nmake "$NT_ROOT/PRIVATE/NEWSAM/SERVER" "NEWSAM/SERVER - samsrv.dll" makedll=1
}
# LSA/SERVER builds lsasrv.dll + lsass.exe (UMAPPL=lsass in its SOURCES).
# It links against samsrv.lib and samsrv links against lsasrv.lib — a
# circular DLL dep. Break it by pre-generating the import libs from
# their .def files before either DLL gets linked.
# Build an import lib from a .def file. When given an obj dir that's
# already been populated by a compile pass, lib -def picks up the
# stdcall @N decorations from the obj symbols, producing a proper
# decorated import lib. Without the objs, @N is stripped, which
# breaks cross-DLL references for any stdcall-exported function.
_lib_from_def() {
    local libname="$1"
    local def_path="$2"
    local objs_glob="${3:-}"
    # def_path / objs_glob are host abs paths under NT_ROOT; convert to Z:\<...>.
    local defwin libwin
    defwin="$(path_to_win "$def_path")"
    libwin="${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\$libname"
    local obj_args=()
    if [ -n "$objs_glob" ]; then
        # Expand the glob on the host, then translate each to Z:\<...>.
        local f
        for f in $objs_glob; do obj_args+=("$(path_to_win "$f")"); done
    fi
    echo ">>> pre-generating $libname from $(basename "$def_path")${obj_args:+ + objs}"
    run_wibo_tool "$SCRIPT_DIR" lib -nologo -machine:i386 \
        "-def:$defwin" "${obj_args[@]}" "-out:$libwin" \
        || { echo "!!! lib -def on $def_path failed"; return 1; }
}
# The samsrv<->lsasrv circular import is broken by compiling each DLL's
# obj files once (no link), then using `lib -def:X.def obj/*.obj` to
# produce decorated import libs. After that, both DLLs can link.
_nmake_only_compile() {
    # Force nmake to stop at the compile step (before link) by requesting
    # the obj dir as the target. Easiest way: run normal nmake but accept
    # link failure. The obj files are what we care about.
    run_nmake "$1" "$2" 2>&1 | tail -3 || true
}
build_lsasrv_imports() {
    # Force compile-only pass on both DLLs so the obj files exist, then
    # synthesize import libs with proper @N decorations from the objs.
    local sam_obj="$NT_ROOT/PRIVATE/NEWSAM/SERVER/obj/i386/*.obj"
    local lsa_obj="$NT_ROOT/PRIVATE/LSA/SERVER/obj/i386/*.obj"
    if ! compgen -G "$sam_obj" > /dev/null; then
        _nmake_only_compile "$NT_ROOT/PRIVATE/NEWSAM/SERVER" "NEWSAM/SERVER compile-only (pre-imports)"
    fi
    if ! compgen -G "$lsa_obj" > /dev/null; then
        _nmake_only_compile "$NT_ROOT/PRIVATE/LSA/SERVER" "LSA/SERVER compile-only (pre-imports)"
    fi
    _lib_from_def lsasrv.lib "$NT_ROOT/PRIVATE/LSA/SERVER/LSASRV.DEF"    "$lsa_obj" || return 1
    _lib_from_def samsrv.lib "$NT_ROOT/PRIVATE/NEWSAM/SERVER/SAMSRV.DEF" "$sam_obj" || return 1
}
build_lsasrv() {
    build_lsa_idl || return 1
    build_sam_idl || return 1
    # Break the samsrv<->lsasrv circular import by generating both .lib
    # import stubs from .def first.
    build_lsasrv_imports || return 1
    # KEEP_UMAPPL=1 preserves UMAPPL=lsass so the same SOURCES produces
    # both lsasrv.dll (DYNLINK) and lsass.exe (native UMAPPL using
    # lsasrv.dll's entry).
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/LSA/SERVER" "LSA/SERVER - lsasrv.dll + lsass.exe" makedll=1
}
build_advapi32() { build_rpcutil || return 1; run_nmake "$NT_ROOT/PRIVATE/WINDOWS/BASE/ADVAPI" "advapi32.dll" makedll=1; }

# --- Host tools (sdktools bootstrap phase) -----------------------------------
# These are wine-executable host tools consumed by later build steps — not
# targets shipped in the disk image. They land in PUBLIC/OAK/BIN/I386 so
# nmake rules can invoke them by bare name, same pattern as gensrv.
#
# MIDL bootstrap chain (for generating RPC stubs from IDL):
#   midleb    — error-recovery DB generator (MIDLNEW/EREC)
#   midlyacc  — custom yacc (MIDLNEW/YACC, bootstraps via shipped YACCP.EXE)
#   midlpg    — parser post-generator (MIDLNEW/PG)
#   midl      — the MIDL compiler itself (MIDL20/FRONT, links support+expr+
#               analysis+codegen libs). Invoked on RUNTIME/RTIFS/*.idl to
#               generate conv.h / epmp.h / mgmt.h / nbase.h.
install_host_tool() {
    local built="$1"
    local name="$2"
    if [ -f "$built" ]; then
        cp "$built" "$NT_ROOT/PUBLIC/OAK/BIN/I386/$name"
        # Ensure wibo-tools has a symlink — the tool may not have existed
        # when wibo-tools was first provisioned (e.g. MC.EXE, midl, gensrv
        # are built from source and only appear after their build step).
        ln -srf "$NT_ROOT/PUBLIC/OAK/BIN/I386/$name" "$WIBO_TOOLS/$name"
        echo ">>> installed $name"
    else
        echo "!!! $name: expected output $built not found" >&2
        return 1
    fi
}
build_midleb() {
    run_nmake "$NT_ROOT/PRIVATE/RPC/MIDLNEW/EREC" "MIDL/EREC - error-recovery DB generator (midleb.exe)"
    install_host_tool "$NT_ROOT/PRIVATE/RPC/MIDLNEW/EREC/obj/i386/midleb.exe" "midleb.exe"
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
build_midlyacc() {
    run_nmake "$NT_ROOT/PRIVATE/RPC/MIDLNEW/YACC" "MIDL/YACC - custom yacc (midlyacc.exe)"
    install_host_tool "$NT_ROOT/PRIVATE/RPC/MIDLNEW/YACC/obj/i386/midlyacc.exe" "midlyacc.exe"
}
build_midlpg() {
    run_nmake "$NT_ROOT/PRIVATE/RPC/MIDLNEW/PG" "MIDL/PG - parser post-generator (midlpg.exe)"
    install_host_tool "$NT_ROOT/PRIVATE/RPC/MIDLNEW/PG/obj/i386/midlpg.exe" "midlpg.exe"
}
# MIDL20 static libs (link-time deps of midl.exe). Each has TARGETPATH=..\lib
# so outputs land at MIDL20/lib/i386/{support,exprlib,analysis,codegen}.lib.
_midl20_lib_prep() { mkdir -p "$NT_ROOT/PRIVATE/RPC/MIDL20/lib/i386"; }
build_midl_support() { _midl20_lib_prep; run_nmake "$NT_ROOT/PRIVATE/RPC/MIDL20/SUPPORT"  "MIDL20/SUPPORT - support.lib"; }
build_midl_expr()    { _midl20_lib_prep; run_nmake "$NT_ROOT/PRIVATE/RPC/MIDL20/EXPR"     "MIDL20/EXPR - exprlib.lib"; }
build_midl_analysis(){ _midl20_lib_prep; run_nmake "$NT_ROOT/PRIVATE/RPC/MIDL20/ANALYSIS" "MIDL20/ANALYSIS - analysis.lib"; }
build_midl_codegen() { _midl20_lib_prep; run_nmake "$NT_ROOT/PRIVATE/RPC/MIDL20/CODEGEN"  "MIDL20/CODEGEN - codegen.lib"; }

# FRONT pre-generation: midlyacc → midlpg → midleb. Originally driven by
# MAKEFILE.INC inside nmake, but the rules used `qgrep` (resource-kit grep)
# to strip #line directives. We patched midlyacc to gate #line behind a -L
# flag (default off), so the pipeline is now: yacc → pg → midleb, no filter.
# Generates grammar.cxx, acfgram.cxx (compiled by FRONT) and
# include/{idlerec.h, acferec.h} (consumed by FRONT sources).
_midl_front_gen() {
    local front="$NT_ROOT/PRIVATE/RPC/MIDL20/FRONT"
    # Use existing uppercase INCLUDE — Linux is case-sensitive and wibo's path
    # resolver does a case-insensitive fallback, so writing through an
    # uppercase INCLUDE dir avoids duplicate-dir confusion.
    local inc="$NT_ROOT/PRIVATE/RPC/MIDL20/INCLUDE"
    echo ">>> MIDL/FRONT gen: midlyacc + midlpg + midleb"
    # midlyacc emits FOO.C/FOO.H/FOO.I (uppercase). Case-insensitive FS lets
    # cl pick up FOO.C when SOURCES says foo.cxx, so we delete the .C/.H/.I
    # after midlpg consumes them.
    run_wibo_tool "$front" midlyacc -his -t "YYSTATIC " grammar.y       || return 1
    run_wibo_tool "$front" midlpg grammar.C   > "$front/grammar.cxx"    || return 1
    run_wibo_tool "$front" midleb - xlatidl.dat IDL > "$inc/idlerec.h"  || return 1
    # Keep grammar.h (lex.cxx includes it) by moving into INCLUDE; drop .C/.I
    # so cl doesn't pick up grammar.C instead of grammar.cxx.
    mv -f "$front/grammar.H" "$inc/grammar.h" && rm -f "$front/grammar.C" "$front/grammar.I"
    run_wibo_tool "$front" midlyacc -hi  -t "YYSTATIC " acfgram.y       || return 1
    run_wibo_tool "$front" midlpg acfgram.C  > "$front/acfgram.cxx"     || return 1
    run_wibo_tool "$front" midleb - xlatacf.dat ACF > "$inc/acferec.h"  || return 1
    mv -f "$front/acfgram.H" "$inc/acfgram.h" && rm -f "$front/acfgram.C" "$front/acfgram.I"
    echo ">>> MIDL/FRONT gen: OK (grammar.cxx, acfgram.cxx, idlerec.h, acferec.h)"
}
build_midl() {
    # FRONT links against all four static libs — build them first.
    build_midl_support  || return 1
    build_midl_expr     || return 1
    build_midl_analysis || return 1
    build_midl_codegen  || return 1
    _midl_front_gen     || return 1
    run_nmake "$NT_ROOT/PRIVATE/RPC/MIDL20/FRONT" "MIDL20/FRONT - midl.exe (compiler driver)"
    install_host_tool "$NT_ROOT/PRIVATE/RPC/MIDL20/lib/i386/midl.exe" "midl.exe"
}

# --- GUI-side drivers (input + video) ----------------------------------------
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
build_framebuf(){
    run_nmake "$GDI/DISPLAYS/FRAMEBUF" "FRAMEBUF - generic framebuffer display driver" makedll=1
}
# --- GUI userland (USER + GDI + console + winsrv + winlogon) ----------------
#
# GDI dependency chain: efloat (FP math) + font drivers (fscaler, ttfd, bmfd,
# vtfd) + halftone → gdisrvl.lib (GRE engine) → gdi32.dll (client).
# USER: usersrvl.lib (server) → user32.dll (client).
# Console: consrvl.lib (server).
# winsrv.dll aggregates usersrvl + gdisrvl + consrvl + basesrv.

GDI="$NT_ROOT/PRIVATE/WINDOWS/GDI"
USER="$NT_ROOT/PRIVATE/WINDOWS/USER"

_ensure_user_headers() {
    # Generate USER client/server dispatch files from .TPL + .LST via listmung.
    local inc="$USER/INC"
    local svr="$USER/SERVER"
    local cli="$USER/CLIENT"

    if [ ! -f "$inc/callback.h" ] || [ "$inc/CB.LST" -nt "$inc/callback.h" ]; then
        echo ">>> listmung: generating USER dispatch headers + source"
        # listmung writes to stdout — redirect to target file.
        # INC headers
        run_wibo_tool "$inc" listmung CB.LST CALLBACK.TPL  > "$inc/callback.h" || return 1
        run_wibo_tool "$inc" listmung CF.LST CSUSER.TPL    > "$inc/csuser.h"   || return 1
        run_wibo_tool "$inc" listmung SCF.LST CSCALL.TPL   > "$inc/cscall.h"   || return 1
        # SERVER generated .c files
        run_wibo_tool "$svr" listmung ..\\inc\\CF.LST DISPCF.TPL  > "$svr/dispcf.c"  || return 1
        run_wibo_tool "$svr" listmung ..\\inc\\SCF.LST CALLCF.TPL > "$svr/callcf.c"  || return 1
        # CLIENT generated .c files
        run_wibo_tool "$cli" listmung ..\\inc\\CB.LST DISPCB.TPL  > "$cli/dispcb.c"   || return 1
    fi
}

build_gdi_efloat()   { run_nmake "$GDI/MATH"               "GDI/MATH - efloat.lib (FP for GDI)"; }
build_gdi_fscaler()  { run_nmake "$GDI/FONDRV/TT/SCALER"   "GDI/FONDRV/TT/SCALER - fscaler.lib"; }
build_gdi_ttfd()     { run_nmake "$GDI/FONDRV/TT/TTFD"     "GDI/FONDRV/TT/TTFD - ttfd.lib"; }
build_gdi_bmfd()     { run_nmake "$GDI/FONDRV/BMFD"        "GDI/FONDRV/BMFD - bmfd.lib"; }
build_gdi_vtfd()     { run_nmake "$GDI/FONDRV/VTFD"        "GDI/FONDRV/VTFD - vtfd.lib"; }
build_gdi_halftone() { run_nmake "$GDI/HALFTONE/HT"        "GDI/HALFTONE - halftone.lib"; }

build_gdi_geni386() {
    echo "========================================"
    echo "Building: GDI GENI386 (struct offset generator)"
    echo "========================================"

    local gre="$GDI/GRE"
    local geni_src="$gre/I386/GENI386.CXX"
    local obj_dir="$gre/obj/i386"
    local out_inc="$GDI/INC/GDII386.INC"
    mkdir -p "$obj_dir"

    if [ ! -f "$geni_src" ]; then
        echo "ERROR: GENI386.CXX not found at $geni_src"
        return 1
    fi

    local obj_dir_win; obj_dir_win="$(path_to_win "$obj_dir")"
    local out_inc_win; out_inc_win="$(path_to_win "$out_inc")"

    # Compile with the same flags as gdisrvl — same include paths, same
    # defines, same packing — so OFFSET() produces the same values the
    # engine objects see.
    run_wibo_tool "$gre" cl386 \
        -nologo -c -Zp8 -Gz \
        -Di386=1 -D_X86_=1 -DNT_UP=1 -DSTD_CALL \
        -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNOICM \
        -DDBG=0 -DDEVL=1 -DPRECOMPILED_GRE -DWIN32=100 -D_NT1X_=100 \
        "-I." \
        "-I..\\inc" \
        "-I..\\..\\inc" \
        "-I${NT_ROOT_WIN}\\PUBLIC\\OAK\\INC" \
        "-I${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC" \
        "-I${NT_ROOT_WIN}\\PUBLIC\\SDK\\INC\\CRT" \
        "-I${NT_ROOT_WIN}\\PRIVATE\\WINDOWS\\GDI\\MATH\\I386" \
        "I386\\GENI386.CXX" \
        "-Fo${obj_dir_win}\\gdi_geni386.obj" || return 1

    run_wibo_tool "$gre" link \
        -nologo -subsystem:console \
        "-out:${obj_dir_win}\\gdi_geni386.exe" \
        "${obj_dir_win}\\gdi_geni386.obj" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\LIBC.LIB" \
        "${NT_ROOT_WIN}\\PUBLIC\\SDK\\LIB\\I386\\KERNEL32.LIB" || return 1

    env -i HOME="$HOME" TERM="${TERM:-dumb}" ${WIBO_DEBUG:+"WIBO_DEBUG=$WIBO_DEBUG"} \
        "${NT_ENV_ARR[@]}" \
        "$WIBO_BIN" --chdir "$gre" \
            "$obj_dir/gdi_geni386.exe" \
            "$out_inc_win" || return 1

    echo ">>> GDI GENI386: $out_inc regenerated"
}

build_gdisrv() {
    build_gdi_geni386 || return 1
    run_nmake "$GDI/GRE"                "GDI/GRE - gdisrvl.lib (GDI engine)"
}

# user32 ↔ gdi32 have a circular import dependency (user32 links
# gdi32p.lib, gdi32 links user32p.lib). Break the cycle by pre-
# generating both private import libs from their DEF files before
# either DLL links — same pattern as samsrv ↔ lsasrv.
build_gui_import_stubs() {
    # user32 ↔ gdi32 circular import. Compile both to .obj (no link),
    # then generate decorated import libs from DEF + objs. Same pattern
    # as build_lsasrv_imports for samsrv ↔ lsasrv.
    _ensure_user_headers
    echo ">>> GUI import stubs: compile-only pass"
    local user_obj="$USER/CLIENT/obj/i386/*.obj"
    local gdi_obj="$GDI/CLIENT/obj/i386/*.obj"
    if ! compgen -G "$user_obj" > /dev/null 2>&1; then
        run_nmake "$USER/CLIENT" "USER/CLIENT compile-only (pre-imports)" NTTARGETFILE0= NTTARGETFILE1= 2>&1 | tail -3 || true
    fi
    if ! compgen -G "$gdi_obj" > /dev/null 2>&1; then
        run_nmake "$GDI/CLIENT" "GDI/CLIENT compile-only (pre-imports)" NTTARGETFILE0= NTTARGETFILE1= 2>&1 | tail -3 || true
    fi
    echo ">>> GUI import stubs: generating user32p.lib + gdi32p.lib"
    # NT generates user32p.def from USER32.DEF via `cl -EP -DPRIVATE=`,
    # stripping the PRIVATE keyword so those exports become normal import
    # stubs. Without this, `lib -def:` skips PRIVATE exports entirely.
    sed 's/  *PRIVATE\b//' "$USER/CLIENT/USER32.DEF" > "$USER/CLIENT/user32p.def"
    _lib_from_def user32p.lib "$USER/CLIENT/user32p.def" "$user_obj" || return 1
    _lib_from_def gdi32p.lib "$GDI/CLIENT/gdi32p.def"  "$gdi_obj"  || return 1
}

build_gdi32() {
    build_gui_import_stubs || return 1
    local saved
    for i in "${!NT_ENV_ARR[@]}"; do
        if [[ "${NT_ENV_ARR[$i]}" == "NTDEBUG=" ]]; then
            saved=$i
            NT_ENV_ARR[$i]="NTDEBUG=sym"
        fi
    done
    run_nmake "$GDI/CLIENT" "GDI/CLIENT - gdi32.dll" makedll=1
    [ -n "${saved:-}" ] && NT_ENV_ARR[$saved]="NTDEBUG="
}

build_usersrv() {
    _ensure_user_headers
    run_nmake "$USER/SERVER" "USER/SERVER - usersrvl.lib"
}
build_user32() {
    build_gui_import_stubs || return 1
    local saved
    for i in "${!NT_ENV_ARR[@]}"; do
        if [[ "${NT_ENV_ARR[$i]}" == "NTDEBUG=" ]]; then
            saved=$i
            NT_ENV_ARR[$i]="NTDEBUG=sym"
        fi
    done
    run_nmake "$USER/CLIENT" "USER/CLIENT - user32.dll" makedll=1
    [ -n "${saved:-}" ] && NT_ENV_ARR[$saved]="NTDEBUG="
}

build_consrv()       { run_nmake "$NT_ROOT/PRIVATE/WINDOWS/WINCON/SERVER/DAYTONA" "WINCON/SERVER - consrvl.lib"; }

build_winsrv() {
    mkdir -p "$NT_ROOT/PRIVATE/WINDOWS/WINSRV/DAYTONA/obj/i386"
    run_nmake "$NT_ROOT/PRIVATE/WINDOWS/WINSRV/DAYTONA" "WINSRV - winsrv.dll (USER+GDI+console aggregator)" makedll=1
}

build_winlogon() {
    KEEP_UMAPPL=1 run_nmake "$USER/WINLOGON/DAYTONA" "WINLOGON - winlogon.exe"
}

build_userinit() {
    KEEP_UMAPPL=1 run_nmake "$USER/USERINIT" "USERINIT - userinit.exe"
}

build_listmung() {
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/SDKTOOLS/LISTMUNG" "LISTMUNG - template list expander"
    install_host_tool "$NT_ROOT/PRIVATE/SDKTOOLS/LISTMUNG/obj/i386/listmung.exe" "listmung.exe"
}

build_gensrv() {
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/SDKTOOLS/GENSRV" "GENSRV - NT syscall stub generator"
    install_host_tool "$NT_ROOT/PRIVATE/SDKTOOLS/GENSRV/obj/i386/gensrv.exe" "gensrv.exe"
}
build_rtl_user() {
    _ensure_error_h
    # TARGETPATH=..\obj puts rtl.lib at RTL/obj/i386/ — ensure it exists.
    mkdir -p "$NTOS/RTL/obj/i386"
    run_nmake "$NTOS/RTL/USER" "RTL_USER - user-mode runtime library"
}
build_ntdll()  {
    # gensrv writes i386/usrstubs.asm into the DAYTONA build dir — create it.
    mkdir -p "$NTOS/DLL/DAYTONA/i386"
    # makedll=1 tells MAKEFILE.DEF to actually link the DLL (not just import lib)
    run_nmake "$NTOS/DLL/DAYTONA" "NTDLL - user-mode runtime library" makedll=1
}
build_urtl()   { run_nmake "$NT_ROOT/PRIVATE/URTL" "URTL - native-app startup library (nt.lib)"; }
build_smlib()  { run_nmake "$NT_ROOT/PRIVATE/SM/CLIENT" "SM client library"; }
build_smss()   {
    # Build smss with NTDEBUG so KdPrint() calls are compiled in and we can
    # see "SMSS: ..." output on serial (via our KDTRAP.C tee). Swap the
    # NTDEBUG= entry in NT_ENV_ARR for NTDEBUG=sym, then restore.
    local i saved
    for i in "${!NT_ENV_ARR[@]}"; do
        if [[ "${NT_ENV_ARR[$i]}" == "NTDEBUG=" ]]; then
            saved=$i
            NT_ENV_ARR[$i]="NTDEBUG=sym"
            break
        fi
    done
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/SM/SERVER" "SMSS - Session Manager"
    local rc=$?
    [ -n "${saved:-}" ] && NT_ENV_ARR[$saved]="NTDEBUG="
    return $rc
}

# --- Client-Server Runtime Subsystem ---
#
# CSR/SERVER builds BOTH csrsrv.dll (TARGETNAME) AND csrss.exe (UMAPPL) in
# a single nmake pass. csrsrv is the subsystem runtime (LPC port listener,
# process/thread bookkeeping, registration). csrss.exe is the hosting
# process — tiny, just calls into csrsrv's ServerDllInitialization loop.
#
# basesrv.dll is the kernel32 server-side: CreateProcess, heap base-named
# objects, NLS server-side, atom table. Loaded by csrss at startup via
# the registry's ServerDll entries under Session Manager\SubSystems.
build_csrss()   {
    # Two toggles needed:
    #   KEEP_UMAPPL=1  — link the EXE (csrss.exe) half of the SOURCES,
    #                    otherwise SOURCES' UMAPPL= directive gets stripped
    #                    by our wrapper and the EXE is skipped.
    #   makedll=1      — tell MAKEFILE.DEF to actually LINK csrsrv.dll,
    #                    not just emit the import lib (same quirk as ntdll).
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/CSR/SERVER" "CSRSS + CSRSRV - Client-Server Runtime" makedll=1
}
build_basesrv() {
    run_nmake "$NT_ROOT/PRIVATE/WINDOWS/BASE/SERVER" "BASESRV - kernel32 subsystem server" makedll=1
}

# --- Win32 user-mode libraries (kernel32.dll chain) ---
#
# Dependency order: baselib <- nlslib <- conlib <- kernel32.dll
#   baselib   = BASE/RTL    (atom.c, handle.c)  -> baselib.lib
#   nlslib    = WINNLS                          -> nlslib.lib
#   conlib    = WINCON/CLIENT (console client)  -> conlib.lib
#   kernel32  = BASE/CLIENT (DAYTONA)           -> kernel32.dll
build_baselib() {
    # TARGETPATH=..\obj -> baselib.lib lands at BASE/obj/i386/
    mkdir -p "$NT_ROOT/PRIVATE/WINDOWS/BASE/obj/i386"
    run_nmake "$NT_ROOT/PRIVATE/WINDOWS/BASE/RTL" "BASELIB - kernel32 support lib"
}
build_nlslib() {
    # TARGETPATH=..\obj -> nlslib.lib lands at WINDOWS/obj/i386/
    mkdir -p "$NT_ROOT/PRIVATE/WINDOWS/obj/i386"
    run_nmake "$NT_ROOT/PRIVATE/WINDOWS/WINNLS" "NLSLIB - NLS/codepage lib for kernel32"
}
build_conlib() {
    # TARGETPATH=..\..\obj -> conlib.lib lands at WINDOWS/obj/i386/
    mkdir -p "$NT_ROOT/PRIVATE/WINDOWS/obj/i386"
    run_nmake "$NT_ROOT/PRIVATE/WINDOWS/WINCON/CLIENT" "CONLIB - console client lib for kernel32"
}
build_nlsmsg() {
    # mc.exe compiles WINERROR.MC -> winerror.h, winerror.rc, msg00001.bin.
    # kernel32's MAKEFILE.INC copies these from WINDOWS/NLSMSG/ into
    # BASE/CLIENT/ at build time.
    local dir="$NT_ROOT/PRIVATE/WINDOWS/NLSMSG"
    echo "========================================"
    echo "Building: NLSMSG - Win32 error messages (mc)"
    echo "========================================"
    run_wibo_tool "$dir" mc -s winerror.mc
    echo ">>> NLSMSG: WINERROR.MC -> winerror.h / winerror.rc / MSG00001.bin"
    ls -la "$dir/winerror.h" "$dir/winerror.rc" "$dir/MSG00001.bin" 2>/dev/null || true
}
build_kernel32() {
    # kernel32 uses DAYTONA-style build dir (like ntdll).
    mkdir -p "$NT_ROOT/PRIVATE/WINDOWS/BASE/CLIENT/DAYTONA/i386"
    # Ensure NLSMSG outputs exist (mc.exe writes MSG00001.bin in uppercase).
    if [ ! -f "$NT_ROOT/PRIVATE/WINDOWS/NLSMSG/MSG00001.bin" ]; then
        build_nlsmsg
    fi
    run_nmake "$NT_ROOT/PRIVATE/WINDOWS/BASE/CLIENT/DAYTONA" "KERNEL32 - Win32 base DLL" makedll=1
}

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

#
# Target split: headless vs GUI.
#
#   headless = kernel + storage/fs drivers + Win32-subsystem base
#              (csrss + csrsrv + basesrv + kernel32 + advapi32 + small
#              client DLLs that don't need USER or GDI).
#   gui      = headless + input drivers + VGA + usersrv/user32/gdisrv/
#              gdi32/consrv/winsrv/winlogon/userinit.
#
# The build-time split only gates what gets COMPILED. Disk-image
# composition (mkdisk.py + mkhive.py) chooses which of the compiled
# binaries to stage. That means you can build `all` (== gui) once and
# then flip between headless-boot and gui-boot at disk-build time.
#
# Order inside each array matters — deps first.

# Tools built from source — must run before any component that invokes
# them via nmake rules or build.sh helpers. Order matters: LINK first
# (needed by everything), MC before any .mc compilation, midl before
# any .idl compilation, gensrv before ntdll.
TOOL_TARGETS=(
    link
    mc
    rc
    listmung
    midleb midlyacc midlpg
    midl_support midl_expr midl_analysis midl_codegen midl
    gensrv
)

NTOSKRNL_TARGETS=(
    geni386
    ke rtl ex ob se ps mm cache config lpc dbgk io kd fsrtl raw vdm
    init
    hal
)

# Drivers needed regardless of mode — disk, FS, visibility/null stubs.
DRIVER_TARGETS=(
    atdisk null fastfat npfs msfs
)

# Drivers only useful with the GUI (input + video).
DRIVER_GUI_TARGETS=(
    i8042prt kbdclass mouclass
    vga_miniport bochsvga
)

# micront = minimum-viable NT kernel + smss only, NO Win32 subsystem.
# smss comes up, looks for its initial command, done. Useful for
# validating the native-NT boot chain with zero GUI/subsystem weight.
MICRONT_USERLAND_TARGETS=(
    rtl_user
    ntdll
    urtl
    smlib
    smss
    # kernel32/basesrv/csrss etc. are not built in micront — it's
    # just the NT kernel + session manager. Any "init" program must
    # be a native NT binary linked against nt.lib (no Win32).
)

# headless = micront + the Win32 base subsystem (csrss, basesrv,
# kernel32 + its support libs). No USER/GDI, no console server.
# This is what we have working today.
USERLAND_TARGETS=(
    "${MICRONT_USERLAND_TARGETS[@]}"
    baselib nlslib conlib nlsmsg
    kernel32
    # Win32 subsystem: csrsrv + csrss.exe first, then basesrv.dll which
    # depends on csrsrv.lib + baselib.
    csrss
    basesrv
    # RPC runtime + advapi32 stack. MIDL + gensrv are built in the tools
    # phase; rpcrt4 depends on midl-generated stubs. advapi32 pulls in
    # LSA + EventLog + SCM + registry — the full security/services
    # fabric the headless profile needs.
    rpcndrp rpcndr rpcndr20 rpcrt4 rpclts1 rpcltc1
    wrlib perflib localreg winreg
    sclib svcctrl
    elfapi
    lsacomm lsaudll
    crypt_engine sys003
    rpcutil
    advapi32
    # Security subsystem: SAM + LSA + NetLogon helpers → lsass.exe.
    # samsrv<->lsasrv have a circular DLL dep that's resolved inside
    # build_lsasrv_imports (compile-only pass, then `lib -def:X.def
    # obj/*.obj` to produce decorated import stubs).
    nlrepl
    samlib samsrv
    lsasrv
)

# GUI userland: pulls in the whole Win32 window/drawing stack. advapi32
# lives here, not in headless, because its SOURCES links against 14
# .libs from RPC/LSA/SCM/eventlog/remote-registry — effectively the
# whole Win32 security/services infrastructure, which isn't tractable
# for a "small DLL" port. winlogon is the main caller.
USERLAND_GUI_TARGETS=(
    # GDI support libs (font drivers + math) — built before gdisrvl
    gdi_efloat gdi_fscaler gdi_ttfd gdi_bmfd gdi_vtfd gdi_halftone
    # GDI server (engine) + client DLL
    gdisrv gdi32
    # USER server + client DLL
    usersrv user32
    # Console server
    consrv
    # winsrv.dll — aggregator (usersrv + gdisrv + consrv + basesrv)
    winsrv
    # LSA client stub + winreg server lib (winlogon links both)
    lsadll winregsrv
    # Framebuffer display driver (pairs with bochsvga.sys miniport)
    framebuf
    # Login + shell
    winlogon userinit
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

build_tools()            { build_group tools            "${TOOL_TARGETS[@]}"; }
build_ntoskrnl()         { build_group ntoskrnl         "${NTOSKRNL_TARGETS[@]}"; }
build_drivers()          { build_group drivers          "${DRIVER_TARGETS[@]}"; }
build_drivers_gui()      { build_group drivers_gui      "${DRIVER_GUI_TARGETS[@]}"; }
build_userland_micront() { build_group userland_micront "${MICRONT_USERLAND_TARGETS[@]}"; }
build_userland()         { build_group userland         "${USERLAND_TARGETS[@]}"; }
build_userland_gui()     { build_group userland_gui     "${USERLAND_GUI_TARGETS[@]}"; }

build_efi() {
    echo ""
    echo "========================================"
    echo "Building: UEFI bootloader (BOOTIA32.EFI)"
    echo "========================================"
    make -C "$SCRIPT_DIR/boot-efi" BOOTIA32.EFI
}

build_disk() {
    local profile="${1:-${PROFILE:-headless}}"
    local out_dir="$(dirname "$SCRIPT_DIR")/build/$profile"
    local efi_bin="$SCRIPT_DIR/boot-efi/BOOTIA32.EFI"

    echo ""
    echo "========================================"
    echo "Building boot disk image ($profile)"
    echo "========================================"

    # Build EFI loader if not already present.
    if [ ! -f "$efi_bin" ]; then
        build_efi
    fi

    mkdir -p "$out_dir"
    python3 "$SCRIPT_DIR/tools/mkhive.py" --profile "$profile" "$out_dir/SYSTEM"
    python3 "$SCRIPT_DIR/tools/mkdisk.py" --profile "$profile" \
        --output-dir "$out_dir" --efi-binary "$efi_bin"
}

#
# Profile builders. Each builds a strict superset of the previous,
# then assembles the disk with the matching profile. Disk assembly
# respects the $PROFILE env var — see boot-efi/Makefile and
# tools/mkhive.py --profile. A previous `./build.sh gui` + later
# `PROFILE=micront ./build.sh disk` is a valid flow (compile once,
# assemble many ways), but the top-level targets set PROFILE for you.
#

# Compile phases — no disk assembly, no PROFILE mutation.
_compile_micront() {
    build_tools
    build_ntoskrnl
    build_drivers
    build_userland_micront
}

_compile_headless() {
    _compile_micront
    build_userland
}

_compile_gui() {
    _compile_headless
    build_drivers_gui
    build_userland_gui
}

# Profile builders — compile everything needed, then assemble the disk
# with the correct PROFILE. Each is the single entry point users invoke.
build_micront() {
    export PROFILE=micront
    _compile_micront
    build_disk
}

build_headless() {
    export PROFILE=headless
    _compile_headless
    build_disk
}

build_gui() {
    export PROFILE=gui
    _compile_gui
    build_disk
}

# `all` == compile everything, then assemble the GUI profile disk.
build_all() { build_gui; }

# --- Main dispatch -----------------------------------------------------------

_dispatch_one() {
    local comp="$1"
    case "$comp" in
        all)               build_all ;;
        gui)               build_gui ;;
        headless)          build_headless ;;
        micront)           build_micront ;;
        ntoskrnl)          build_ntoskrnl ;;
        drivers)           build_drivers ;;
        drivers-gui)       build_drivers_gui ;;
        userland-micront)  build_userland_micront ;;
        userland)          build_userland ;;
        userland-gui)      build_userland_gui ;;
        disk)              build_disk ;;
        disk-gui)          build_disk gui ;;
        disk-headless)     build_disk headless ;;
        disk-micront)      build_disk micront ;;
        *)
            if declare -F "build_$comp" > /dev/null; then
                "build_$comp"
            else
                echo "Unknown component: $comp"
                echo ""
                echo "Profile targets: all (=gui), gui, headless, micront"
                echo "Group targets:   ntoskrnl, drivers, drivers-gui,"
                echo "                 userland-micront, userland, userland-gui, disk"
                echo ""
                echo "Individual components (in build order):"
                echo "  ntoskrnl:          ${NTOSKRNL_TARGETS[*]}"
                echo "  drivers:           ${DRIVER_TARGETS[*]}"
                echo "  drivers-gui:       ${DRIVER_GUI_TARGETS[*]}"
                echo "  userland-micront:  ${MICRONT_USERLAND_TARGETS[*]}"
                echo "  userland:          ${USERLAND_TARGETS[*]}"
                echo "  userland-gui:      ${USERLAND_GUI_TARGETS[*]}"
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
