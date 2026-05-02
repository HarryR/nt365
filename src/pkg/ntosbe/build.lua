-- ntosbe.build — top-level MicroNT build driver.
--
-- Originally lived as src/build.lua next to the bash entry point; now
-- a regular package module so the same body runs on host (Linux +
-- wibo) and inside MicroNT once the platform.spawn_wait NT backend
-- lands.  src/build.sh remains the host CLI wrapper — it bootstraps
-- LuaJIT, sets package.path, and calls main(arg) here.
--
-- Top-level groups: tools, ntoskrnl, drivers, userland, cr, efi, disk.
-- See usage() at the bottom for the per-component target list.
--
-- Entry:
--
--   require('ntosbe.build').main{
--       script_dir = "/abs/path/to/src",        -- where pkg/ + NT/ live
--       repo_root  = "/abs/path/to/repo",       -- defaults: dirname(script_dir)
--       wibo_bin   = "/abs/path/to/wibo",       -- host only; nil on guest
--       wibo_tools = "<script_dir>/wibo-tools", -- defaults: $script_dir/wibo-tools
--       drive_root = "Z:",                      -- "Z:" host, "C:" on NT
--       args       = arg or {},                 -- positional + --debug etc.
--   }
--
-- script_dir + repo_root come from the caller (the host shim knows them
-- via build.sh; the eventual NT-side entry script will set them to
-- "\\SystemRoot\\NT" or wherever the source tree is mounted).

local platform  = require('ntosbe.platform')
local sources   = require('ntosbe.sources')
local toolchain = require('ntosbe.tchain')
local codegen   = require('ntosbe.codegen')
local ntosbe    = require('ntosbe')

local M = {}

-- ----------------------------------------------------------------
-- main — builds the target table from configured roots, parses argv,
-- dispatches.  Single entry; no globals leak out of this function.
-- ----------------------------------------------------------------

