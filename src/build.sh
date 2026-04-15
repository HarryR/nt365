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
#
# By default clears NTTEST / UMTEST / UMAPPL so they don't accidentally turn
# a kernel-lib build into an EXE build. For components that are intentionally
# UMAPPL builds (like gensrv, smss), set `KEEP_UMAPPL=1` in the caller env
# to preserve the SOURCES file's UMAPPL= directive.
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
    python3 "$SCRIPT_DIR/tools/gen_objects.py" "$linux_dir"

    # Convert Linux path to D:\ path
    local rel_path="${linux_dir#$NT_ROOT}"
    local win_dir="D:$(echo "$rel_path" | sed 's|/|\\|g')"

    # For user-mode-app builds (UMAPPL=), don't zero out UMAPPL.
    local umappl_override="UMAPPL="
    if [ "${KEEP_UMAPPL:-}" = "1" ]; then
        umappl_override=""
    fi

    cd "$linux_dir"

    WINEDEBUG=-all \
    wine cmd.exe /C \
        "$NT_ENV&& set MAKEDIR=$win_dir&& nmake /NOLOGO NTTEST= UMTEST= $umappl_override $extra_args"

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

# --- Boot device / filesystem drivers (TARGETTYPE=DRIVER) ---
build_atdisk() { run_nmake "$NTOS/DD/HARDDISK" "ATDISK - IDE disk driver"; }
build_null()   { run_nmake "$NTOS/DD/NULL"     "NULL - null device driver"; }
build_fastfat(){ run_nmake "$NTOS/FASTFAT"     "FASTFAT - FAT filesystem driver"; }
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
    (
        cd "$rtifs" || exit 1
        rm -f nbase.h {conv,epmp,mgmt}.h {conv,epmp,mgmt}_{c,s}.c
        local env='set PATH=D:\\PUBLIC\\OAK\\BIN\\I386&& set INCLUDE=D:\\PUBLIC\\SDK\\INC;D:\\PUBLIC\\OAK\\INC;D:\\PUBLIC\\SDK\\INC\\CRT'
        for idl in conv epmp mgmt nbase; do
            wine cmd /c "$env&& midl /ms_ext /c_ext /app_config /D MIDL_PASS /I ..\\mtrt $idl.idl" || exit 1
        done
    ) || { echo "!!! RPC/RTIFS midl gen failed"; return 1; }
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
        echo ">>> installed $name into PUBLIC/OAK/BIN/I386/"
    else
        echo "!!! $name: expected output $built not found" >&2
        return 1
    fi
}
build_midleb() {
    run_nmake "$NT_ROOT/PRIVATE/RPC/MIDLNEW/EREC" "MIDL/EREC - error-recovery DB generator (midleb.exe)"
    install_host_tool "$NT_ROOT/PRIVATE/RPC/MIDLNEW/EREC/obj/i386/midleb.exe" "midleb.exe"
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
    # Use existing uppercase INCLUDE — Linux is case-sensitive, wine isn't,
    # and creating a sibling lowercase `include/` makes wine see two dirs.
    local inc="$NT_ROOT/PRIVATE/RPC/MIDL20/INCLUDE"
    local oak="D:\\PUBLIC\\OAK\\BIN\\I386"
    echo ">>> MIDL/FRONT gen: midlyacc + midlpg + midleb"
    (
        cd "$front" || exit 1
        # midlyacc emits FOO.C/FOO.H/FOO.I (uppercase). Wine's case-
        # insensitive FS lets cl pick up FOO.C when SOURCES says foo.cxx,
        # so we delete the .C/.H/.I after midlpg consumes them.
        wine "$oak\\midlyacc.exe" -his -t "YYSTATIC " grammar.y       || exit 1
        wine "$oak\\midlpg.exe"   grammar.C   > grammar.cxx           || exit 1
        wine "$oak\\midleb.exe"   - xlatidl.dat IDL > "$inc/idlerec.h" || exit 1
        # Keep grammar.h (lex.cxx includes it) by moving into INCLUDE; drop .C/.I
        # so cl doesn't pick up grammar.C instead of grammar.cxx on wine FS.
        mv -f grammar.H "$inc/grammar.h" && rm -f grammar.C grammar.I
        wine "$oak\\midlyacc.exe" -hi  -t "YYSTATIC " acfgram.y       || exit 1
        wine "$oak\\midlpg.exe"   acfgram.C  > acfgram.cxx            || exit 1
        wine "$oak\\midleb.exe"   - xlatacf.dat ACF > "$inc/acferec.h" || exit 1
        mv -f acfgram.H "$inc/acfgram.h" && rm -f acfgram.C acfgram.I
    ) || { echo "!!! MIDL/FRONT gen failed"; return 1; }
    echo ">>> MIDL/FRONT gen: OK (grammar.cxx, acfgram.cxx, idlerec.h, acferec.h)"
}
build_midl() {
    _midl_front_gen || return 1
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
build_videoprt()    { run_nmake "$NTOS/VIDEO/PORT" "VIDEOPRT - video miniport framework"; }
build_vga_miniport(){
    build_videoprt
    run_nmake "$NTOS/VIDEO/VGA" "VGA - VGA miniport driver"
}
build_gensrv() {
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/SDKTOOLS/GENSRV" "GENSRV - NT syscall stub generator"
    # Install into OAK/BIN/I386 so nmake rules can invoke it by bare name.
    local gensrv_out="$NT_ROOT/PRIVATE/SDKTOOLS/GENSRV/obj/i386/gensrv.exe"
    local gensrv_dst="$NT_ROOT/PUBLIC/OAK/BIN/I386/gensrv.exe"
    if [ -f "$gensrv_out" ]; then
        cp "$gensrv_out" "$gensrv_dst"
        echo ">>> installed gensrv.exe into PUBLIC/OAK/BIN/I386/"
    fi
}
build_rtl_user() {
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
    # see "SMSS: ..." output on serial (via our KDTRAP.C tee).
    NT_ENV_SAVED="$NT_ENV"
    NT_ENV="$(echo "$NT_ENV" | sed 's/set NTDEBUG=&&/set NTDEBUG=sym\&\&/')"
    KEEP_UMAPPL=1 run_nmake "$NT_ROOT/PRIVATE/SM/SERVER" "SMSS - Session Manager"
    NT_ENV="$NT_ENV_SAVED"
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
    run_wine_cmd "NLSMSG mc" \
        "D:&& cd \\PRIVATE\\WINDOWS\\NLSMSG&& mc -s winerror.mc"
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

build_init() {
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

# Multi-arg support: `build.sh kd init` builds both in order.
# No args → build all.
if [ $# -gt 1 ]; then
    for arg in "$@"; do
        bash "$SCRIPT_DIR/build.sh" "$arg" || exit $?
    done
    exit 0
fi

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

NTOSKRNL_TARGETS=(
    geni386
    ke rtl ex ob se ps mm cache config lpc dbgk io kd fsrtl raw vdm
    init
    hal
)

# Drivers needed regardless of mode — disk, FS, visibility/null stubs.
DRIVER_TARGETS=(
    atdisk null fastfat hello
)

# Drivers only useful with the GUI (input + video).
DRIVER_GUI_TARGETS=(
    i8042prt kbdclass mouclass
    vga_miniport
)

# micront = minimum-viable NT kernel + smss only, NO Win32 subsystem.
# smss comes up, looks for its initial command, done. Useful for
# validating the native-NT boot chain with zero GUI/subsystem weight.
MICRONT_USERLAND_TARGETS=(
    gensrv
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
)

# GUI userland: pulls in the whole Win32 window/drawing stack. advapi32
# lives here, not in headless, because its SOURCES links against 14
# .libs from RPC/LSA/SCM/eventlog/remote-registry — effectively the
# whole Win32 security/services infrastructure, which isn't tractable
# for a "small DLL" port. winlogon is the main caller.
USERLAND_GUI_TARGETS=(
    # advapi32 + its dependency chain (RPC runtime, LSA, SCM, eventlog,
    # winreg) — would need a dedicated porting sub-group before we can
    # land this. Left as TODO; winlogon blocks on it.
    # advapi32
    # USER subsystem server + user32 client
    usersrv user32
    # GDI server + client + font drivers
    gdisrv  gdi32
    # Console server (needs USER for console window)
    consrv
    # winsrv.dll — aggregator that links usersrv + gdisrv + consrv + basesrv
    winsrv
    # VGA user-mode display driver (talks to vga_miniport)
    vga_display
    # Login + shell bootstrap — needs advapi32 too
    # winlogon userinit
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

build_ntoskrnl()         { build_group ntoskrnl         "${NTOSKRNL_TARGETS[@]}"; }
build_drivers()          { build_group drivers          "${DRIVER_TARGETS[@]}"; }
build_drivers_gui()      { build_group drivers_gui      "${DRIVER_GUI_TARGETS[@]}"; }
build_userland_micront() { build_group userland_micront "${MICRONT_USERLAND_TARGETS[@]}"; }
build_userland()         { build_group userland         "${USERLAND_TARGETS[@]}"; }
build_userland_gui()     { build_group userland_gui     "${USERLAND_GUI_TARGETS[@]}"; }

build_disk() {
    echo ""
    echo "========================================"
    echo "Building boot disk image"
    echo "========================================"
    python3 "$SCRIPT_DIR/tools/mkhive.py" "$SCRIPT_DIR/boot/data/SYSTEM"
    python3 "$SCRIPT_DIR/tools/mkdisk.py"
}

#
# Profile builders. Each builds a strict superset of the previous,
# then assembles the disk with the matching profile. Disk assembly
# respects the $PROFILE env var — see boot-efi/Makefile and
# tools/mkhive.py --profile. A previous `./build.sh gui` + later
# `PROFILE=micront ./build.sh disk` is a valid flow (compile once,
# assemble many ways), but the top-level targets set PROFILE for you.
#

build_micront() {
    PROFILE=micront
    export PROFILE
    build_ntoskrnl
    build_drivers
    build_userland_micront
    build_disk
}

build_headless() {
    PROFILE=headless
    export PROFILE
    build_ntoskrnl
    build_drivers
    build_userland
    build_disk
}

build_gui() {
    PROFILE=gui
    export PROFILE
    build_ntoskrnl
    build_drivers
    build_drivers_gui
    build_userland
    build_userland_gui
    build_disk
}

# `all` == compile everything, then assemble the GUI profile disk.
build_all() { build_gui; }

# --- Main dispatch -----------------------------------------------------------

COMPONENT="${1:-all}"

# Everything callable: individual component functions + group targets + all/disk.
# If a matching build_<name> function exists, invoke it. Otherwise complain.
case "$COMPONENT" in
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
    *)
        if declare -F "build_$COMPONENT" > /dev/null; then
            "build_$COMPONENT"
        else
            echo "Unknown component: $COMPONENT"
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