function M.main(opts)
    opts = opts or {}
    local SCRIPT_DIR = assert(opts.script_dir,
        "ntosbe.build.main: script_dir required")
    local NT_ROOT  = SCRIPT_DIR .. "/NT"
    local NTOS     = NT_ROOT .. "/PRIVATE/NTOS"
    local REPO_ROOT = opts.repo_root
                  or sources.dirname(SCRIPT_DIR)
    local WIBO_BIN  = opts.wibo_bin
                  or (REPO_ROOT .. "/wibo-x86_64")
    local WIBO_TOOLS = opts.wibo_tools
                  or (SCRIPT_DIR .. "/wibo-tools")
    local DRIVE_ROOT = opts.drive_root or "Z:"
    local args = opts.args or {}

    -- Filesystem aliases — short handles for this function's body.
    local file_exists   = platform.file_exists
    local is_executable = platform.is_executable
    local mkdir_p       = platform.mkdir_p
    local mtime         = platform.mtime
    local copy_file     = platform.copy_file

    local basename        = sources.basename
    local stem            = sources.stem
    local dirname         = sources.dirname
    local gen_objects     = sources.gen_objects

    if platform.on_host and not is_executable(WIBO_BIN) then
        platform.log(("ERROR: wibo binary not found or not executable: %s")
            :format(WIBO_BIN))
        platform.log("Download from https://github.com/HarryR/wibo/releases (the MicroNT-patched fork)")
        platform.log(("and place as %s, then chmod +x."):format(WIBO_BIN))
        return 1
    end

    -- ----------------------------------------------------------------
    -- Logging — local to this run.
    -- ----------------------------------------------------------------

    local function log(s) platform.log(s) end

    local function banner(title)
        log("========================================")
        log("Building: " .. title)
        log("========================================")
    end

    -- ----------------------------------------------------------------
    -- Toolchain bridge.  configure() picks host vs NT, prepares NT_ENV
    -- and (host only) populates the wibo-tools symlink farm.
    -- ----------------------------------------------------------------

    toolchain.configure{
        nt_root    = NT_ROOT,
        wibo_tools = WIBO_TOOLS,
        wibo_bin   = platform.on_host and WIBO_BIN or nil,
        drive_root = DRIVE_ROOT,
        path_strip = opts.path_strip,
    }

    local path_to_win        = toolchain.path_to_win
    local NT_ROOT_WIN        = path_to_win(NT_ROOT)
    local WIBO_TOOLS_WIN     = path_to_win(WIBO_TOOLS)
    local build_envp         = toolchain.build_envp
    local run_nmake          = toolchain.run_nmake
    local run_wibo_tool      = toolchain.run_wibo_tool
    local wibo_spawn_args    = toolchain.wibo_spawn_args
    local install_host_tool  = toolchain.install_host_tool

    -- ----------------------------------------------------------------
    -- Codegen helpers — small "regenerate this generated file" steps
    -- that gate specific nmake builds.
    -- ----------------------------------------------------------------

    codegen.configure{
        nt_root  = NT_ROOT,
        src_root = SCRIPT_DIR,
    }

    local ensure_error_h  = codegen.ensure_error_h
    local ensure_bugcodes = codegen.ensure_bugcodes
    local ensure_serlog   = codegen.ensure_serlog
    local ensure_cmdmsg   = codegen.ensure_cmdmsg

    -- ----------------------------------------------------------------
    -- Host-only helper: spawn `make -C <dir>` for the cr + boot-efi
    -- peer trees.  Errors out cleanly on guest (those don't build there).
    -- ----------------------------------------------------------------

    local function run_make(cwd, target)
        if not platform.on_host then
            log("ERROR: run_make is host-only (cwd=" .. cwd .. ")")
            return 1
        end
        local argv = { "make", "-C", cwd }
        if target then argv[#argv + 1] = target end
        return platform.spawn_wait{ argv = argv, search_path = true }
    end

    -- ----------------------------------------------------------------
    -- Targets — faithful translations of build.sh's per-component
    -- functions.  Trivial 1-line wrappers stay 1-line here.
    -- ----------------------------------------------------------------

    local targets = {}

    -- clean_dirs[name] = { source_dir, ... } — every directory whose
    -- `obj/` subtree gets removed by `clean:<name>`.  Trivial
    -- nmake_target builds self-register; non-trivial / multi-dir
    -- targets register manually below.
    local clean_dirs = {}

    local function nmake_target(name, dir, desc, t_opts, ...)
        local extras = {...}
        targets[name] = function()
            if t_opts and t_opts.pre and not t_opts.pre() then return 1 end
            return run_nmake(dir, desc, extras, t_opts)
        end
        clean_dirs[name] = { dir }
    end

    -- ----- NTOS core -----
    nmake_target("ke",     NTOS .. "/KE/UP",     "KE - Kernel Core")
    nmake_target("ex",     NTOS .. "/EX/UP",     "EX - Executive")
    nmake_target("ob",     NTOS .. "/OB/UP",     "OB - Object Manager")
    nmake_target("se",     NTOS .. "/SE/UP",     "SE - Security")
    nmake_target("ps",     NTOS .. "/PS/UP",     "PS - Process Structure")
    nmake_target("mm",     NTOS .. "/MM/UP",     "MM - Memory Manager")
    nmake_target("cache",  NTOS .. "/CACHE/UP",  "CACHE - Cache Manager")
    nmake_target("config", NTOS .. "/CONFIG/UP", "CONFIG - Registry")
    nmake_target("lpc",    NTOS .. "/LPC/UP",    "LPC - Local Procedure Call")
    nmake_target("dbgk",   NTOS .. "/DBGK/UP",   "DBGK - Debug Subsystem")
    nmake_target("io",     NTOS .. "/IO/UP",     "IO - I/O Manager")
    nmake_target("kd",     NTOS .. "/KD/UP",     "KD - Kernel Debugger")
    nmake_target("fsrtl",  NTOS .. "/FSRTL/UP",  "FSRTL - File System RTL")
    nmake_target("raw",    NTOS .. "/RAW/UP",    "RAW - Raw File System")
    nmake_target("vdm",    NTOS .. "/VDM/UP",    "VDM - Virtual DOS Machine")

    -- ----- File-system / I/O drivers -----
    nmake_target("atdisk",  NTOS .. "/DD/HARDDISK", "ATDISK - IDE disk driver")
    nmake_target("serial",  NTOS .. "/DD/SERIAL",   "SERIAL - NT 3.5 serial port driver",
                 { pre = ensure_serlog })
    nmake_target("null",    NTOS .. "/DD/NULL",     "NULL - null device driver")
    nmake_target("fastfat", NTOS .. "/FASTFAT",     "FASTFAT - FAT filesystem driver")
    nmake_target("npfs",    NTOS .. "/NPFS",        "NPFS - Named Pipe filesystem driver")
    nmake_target("msfs",    NTOS .. "/MAILSLOT",    "MSFS - Mailslot filesystem driver")
    nmake_target("hello",   NTOS .. "/DD/HELLO",    "HELLO - MicroNT visibility driver")

    -- ----- Input / video stack -----
    nmake_target("i8042prt", NTOS .. "/DD/I8042PRT", "I8042PRT - PS/2 port driver (kb + mouse)")
    nmake_target("kbdclass", NTOS .. "/DD/KBDCLASS", "KBDCLASS - keyboard class driver")
    nmake_target("mouclass", NTOS .. "/DD/MOUCLASS", "MOUCLASS - mouse class driver")
    nmake_target("videoprt", NTOS .. "/VIDEO/PORT",  "VIDEOPRT - video miniport framework",
                 nil, "makedll=1")
    nmake_target("bochsvga", NTOS .. "/VIDEO/BOCHSVGA",
                 "BOCHSVGA - Bochs/QEMU VBE miniport")

    -- ----- VirtIO stack -----
    nmake_target("virtio_lib", NTOS .. "/VIRTIO",        "VIRTIO - bus + ring + PCI legacy (virtio.lib)")
    nmake_target("viorng",     NTOS .. "/DD/VIORNG",     "VIORNG - virtio-rng entropy driver")
    nmake_target("vioser",     NTOS .. "/DD/VIOSER",     "VIOSER - virtio-console driver")
    nmake_target("vioinput",   NTOS .. "/DD/VIOINPUT",   "VIOINPUT - virtio-input keyboard/mouse driver")

    -- ----- Tests -----
    nmake_target("cowtest", NT_ROOT .. "/PRIVATE/TESTS/cowtest",
                 "COWTEST - COW test program",
                 { keep_umappl = true })

    -- ----- WINDOWS / BASE / CLIENT — kernel32.dll (the only Win32 lib) -----
    -- Lifted from NT 3.5 source under src/NT/PRIVATE/WINDOWS/.  Two
    -- static-lib dependencies (windows_base_rtl, windows_winnls) are
    -- exposed for granular debug rebuilds; the operator-facing entry is
    -- windows_base_client which chains them in order.  No new group;
    -- windows_base_client joins USERLAND_TARGETS so `build.lua all`
    -- includes it.
    --
    -- Output paths:
    --   windows_base_rtl    → BASE/obj/i386/baselib.lib
    --   windows_winnls      → WINDOWS/obj/i386/nlslib.lib
    --   windows_base_client → PUBLIC/SDK/LIB/i386/{kernel32.lib, kernel32.dll}
    --
    -- run_nmake auto-mkdirs only <linux_dir>/obj/i386, so the parent
    -- TARGETPATH dirs (..\obj relative to each leaf) need pre-creation.
    local WIN = NT_ROOT .. "/PRIVATE/WINDOWS"

    targets.windows_base_rtl = function()
        mkdir_p(WIN .. "/BASE/obj/i386")
        return run_nmake(WIN .. "/BASE/RTL",
                         "WINDOWS/BASE/RTL - baselib (atoms + handles)")
    end
    clean_dirs.windows_base_rtl = { WIN .. "/BASE/RTL", WIN .. "/BASE/obj" }

    targets.windows_winnls = function()
        mkdir_p(WIN .. "/obj/i386")
        return run_nmake(WIN .. "/WINNLS",
                         "WINDOWS/WINNLS - nlslib (codepage / locale)")
    end
    clean_dirs.windows_winnls = { WIN .. "/WINNLS", WIN .. "/obj" }

    targets.windows_base_client = function()
        if targets.windows_base_rtl() ~= 0 then return 1 end
        if targets.windows_winnls()   ~= 0 then return 1 end
        return run_nmake(WIN .. "/BASE/CLIENT",
                         "WINDOWS/BASE/CLIENT - kernel32.dll",
                         { "makedll=1" })
    end
    clean_dirs.windows_base_client = {
        WIN .. "/BASE/CLIENT", WIN .. "/BASE/RTL", WIN .. "/WINNLS",
        WIN .. "/BASE/obj", WIN .. "/obj",
    }

    -- ----- WINDOWS/CMD — NT 3.5 cmd.exe lifted from stuff/.
    -- NMAKE shells inline commands (@if exist, &&, |, redirections)
    -- through COMSPEC; without a working cmd.exe those _spawn calls
    -- fail.  user32.lib + advapi32.lib were dropped from upstream
    -- UMLIBS — wsprintf is shimmed via libc.lib's static vsprintf
    -- (see CMD/wsprintf_shim.c).  cmdmsg.mc → cmdmsg.h/.rc ahead of
    -- the build via ensure_cmdmsg.  keep_umappl preserves the
    -- UMAPPL=cmd directive so the link step actually produces
    -- cmd.exe (TARGETTYPE=LIBRARY alone only builds cmd.lib).
    nmake_target("cmd", WIN .. "/CMD",
                 "WINDOWS/CMD - cmd.exe (Win32 shell, COMSPEC for NMAKE)",
                 { pre = ensure_cmdmsg, keep_umappl = true })

    -- ----- SCSI subsystem -----
    -- class.lib → scsiport.sys → scsidisk.sys (linkage is in MAKEFILE.DEFs).
    -- run_nmake already does linux_dir/obj/i386 mkdir, so the explicit
    -- mkdirs in build.sh are redundant here.
    nmake_target("dd_class",    NTOS .. "/DD/CLASS",
                 "CLASS - SCSI class-driver helper lib")
    nmake_target("dd_scsiport", NTOS .. "/DD/SCSIPORT",
                 "SCSIPORT - SCSI miniport framework",
                 nil, "makedll=1")
    nmake_target("dd_scsidisk", NTOS .. "/DD/SCSIDISK",
                 "SCSIDISK - SCSI disk class driver")
    nmake_target("dd_nvme2k",   NTOS .. "/DD/NVME2K",
                 "NVME2K - NVMe storage controller (SCSI miniport)")

    -- ----- NDIS framework -----
    nmake_target("ndis_wrapper", NTOS .. "/NDIS/WRAPPER",
                 "NDIS - NDIS wrapper / framework",
                 nil, "makedll=1")
    nmake_target("ndis_vionet",  NTOS .. "/NDIS/VIONET",
                 "VIONET - virtio-net NDIS miniport")

    -- ----- TDI + TCPIP -----
    nmake_target("tdi_wrapper", NTOS .. "/TDI/WRAPPER",
                 "TDI - TDI wrapper (tdi.sys)",
                 nil, "makedll=1")
    -- ip.lib lands at TCPIP/obj/i386/ rather than TCPIP/IP/obj/i386/, so
    -- the parent dir needs an explicit mkdir before nmake runs.
    targets.tdi_tcpip_ip = function()
        mkdir_p(NTOS .. "/TDI/TCPIP/obj/i386")
        return run_nmake(NTOS .. "/TDI/TCPIP/IP",
                         "TDI/TCPIP/IP - IP/ARP/ICMP (ip.lib)")
    end
    nmake_target("tdi_tcpip_tcp", NTOS .. "/TDI/TCPIP/TCP",
                 "TDI/TCPIP/TCP - TCP/UDP transport (tcpip.sys)")

    -- ----- AFD socket layer (links against tdi.lib) -----
    nmake_target("afd", NTOS .. "/AFD",
                 "AFD - socket emulation driver (afd.sys)")

    -- ----- VirtIO composite — lib + 3 simple drivers + the NDIS miniport.
    -- Builds in deterministic order so the .lib lands before its consumers.
    targets.virtio = function()
        for _, t in ipairs({ "virtio_lib", "viorng", "vioser", "vioinput",
                             "ndis_vionet" }) do
            local rc = targets[t]()
            if rc ~= 0 then return rc end
        end
        return 0
    end

    -- ----- Video miniport composite — videoprt.sys built first, then
    -- vga miniports against it.  vga_miniport handled inline; build.sh
    -- always rebuilds videoprt before each, but here we let group order
    -- handle that since videoprt is in the trivial-target list above.
    targets.vga_miniport = function()
        local rc = targets.videoprt(); if rc ~= 0 then return rc end
        return run_nmake(NTOS .. "/VIDEO/VGA", "VGA - VGA miniport driver")
    end

    -- ----- RTL — needs error.h generated first (Python helper for now). -----
    targets.rtl = function()
        if not ensure_error_h() then return 1 end
        return run_nmake(NTOS .. "/RTL/UP", "RTL - Runtime Library")
    end

    -- ----- Userland NT runtime libs ----------------------------------------
    targets.rtl_user = function()
        if not ensure_error_h() then return 1 end
        -- TARGETPATH=..\obj puts rtl.lib at RTL/obj/i386/.
        mkdir_p(NTOS .. "/RTL/obj/i386")
        return run_nmake(NTOS .. "/RTL/USER", "RTL_USER - user-mode runtime library")
    end

    -- ----- gensrv (NT syscall stub generator) — UMAPPL kept so it builds as
    -- a host EXE the DAYTONA nmake rule can invoke.
    targets.gensrv = function()
        local rc = run_nmake(NT_ROOT .. "/PRIVATE/SDKTOOLS/GENSRV",
                             "GENSRV - NT syscall stub generator",
                             {}, { keep_umappl = true })
        if rc ~= 0 then return rc end
        if not install_host_tool(
                NT_ROOT .. "/PRIVATE/SDKTOOLS/GENSRV/obj/i386/gensrv.exe",
                "gensrv.exe") then
            return 1
        end
        return 0
    end

    -- ----- ntdll.dll — makedll=1 triggers the DLL link step in MAKEFILE.DEF
    -- on top of the import-lib build.
    targets.ntdll = function()
        -- DAYTONA needs an i386 subdir for the generated usrstubs.asm.
        mkdir_p(NTOS .. "/DLL/DAYTONA/i386")
        return run_nmake(NTOS .. "/DLL/DAYTONA",
                         "NTDLL - user-mode runtime library",
                         { "makedll=1" })
    end

    nmake_target("urtl", NT_ROOT .. "/PRIVATE/URTL",
                 "URTL - native-app startup library (nt.lib)")

    -- ----- HAL stubs lib (consumed by ntoskrnl link) ----------------------
    targets.hal_stubs = function()
        -- Running full nmake compiles the HAL objs; lib -def:hal.def runs as
        -- part of MAKEFILE.INC's lib rule.  Final hal.dll link happens later
        -- in targets.hal.
        return run_nmake(NTOS .. "/NTHALS/HAL", "HAL - stubs lib (for ntoskrnl link)")
    end

    -- ----- HAL (DLL link step on top of hal.lib) --------------------------
    targets.hal = function()
        local hal_dir = NTOS .. "/NTHALS/HAL"
        local rc = run_nmake(hal_dir, "HAL - MicroNT HAL (lib)")
        if rc ~= 0 then return rc end

        banner("HAL - MicroNT HAL (DLL link)")
        mkdir_p(hal_dir .. "/obj/i386")

        rc = run_wibo_tool(hal_dir, "link",
            "-OUT:obj\\i386\\hal.dll", "-DLL", "-MACHINE:i386",
            "-BASE:0x80400000", "-SUBSYSTEM:NATIVE", "-ENTRY:HalInitSystem@8",
            "-NODEFAULTLIB", "-RELEASE", "-DEBUG:MINIMAL", "-DEBUGTYPE:COFF",
            "-OPT:REF",
            "obj\\i386\\*.obj",
            NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\ntoskrnl.lib",
            NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\libcntpr.lib",
            NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\int64.lib",
            NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\hal.exp")
        if rc ~= 0 or not file_exists(hal_dir .. "/obj/i386/hal.dll") then
            log(">>> HAL - MicroNT HAL (DLL): FAILED")
            return 1
        end
        log(">>> HAL - MicroNT HAL (DLL): OK")
        return 0
    end

    -- ----- INIT — links every kernel .lib into ntoskrnl.exe.  Special: we
    -- must NOT override NTTEST (NMAKE uses NTTEST=ntoskrnl to drive the
    -- exe build via MAKEFILE.DEF).  bug-codes generated first.
    -- ----------------------------------------------------------------------
    targets.init = function()
        if targets.hal_stubs() ~= 0 then return 1 end
        if not ensure_bugcodes() then return 1 end

        local linux_dir = NTOS .. "/INIT/UP"
        local desc      = "INIT - NTOSKRNL.EXE"
        banner(desc)

        mkdir_p(linux_dir .. "/obj/i386")
        if not gen_objects(linux_dir) then return 1 end

        local rel_path    = linux_dir:sub(#NT_ROOT + 1)
        local makedir_win = NT_ROOT_WIN .. rel_path:gsub("/", "\\")

        local argv = {
            "wibo", "--chdir", linux_dir,
            WIBO_TOOLS .. "/NMAKE.EXE", "/NOLOGO",
            "UMTEST=", "UMAPPL=",
        }
        local envp = build_envp({ "MAKEDIR=" .. makedir_win })
        local rc = platform.spawn_wait{ argv = argv, env = envp, path = WIBO_BIN }

        if rc == 0 then
            log(">>> " .. desc .. ": OK")
        else
            log((">>> %s: FAILED (rc=%d)"):format(desc, rc))
        end
        return rc
    end

    -- ----- GENI386 (struct offset generator → KS386.INC + HAL386.INC) -----
    targets.geni386 = codegen.geni386

    -- ----- LINK.EXE rebuild from source ---------------------------------
    targets.link = function()
        local link_dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/LINK"
        local pdb_dir  = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/PDB"
        banner("LINK.EXE (patched for wibo)")

        if run_nmake(pdb_dir  .. "/DBI",       "pdb/dbi.lib")          ~= 0 then return 1 end
        if run_nmake(link_dir .. "/CVTOMF",    "link/cvtomf.lib")      ~= 0 then return 1 end
        if run_nmake(link_dir .. "/DISASM",    "link/disasm.lib")      ~= 0 then return 1 end
        if run_nmake(link_dir .. "/DISASM68",  "link/disasm68.lib")    ~= 0 then return 1 end
        if run_nmake(link_dir .. "/COFF",      "link/coff (link.exe)") ~= 0 then return 1 end

        if not install_host_tool(link_dir .. "/COFF/obj/i386/link.exe", "LINK.EXE") then
            return 1
        end
        log(">>> LINK.EXE rebuilt with error message resources")
        return 0
    end

    -- ----- cmd-stub (minimal cmd.exe replacement for NMAKE COMSPEC) ------
    -- Self-bootstrap dependency: must run before any wibo-tools invocation
    -- that touches COMSPEC.  Uses a stripped env (no wibo NT_ENV) because
    -- COMSPEC isn't wired until after this completes.
    targets.cmdstub = function()
        local src_dir = SCRIPT_DIR .. "/cmd-stub"
        if not file_exists(src_dir .. "/cmd.c") then
            log("ERROR: cmd-stub source not found at " .. src_dir .. "/cmd.c")
            return 1
        end

        banner("cmd-stub (NMAKE COMSPEC replacement)")

        platform.unlink(src_dir .. "/cmd.obj")
        platform.unlink(src_dir .. "/cmd.exe")

        -- Stripped env — no NT_ENV here because COMSPEC isn't wired yet.
        local env = {
            "HOME=" .. (platform.getenv("HOME") or ""),
            "TERM=" .. (platform.getenv("TERM") or "dumb"),
            "INCLUDE=" .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC;"
                       .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC\\CRT",
            "LIB=" .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386",
            "PATH=" .. WIBO_TOOLS_WIN,
            "WIBO_PATH=" .. WIBO_TOOLS,
        }
        local wibo_dbg = platform.getenv("WIBO_DEBUG")
        if wibo_dbg then env[#env + 1] = "WIBO_DEBUG=" .. wibo_dbg end

        local argv = {
            "wibo", "--chdir", src_dir,
            WIBO_TOOLS .. "/CL.EXE", "-nologo", "cmd.c",
            "-link", "-subsystem:console", "-out:cmd.exe",
            NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\libc.lib",
            NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\kernel32.lib",
        }
        local rc = platform.spawn_wait{ argv = argv, env = env, path = WIBO_BIN }
        if rc ~= 0 then
            log(">>> cmd-stub: FAILED")
            return rc
        end

        if not copy_file(src_dir .. "/cmd.exe", WIBO_TOOLS .. "/cmd.exe") then
            log("ERROR: cp cmd.exe -> wibo-tools failed")
            return 1
        end
        log(">>> cmd-stub: " .. WIBO_TOOLS .. "/cmd.exe installed")
        return 0
    end

    -- ----- MC (message compiler) — direct CL invocations, can't go through
    -- nmake without tripping LINK.EXE's bufio assertion under wibo.
    -- ----------------------------------------------------------------------
    targets.mc = function()
        local mc_dir  = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/MC"
        local obj_dir = mc_dir .. "/obj/i386"
        banner("MC - message compiler (patched for wibo)")
        mkdir_p(obj_dir)

        -- Original NT 3.5 SOURCES file has no UNICODE define; we follow
        -- suit so any LoadLibraryA/W macro expansions stay ANSI (the
        -- mc.exe upstream source isn't TCHAR-clean for ANSI-literal
        -- DLL names).  user32.lib + advapi32.lib both dropped from
        -- the link — mc.exe runs only as a host build tool under wibo,
        -- whose ntdll/advapi32 shims don't implement IsTextUnicode or
        -- RtlIsTextUnicode.  MCUTIL.C now hard-codes IsFileUnicode to
        -- FALSE (correct for our always-ANSI .mc inputs), removing
        -- the runtime GetProcAddress dance entirely.  cmd.exe runs
        -- natively on guest and uses the real ntdll RtlIsTextUnicode.
        local cflags = {
            "-nologo", "-c",
            "-I", ".",
            "-D_X86_=1", "-Di386=1", "-DWIN32_LEAN_AND_MEAN=1", "-DWIN32=100",
            "-DCOMMAND=1", "-DENABLE_NLS=0",
            "-DSTD_CALL", "-DCONDITION_HANDLING=1",
            "-DDBG=0", "-DDEVL=1",
        }

        for _, src in ipairs({ "mc", "mclex", "mcparse", "mcout", "mcutil" }) do
            log(">>> CL " .. src .. ".c")
            local cl_args = {}
            for _, f in ipairs(cflags) do cl_args[#cl_args + 1] = f end
            cl_args[#cl_args + 1] = "-Fo" .. "obj/i386/" .. src .. ".obj"
            cl_args[#cl_args + 1] = src .. ".c"
            if run_wibo_tool(mc_dir, "CL", unpack(cl_args)) ~= 0 then return 1 end
        end

        log(">>> LINK mc.exe")
        if run_wibo_tool(mc_dir, "LINK",
            "-nologo", "-subsystem:console", "-machine:i386",
            "-out:obj/i386/mc.exe",
            "obj/i386/mc.obj", "obj/i386/mclex.obj", "obj/i386/mcparse.obj",
            "obj/i386/mcout.obj", "obj/i386/mcutil.obj",
            "libc.lib", "kernel32.lib") ~= 0 then
            return 1
        end

        if not install_host_tool(obj_dir .. "/mc.exe", "MC.EXE") then
            return 1
        end
        return 0
    end

    -- ----- RC.EXE + RCDLL.DLL ---------------------------------------------
    targets.rc = function()
        local rcdll_dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/RCDLL"
        local rc_dir    = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/RC"
        banner("RC.EXE + RCDLL.DLL (resource compiler from source)")

        if run_nmake(rcdll_dir, "RCDLL - rcdll.dll", { "makedll=1" }) ~= 0 then return 1 end
        if run_nmake(rc_dir, "RC - rc.exe", {}, { keep_umappl = true }) ~= 0 then return 1 end
        if not install_host_tool(rcdll_dir .. "/obj/i386/rcdll.dll", "RCDLL.DLL") then return 1 end
        if not install_host_tool(rc_dir    .. "/obj/i386/rc.exe",    "RC.EXE")    then return 1 end
        return 0
    end

    -- ----- EFI / cr / disk — host-side helpers ----------------------------
    targets.efi = function()
        banner("UEFI bootloader (BOOTX64.EFI)")
        return run_make(SCRIPT_DIR .. "/boot-efi", "BOOTX64.EFI")
    end

    targets.cr = function()
        banner("cr (LuaJIT runtime + lua/ tree)")
        return run_make(SCRIPT_DIR .. "/cr", nil)
    end

    -- Disk target: hive + ESP image, both built via pkg/ntosbe (Lua port
    -- of the historical tools/mkhive.py + tools/mkdisk.py pair).  Zero
    -- Python dependency on this path; everything in-tree.
    targets.disk = function()
        local out_dir = REPO_ROOT .. "/build/disk"
        local efi_bin = SCRIPT_DIR .. "/boot-efi/BOOTX64.EFI"
        banner("boot disk image")

        if not file_exists(efi_bin) then
            if targets.efi() ~= 0 then return 1 end
        end

        mkdir_p(out_dir)
        return ntosbe.build_image {
            profile    = "ide",
            efi_binary = efi_bin,
            output_dir = out_dir,
            src_root   = SCRIPT_DIR,
        }
    end

    -- ------------------------------------------------------------------
    -- Group targets — order inside each list matters (deps first).
    -- ------------------------------------------------------------------

    local TOOL_TARGETS = {
        "link", "mc", "rc", "gensrv",
    }

    local NTOSKRNL_TARGETS = {
        "geni386",
        "ke", "rtl", "ex", "ob", "se", "ps", "mm", "cache", "config",
        "lpc", "dbgk", "io", "kd", "fsrtl", "raw", "vdm",
        "init",
        "hal",
    }

    local DRIVER_TARGETS = {
        "atdisk", "null", "fastfat", "npfs", "msfs", "serial",
        "i8042prt", "kbdclass", "mouclass",
        "vga_miniport", "bochsvga",
        "ndis_wrapper",
        "virtio",
        "dd_class", "dd_scsiport", "dd_scsidisk", "dd_nvme2k",
        "tdi_wrapper", "tdi_tcpip_ip", "tdi_tcpip_tcp", "afd",
    }

    local USERLAND_TARGETS = {
        "rtl_user", "ntdll", "urtl", "windows_base_client", "cmd",
    }

    local function build_group(name, list)
        log("")
        log("########################################")
        log("# Group: " .. name)
        log("########################################")
        for _, t in ipairs(list) do
            local rc = (targets[t] or function()
                platform.log("Unknown target in group '" .. name .. "': " .. t)
                return 1
            end)()
            if rc ~= 0 then return rc end
        end
        return 0
    end

    targets.tools    = function() return build_group("tools",    TOOL_TARGETS)    end
    targets.ntoskrnl = function() return build_group("ntoskrnl", NTOSKRNL_TARGETS) end
    targets.drivers  = function() return build_group("drivers",  DRIVER_TARGETS)  end
    targets.userland = function() return build_group("userland", USERLAND_TARGETS) end

    targets.all = function()
        for _, g in ipairs({ "tools", "ntoskrnl", "drivers", "userland", "cr", "disk" }) do
            local rc = targets[g]()
            if rc ~= 0 then return rc end
        end
        return 0
    end

    -- ------------------------------------------------------------------
    -- Clean targets — `clean` does the full nuke, `clean:<name>` drops
    -- just one component's obj/ tree, `clean:<group>` recurses.
    -- ------------------------------------------------------------------

    clean_dirs.rtl       = { NTOS .. "/RTL/UP" }
    clean_dirs.rtl_user  = { NTOS .. "/RTL/USER", NTOS .. "/RTL/DAYTONA" }
    clean_dirs.gensrv    = { NT_ROOT .. "/PRIVATE/SDKTOOLS/GENSRV" }
    clean_dirs.ntdll     = { NTOS .. "/DLL/DAYTONA" }
    clean_dirs.hal_stubs = { NTOS .. "/NTHALS/HAL" }
    clean_dirs.hal       = { NTOS .. "/NTHALS/HAL" }
    clean_dirs.init      = { NTOS .. "/INIT/UP" }
    clean_dirs.geni386   = { NTOS .. "/INIT" }
    clean_dirs.mc        = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/MC" }
    clean_dirs.rc        = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/RC",
                             NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/RCDLL" }
    clean_dirs.tdi_tcpip_ip   = { NTOS .. "/TDI/TCPIP/IP", NTOS .. "/TDI/TCPIP" }
    clean_dirs.vga_miniport   = { NTOS .. "/VIDEO/VGA" }

    -- Composites — clean each member's dir.
    clean_dirs.virtio = {
        NTOS .. "/VIRTIO",
        NTOS .. "/DD/VIORNG",
        NTOS .. "/DD/VIOSER",
        NTOS .. "/DD/VIOINPUT",
        NTOS .. "/NDIS/VIONET",     -- ndis_vionet's source dir; harmless if absent
    }

    -- Group → list of member targets.
    local CLEAN_GROUPS = {
        tools    = TOOL_TARGETS,
        ntoskrnl = NTOSKRNL_TARGETS,
        drivers  = DRIVER_TARGETS,
        userland = USERLAND_TARGETS,
    }

    local function rmrf(path)
        if not file_exists(path) then return end
        if platform.rmrf(path) then
            log("  cleaned " .. path:gsub(SCRIPT_DIR .. "/", ""))
        else
            log("rmrf " .. path .. " failed")
        end
    end

    -- Glob → Lua pattern.  Supports `*`, `?`, character classes
    -- `[...]` (incl. ranges).  Used by the clean machinery.
    local function glob_to_pattern(glob)
        local out = "^"
        local i = 1
        while i <= #glob do
            local c = glob:sub(i, i)
            if c == "*" then
                out = out .. ".*"
            elseif c == "?" then
                out = out .. "."
            elseif c == "[" then
                local j = glob:find("%]", i + 1)
                if not j then
                    error("glob_to_pattern: unterminated [ in " .. glob)
                end
                out = out .. glob:sub(i, j)
                i = j
            elseif c:match("[%-%.%%%+%(%)%^%$]") then
                out = out .. "%" .. c
            else
                out = out .. c
            end
            i = i + 1
        end
        return out .. "$"
    end

    -- Recursive name-matched walk (find -maxdepth N -name <pattern>).
    local function find_named(root, max_depth, pattern, on_match)
        local function visit(path, depth)
            for _, name in ipairs(platform.list_dir(path)) do
                local sub = path .. "/" .. name
                local is_dir = platform.is_dir(sub)
                if name:match(pattern) then
                    on_match(sub, is_dir)
                end
                if is_dir and depth < max_depth then
                    visit(sub, depth + 1)
                end
            end
        end
        if platform.is_dir(root) then visit(root, 1) end
    end

    -- Per-component clean: blow away obj/ under each registered source dir.
    local function clean_one(name)
        -- Special cases that delegate to peer Makefiles.
        if name == "cr" then
            log("Cleaning cr/ ...")
            return platform.spawn_wait{
                argv = { "make", "-C", SCRIPT_DIR .. "/cr", "clean" },
                search_path = true,
            }
        end
        if name == "efi" then
            log("Cleaning boot-efi/ ...")
            return platform.spawn_wait{
                argv = { "make", "-C", SCRIPT_DIR .. "/boot-efi", "clean" },
                search_path = true,
            }
        end
        if name == "disk" then
            log("Cleaning build/disk/ ...")
            rmrf(REPO_ROOT .. "/build/disk")
            return 0
        end

        -- Group recursion.
        local group = CLEAN_GROUPS[name]
        if group then
            log("Cleaning group: " .. name)
            for _, t in ipairs(group) do clean_one(t) end
            return 0
        end

        -- Per-component obj clean.
        local dirs = clean_dirs[name]
        if not dirs then
            platform.log("clean: unknown target '" .. name .. "'")
            return 1
        end
        log("Cleaning " .. name)
        for _, d in ipairs(dirs) do rmrf(d .. "/obj") end
        return 0
    end

    -- Full clean — port of the historical clean.sh.
    targets.clean = function()
        log("########################################")
        log("# Full clean")
        log("########################################")

        -- Every per-component obj/ tree under NT/PRIVATE.
        local nt_priv = NT_ROOT .. "/PRIVATE"
        local obj_dirs = {}
        find_named(nt_priv, 10, "^obj$", function(p, is_dir)
            if is_dir then obj_dirs[#obj_dirs + 1] = p end
        end)
        for _, d in ipairs(obj_dirs) do rmrf(d) end

        -- Aggregated TARGETPATH dirs (component .lib files land here).
        rmrf(NTOS .. "/obj")
        rmrf(NTOS .. "/RTL/obj")
        rmrf(NT_ROOT .. "/PRIVATE/WINDOWS/BASE/obj")
        rmrf(NT_ROOT .. "/PRIVATE/WINDOWS/obj")
        rmrf(NT_ROOT .. "/PRIVATE/RPC/MIDL20/lib")

        -- nmake / rc temp files.  rc.exe leaves R[CD]<letter><5digits>; nmake
        -- leaves nm<pid>.  Both have no other meaning so blanket-remove.
        local temp_files = {}
        find_named(nt_priv, 10,
                   glob_to_pattern("R[CD][a-z][0-9][0-9][0-9][0-9][0-9]"),
                   function(p, is_dir)
                       if not is_dir then temp_files[#temp_files + 1] = p end
                   end)
        find_named(nt_priv, 10, glob_to_pattern("nm[0-9]*"),
                   function(p, is_dir)
                       if not is_dir then temp_files[#temp_files + 1] = p end
                   end)
        for _, p in ipairs(temp_files) do platform.unlink(p) end

        -- Generated headers / message resources (rebuilt on demand).
        platform.unlink(NT_ROOT .. "/PRIVATE/WINDOWS/GDI/INC/GDII386.INC")
        for _, f in ipairs({
            "PRIVATE/WINDOWS/NLSMSG/winerror.h",
            "PRIVATE/WINDOWS/NLSMSG/winerror.rc",
            "PRIVATE/WINDOWS/NLSMSG/MSG00001.bin",
            "PRIVATE/WINDOWS/BASE/CLIENT/winerror.rc",
            "PRIVATE/WINDOWS/BASE/CLIENT/DAYTONA/MSG00001.bin",
        }) do platform.unlink(NT_ROOT .. "/" .. f) end

        -- MIDL-generated stubs (RPC / IDL clients/servers).
        for _, d in ipairs({
            "PRIVATE/RPC/RUNTIME/RTIFS",
            "PRIVATE/RPC/RUNTIME/MTRT",
            "PRIVATE/WINDOWS/SCREG/WINREG",
            "PRIVATE/WINDOWS/SCREG/SC",
            "PRIVATE/EVENTLOG",
            "PRIVATE/LSA",
            "PRIVATE/NEWSAM",
        }) do
            for _, pat in ipairs({ "*_c.c", "*_s.c", "*rpc.h", "*rpc_c.h" }) do
                local matches = {}
                find_named(NT_ROOT .. "/" .. d, 2, glob_to_pattern(pat),
                           function(p, is_dir)
                               if not is_dir then matches[#matches + 1] = p end
                           end)
                for _, p in ipairs(matches) do platform.unlink(p) end
            end
        end

        -- PUBLIC/SDK/LIB/I386 outputs we produce.  Anything imported from
        -- the bootstrap libs (LINK.EXE, RC.EXE, ntdll.lib pre-builds)
        -- stays.
        local public_lib = NT_ROOT .. "/PUBLIC/SDK/LIB/I386"
        for _, f in ipairs({
            "ntoskrnl.lib", "ntoskrnl.exp", "hal.exp", "tmp.lib", "tmp.exp",
            "ntdll.dll", "ntdll.exp",
            "kernel32.dll", "kernel32.exp",
            "advapi32.dll", "advapi32.exp",
            "rpcrt4.dll", "rpcrt4.exp", "rpcrt4.lib",
            "samlib.dll", "samlib.exp", "samlib.lib",
            "samsrv.dll", "samsrv.exp", "samsrv.lib",
            "lsasrv.dll", "lsasrv.exp", "lsasrv.lib",
            "csrsrv.dll", "csrsrv.exp", "csrsrv.lib",
            "basesrv.dll", "basesrv.exp", "basesrv.lib",
            "atdisk.sys", "null.sys", "fastfat.sys",
            "class.lib", "scsiport.lib", "scsiport.exp", "scsiport.sys",
            "scsidisk.sys", "nvme2k.sys",
            "ndis.lib", "ndis.exp", "ndis.sys",
            "tdi.lib", "tdi.exp", "tdi.sys", "tcpip.sys", "vionet.sys",
            "gdisrvl.lib", "efloat.lib", "fscaler.lib", "ttfd.lib",
            "bmfd.lib", "vtfd.lib", "halftone.lib",
            "gdi32.dll", "gdi32.exp", "gdi32p.exp", "gdi32p.lib",
            "usersrvl.lib",
            "user32.dll", "user32.exp", "user32p.exp", "user32p.lib",
            "userexts.dll", "userexts.exp", "userexts.lib",
            "consrvl.lib",
            "conexts.dll", "conexts.exp", "conexts.lib",
            "winsrv.dll", "winsrv.exp", "winsrv.lib",
            "lsadll.lib",
        }) do
            local p = public_lib .. "/" .. f
            if file_exists(p) then
                platform.unlink(p)
                log("  cleaned PUBLIC/SDK/LIB/I386/" .. f)
            end
        end

        -- USER files generated by listmung from .TPL + .LST.
        for _, f in ipairs({
            "PRIVATE/WINDOWS/USER/INC/callback.h",
            "PRIVATE/WINDOWS/USER/INC/csuser.h",
            "PRIVATE/WINDOWS/USER/INC/cscall.h",
            "PRIVATE/WINDOWS/USER/SERVER/dispcf.c",
            "PRIVATE/WINDOWS/USER/SERVER/callcf.c",
            "PRIVATE/WINDOWS/USER/CLIENT/dispcb.c",
            "PRIVATE/WINDOWS/USER/CLIENT/user32p.def",
        }) do platform.unlink(NT_ROOT .. "/" .. f) end

        -- cmd-stub + the wibo-tools symlink farm; both auto-provisioned on
        -- the next build.
        platform.unlink(SCRIPT_DIR .. "/cmd-stub/cmd.obj")
        platform.unlink(SCRIPT_DIR .. "/cmd-stub/cmd.exe")
        rmrf(SCRIPT_DIR .. "/wibo-tools")

        -- Profile-specific disk images under build/.
        for _, profile in ipairs({ "disk", "micront", "headless", "gui" }) do
            rmrf(REPO_ROOT .. "/build/" .. profile)
        end

        -- Delegate to peer Makefiles for the cr + boot-efi trees.
        clean_one("cr")
        clean_one("efi")

        log("Clean complete.")
        return 0
    end

    -- ------------------------------------------------------------------
    -- Self-bootstrap of cmd-stub before any wibo invocation that touches
    -- COMSPEC.  Same idempotent guard as build.sh.
    -- ------------------------------------------------------------------

    local function newer_than(a, b)
        local am, bm = mtime(a), mtime(b)
        return am and bm and am > bm
    end

    local function bootstrap_cmdstub_if_needed()
        local cmd_exe = WIBO_TOOLS .. "/cmd.exe"
        local cmd_src = SCRIPT_DIR .. "/cmd-stub/cmd.c"
        if not file_exists(cmd_exe)
           or (file_exists(cmd_src) and newer_than(cmd_src, cmd_exe)) then
            local rc = targets.cmdstub()
            if rc ~= 0 then return rc end
        end
        return 0
    end

    -- ------------------------------------------------------------------
    -- Dispatch.
    -- ------------------------------------------------------------------

    local function usage()
        platform.log("Usage: build.sh [--debug] [<target> ...]")
        platform.log("")
        platform.log("No arguments → builds 'all' (every group + cr + disk).")
        platform.log("")
        platform.log("Top-level targets:")
        platform.log("  all, tools, ntoskrnl, drivers, userland, cr, efi, disk")
        platform.log("")
        platform.log("Individual components (build order within each group):")
        platform.log("  tools:    " .. table.concat(TOOL_TARGETS,    ", "))
        platform.log("  ntoskrnl: " .. table.concat(NTOSKRNL_TARGETS, ", "))
        platform.log("  drivers:  " .. table.concat(DRIVER_TARGETS,  ", "))
        platform.log("  userland: " .. table.concat(USERLAND_TARGETS, ", "))
        platform.log("")
        platform.log("Cleaning:")
        platform.log("  clean              — full nuke (every obj/, generated headers,")
        platform.log("                       PUBLIC/SDK/LIB outputs, wibo-tools/, build/)")
        platform.log("  clean:<component>  — drop just that component's obj/ tree")
        platform.log("  clean:<group>      — recurse over the group's members")
        platform.log("  clean:cr           — delegates to make -C cr clean")
        platform.log("  clean:efi          — delegates to make -C boot-efi clean")
        platform.log("  clean:disk         — drops build/disk/")
    end

    -- Parse --debug etc. before target names.  WIBO_DEBUG is exported
    -- exactly the way build.sh does it, so build_envp's later getenv
    -- pulls it through to wibo.
    local positional = {}
    for _, a in ipairs(args) do
        if a == "--debug" then
            platform.setenv("WIBO_DEBUG", "1")
        elseif a:sub(1, 2) == "--" then
            platform.log("Unknown flag: " .. a)
            usage()
            return 1
        else
            positional[#positional + 1] = a
        end
    end

    if platform.on_host then
        local rc = bootstrap_cmdstub_if_needed()
        if rc ~= 0 then return rc end
    end

    if #positional == 0 then
        return targets.all()
    end

    for _, name in ipairs(positional) do
        -- clean:<X> → per-component / per-group cleanup.  Bare `clean`
        -- still routes through targets.clean above.
        local sub = name:match("^clean:(.+)$")
        if sub then
            local rc = clean_one(sub)
            if rc ~= 0 then return rc end
        else
            local fn = targets[name]
            if not fn then
                platform.log("Unknown target: " .. name)
                usage()
                return 1
            end
            local rc = fn() or 0
            if rc ~= 0 then return rc end
        end
    end
    return 0
end

return M
