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
--       args       = arg or {},                 -- positional + --wibo-trace etc.
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
    -- argv parsing — must happen before toolchain.configure so the
    -- debug-symbols flag reaches every nmake invocation.  Positional
    -- args (target names) are deferred for dispatch at the bottom.
    -- ----------------------------------------------------------------

    -- Build-mode default: ON.  /Z7 + -debugtype:cv adds CV records to
    -- .obj's and consolidates them into the PE's .debug section, which
    -- splitsym then extracts to <name>.DBG.  Final .sys/.dll/.exe is
    -- identical to a retail build except for a 28-byte
    -- IMAGE_DEBUG_DIRECTORY entry pointing at the sidecar.  The cost
    -- is purely build-time / build-tree disk; ship binaries are
    -- unaffected.  --no-syms opts out for retail-only iteration.
    local debug_symbols = true
    local positional = {}
    for _, a in ipairs(args) do
        if a == "--wibo-trace" then
            if not platform.on_host then
                platform.log("--wibo-trace is host-only (wibo runs on Linux);"
                          .. " self-hosted NT builds have no wibo to trace.")
                return 1
            end
            -- wibo-specific: emit one trace line per intercepted syscall.
            platform.setenv("WIBO_DEBUG", "1")
        elseif a == "--syms" or a == "--debug-symbols" then
            debug_symbols = true   -- explicit no-op (default is on)
        elseif a == "--no-syms" then
            debug_symbols = false
        elseif a:sub(1, 2) == "--" then
            platform.log("Unknown flag: " .. a)
            -- usage() not yet defined this early; fall through.
            return 1
        else
            positional[#positional + 1] = a
        end
    end

    -- ----------------------------------------------------------------
    -- Toolchain bridge.  configure() picks host vs NT, prepares NT_ENV
    -- and (host only) populates the wibo-tools symlink farm.
    -- ----------------------------------------------------------------

    toolchain.configure{
        nt_root        = NT_ROOT,
        wibo_tools     = WIBO_TOOLS,
        wibo_bin       = platform.on_host and WIBO_BIN or nil,
        drive_root     = DRIVE_ROOT,
        path_strip     = opts.path_strip,
        debug_symbols  = debug_symbols,
    }

    local path_to_win        = toolchain.path_to_win
    local NT_ROOT_WIN        = path_to_win(NT_ROOT)
    local WIBO_TOOLS_WIN     = path_to_win(WIBO_TOOLS)
    local build_envp         = toolchain.build_envp
    local run_nmake          = toolchain.run_nmake
    local run_wibo_tool      = toolchain.run_wibo_tool
    local wibo_spawn_args    = toolchain.wibo_spawn_args
    -- Imported raw; wrapped further down (after run_splitsym + run_dbg2dwf
    -- are in scope) so every PE host-tool install also gets a .DBG/.dwf
    -- next to its source binary.  Call sites use the wrapped form below.
    local install_host_tool_raw = toolchain.install_host_tool

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

    -- run_splitsym — invoke wibo-tools/SPLITSYM.EXE with cwd=wibo-tools
    -- so wibo's dll loader resolves the imagehlp.dll sibling.  -a forces
    -- extraction of every CV section into the sidecar .DBG.  The image
    -- arg is converted to Z:\... because cwd is no longer the build dir.
    local function run_splitsym(image_path)
        if not platform.file_exists(image_path) then
            log("!!! splitsym: image not found: " .. image_path)
            return 1
        end
        local img_win = path_to_win(image_path)
        log(">>> SPLITSYM -a " .. img_win)
        return run_wibo_tool(WIBO_TOOLS, "SPLITSYM.EXE", "-a", img_win)
    end

    -- run_dbg2dwf — convert the sidecar .DBG (produced by splitsym)
    -- into a gdb-loadable DWARF ELF placed next to it as .dwf.
    -- Same wibo-tools cwd discipline as run_splitsym (DBG2DWF.EXE
    -- doesn't actually need imagehlp.dll at runtime today, but keeping
    -- the same call style avoids surprises if it grows that dep).
    local function run_dbg2dwf(image_path)
        local dbg_path = image_path:gsub("%.[^.]+$", ".DBG")
        if not platform.file_exists(dbg_path) then
            log("!!! dbg2dwf: missing sidecar: " .. dbg_path)
            return 1
        end
        local elf_path  = image_path:gsub("%.[^.]+$", ".dwf")
        local dbg_win   = path_to_win(dbg_path)
        local elf_win   = path_to_win(elf_path)
        log(">>> DBG2DWF " .. elf_win)
        return run_wibo_tool(WIBO_TOOLS, "DBG2DWF.EXE", dbg_win, elf_win)
    end

    -- pe_has_debug_dir — does this PE have a non-empty IMAGE_DEBUG_DIRECTORY?
    -- Without one, splitsym has nothing to extract, so we skip the symbol
    -- pass instead of treating it as a failure.  An empty Debug Directory
    -- in a target indicates a build-flag bug (its link rule is missing
    -- -debug:full -debugtype:cv) and is worth flagging at install time so
    -- it's visible in the build log, but not worth aborting the build for.
    local function pe_has_debug_dir(path)
        -- platform.read_file works on host (LuaJIT + io) and guest
        -- (run.exe + ntdll FFI); io.open does not exist on the guest
        -- because cr's libc-free LuaJIT has no CRT-backed io library.
        local data = platform.read_file(path)
        if not data or #data < 0x40 then return false end
        local function rd_u32(off)
            -- off is 0-based; lua strings are 1-based
            local b1, b2, b3, b4 = data:byte(off + 1, off + 4)
            if not b4 then return nil end
            return bit.bor(b1, bit.lshift(b2, 8),
                               bit.lshift(b3, 16),
                               bit.lshift(b4, 24))
        end
        local pe_off = rd_u32(0x3c)
        if not pe_off then return false end
        -- DataDirectory[6] (Debug) lives at OptionalHeader+144 in PE32:
        --   24 (sig + COFF header) + 96 (std OptHdr) + 6*8 (dir entries) = 192
        local sz = rd_u32(pe_off + 24 + 96 + 6*8 + 4)
        return sz ~= nil and sz > 0
    end

    -- install_host_tool — wrapper around toolchain.install_host_tool that
    -- ALSO runs splitsym + dbg2dwf on the source binary whenever it's
    -- a PE (.exe / .dll) with embedded debug info, so every host tool
    -- consistently produces a <name>.DBG and <name>.dwf next to its
    -- obj/i386/<name>.exe.  Non-PE artifacts (.err / .lib / etc.) skip
    -- the symbol pass; PEs that were linked without -debug also skip
    -- (with a one-line note) so a single missing-debug target doesn't
    -- cascade-fail unrelated installs.  Gated on the global debug_symbols
    -- flag so --no-syms builds stay clean.
    --
    -- Why here and not on the toolchain side: run_splitsym and
    -- run_dbg2dwf are local closures over WIBO_TOOLS / log / file_exists
    -- and only make sense in the build-driver scope.
    local function install_host_tool(built, name)
        if not install_host_tool_raw(built, name) then return false end
        if not debug_symbols then return true end
        local lname = name:lower()
        if not (lname:match("%.exe$") or lname:match("%.dll$")) then
            return true
        end
        if not pe_has_debug_dir(built) then
            log("    " .. name .. ": no Debug Directory, skipping splitsym/dbg2dwf"
                .. " (link rule missing -debug:full -debugtype:cv?)")
            return true
        end
        local rc = run_splitsym(built)
        if rc == 0 then rc = run_dbg2dwf(built) end
        if rc ~= 0 then
            log("!!! " .. name .. ": symbol extraction failed (rc=" .. rc .. ")")
            return false
        end
        return true
    end

    -- splitsym_dir — splitsym every PE image in `dir` modified at or
    -- after `since`.  Skips files that already have a fresher .DBG
    -- next to them, so re-running on an up-to-date tree is a no-op.
    -- Used after each nmake build to scan PUBLIC/SDK/LIB/I386 and any
    -- per-component obj/i386 dir that holds a final .sys/.dll/.exe.
    -- No-op when --syms wasn't passed.
    local PUB_LIB = NT_ROOT .. "/PUBLIC/SDK/LIB/I386"

    local function splitsym_dir(dir, since)
        if not debug_symbols then return 0 end
        if not platform.is_dir(dir) then return 0 end
        for _, name in ipairs(platform.list_dir(dir)) do
            local lower = name:lower()
            if lower:match("%.sys$") or lower:match("%.dll$")
                                     or lower:match("%.exe$") then
                local full = dir .. "/" .. name
                local m = platform.mtime(full)
                if m and (not since or m >= since) then
                    local dbg = full:gsub("%.[^.]+$", ".DBG")
                    local dm = platform.mtime(dbg)
                    if not dm or dm < m then
                        local rc = run_splitsym(full)
                        if rc ~= 0 then return rc end
                    end
                    -- After (or alongside) splitsym, run dbg2dwf so
                    -- gdb gets a matching .dwf without a separate
                    -- step.  Skipped if the .dwf is already up
                    -- to date relative to the .DBG.
                    local elf      = full:gsub("%.[^.]+$", ".dwf")
                    local dbg_m    = platform.mtime(dbg)
                    local elf_m    = platform.mtime(elf)
                    if dbg_m and (not elf_m or elf_m < dbg_m) then
                        local rc2 = run_dbg2dwf(full)
                        if rc2 ~= 0 then return rc2 end
                    end
                end
            end
        end
        return 0
    end

    local function nmake_target(name, dir, desc, t_opts, ...)
        local extras = {...}
        targets[name] = function()
            if t_opts and t_opts.pre and not t_opts.pre() then return 1 end
            local since = platform.now()
            local rc = run_nmake(dir, desc, extras, t_opts)
            if rc ~= 0 then return rc end
            -- Auto-scan two locations for fresh PE images:
            --   PUB_LIB                — shared driver/SDK staging
            --   <dir>/obj/i386         — component's own EXE/DLL outputs
            -- Without the second scan, userland EXEs (link.exe, mkmsg,
            -- gensrv, cmd-stub, run.exe, …) never get .DBG/.dwf unless
            -- their target wires splitsym explicitly — exactly the kind
            -- of thing that rots and leaves an agent without symbols.
            local srcr = splitsym_dir(PUB_LIB, since)
            if srcr ~= 0 then return srcr end
            local srcr2 = splitsym_dir(dir .. "/obj/i386", since)
            if srcr2 ~= 0 then return srcr2 end
            if t_opts and t_opts.post then
                local prc = t_opts.post()
                if prc and prc ~= 0 then return prc end
            end
            return 0
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

    -- ----- File-system / I/O drivers -----
    nmake_target("atdisk",  NTOS .. "/DD/HARDDISK", "ATDISK - IDE disk driver")
    nmake_target("serial",  NTOS .. "/DD/SERIAL",   "SERIAL - NT 3.5 serial port driver",
                 { pre = ensure_serlog })
    nmake_target("null",    NTOS .. "/DD/NULL",     "NULL - null device driver")
    nmake_target("fastfat", NTOS .. "/FASTFAT",     "FASTFAT - FAT filesystem driver")
    -- LFS = Log File Service.  Static lib (lfs.lib) consumed by ntfs.sys
    -- for the transaction-log machinery; not a standalone driver.
    nmake_target("lfs",     NTOS .. "/LFS",         "LFS - Log File Service (lfs.lib for NTFS)")
    nmake_target("ntfs",    NTOS .. "/NTFS",        "NTFS - NTFS filesystem driver")
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
        local since = platform.now()
        local rc = run_nmake(WIN .. "/BASE/CLIENT",
                         "WINDOWS/BASE/CLIENT - kernel32.dll",
                         { "makedll=1" })
        if rc ~= 0 then return rc end
        return splitsym_dir(PUB_LIB, since)
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
    nmake_target("dd_vioblk",   NTOS .. "/DD/VIOBLK",
                 "VIOBLK - virtio-blk storage (SCSI miniport via virtio.lib)")

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

    -- ----- RTL — needs error.h + bugcodes.h generated first.  bugcodes.h
    -- is pulled in via ntos.h, so every RTL source needs it; ensure it
    -- here rather than relying on a build-order accident. -----
    targets.rtl = function()
        if not ensure_error_h() then return 1 end
        if not ensure_bugcodes() then return 1 end
        return run_nmake(NTOS .. "/RTL/UP", "RTL - Runtime Library")
    end

    -- ----- Userland NT runtime libs.  Same ntos.h → bugcodes.h need;
    -- this is what `imagehlp` (tools phase) pulls in, so the message
    -- compiler must already be built — see the TOOL_TARGETS ordering.
    targets.rtl_user = function()
        if not ensure_error_h() then return 1 end
        if not ensure_bugcodes() then return 1 end
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
        local since = platform.now()
        local rc = run_nmake(NTOS .. "/DLL/DAYTONA",
                         "NTDLL - user-mode runtime library",
                         { "makedll=1" })
        if rc ~= 0 then return rc end
        return splitsym_dir(PUB_LIB, since)
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
        local since = platform.now()
        local rc = run_nmake(hal_dir, "HAL - MicroNT HAL (lib)")
        if rc ~= 0 then return rc end

        banner("HAL - MicroNT HAL (DLL link)")
        mkdir_p(hal_dir .. "/obj/i386")

        -- Debug-symbols build flips the link to consolidate CV records
        -- into hal.dll's .debug section so splitsym below can extract
        -- them; otherwise stay with the lean retail flags.
        local dbg_link_flags = debug_symbols
            and { "-DEBUG:FULL", "-DEBUGTYPE:CV" }
            or  { "-DEBUG:MINIMAL", "-DEBUGTYPE:COFF" }
        rc = run_wibo_tool(hal_dir, "link",
            "-NOLOGO",
            "-OUT:obj\\i386\\hal.dll", "-DLL", "-MACHINE:i386",
            "-BASE:0x80400000", "-SUBSYSTEM:NATIVE", "-ENTRY:HalInitSystem@8",
            "-NODEFAULTLIB", "-RELEASE",
            dbg_link_flags[1], dbg_link_flags[2],
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
        local srcr = splitsym_dir(hal_dir .. "/obj/i386", since)
        if srcr ~= 0 then return srcr end
        return 0
    end

    -- ----- INIT — links every kernel .lib into ntoskrnl.exe.  Special: we
    -- must NOT override NTTEST (NMAKE uses NTTEST=ntoskrnl to drive the
    -- exe build via MAKEFILE.DEF).  bug-codes generated first.
    -- ----------------------------------------------------------------------
    targets.init = function()
        if targets.hal_stubs() ~= 0 then return 1 end
        if not ensure_bugcodes() then return 1 end
        local since = platform.now()
        -- INIT's SOURCES depends on NTTEST=ntoskrnl reaching NMAKE so
        -- MAKEFILE.DEF selects the ntoskrnl.exe link rule.  Use the
        -- standard run_nmake with keep_nttest=true; this dispatches
        -- through tchain.spawn_tool which handles host (wibo) and
        -- guest (native NT) uniformly.
        local rc = run_nmake(NTOS .. "/INIT/UP",
                         "INIT - NTOSKRNL.EXE",
                         nil,
                         { keep_nttest = true })
        if rc ~= 0 then return rc end
        -- ntoskrnl.exe lives at INIT/UP/obj/i386/ — TARGETPATH=..\..\obj
        -- in INIT/UP/SOURCES governs the .lib output, not the .exe.
        -- Scan that dir plus the shared NTOS/obj and the public lib
        -- staging dir (some component .lib files can land in any of
        -- these depending on the target's TARGETPATH).
        for _, d in ipairs({
            NTOS .. "/INIT/UP/obj/i386",
            NTOS .. "/obj/i386",
            PUB_LIB,
        }) do
            local srcr = splitsym_dir(d, since)
            if srcr ~= 0 then return srcr end
        end
        return 0
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

        local link_exe = link_dir .. "/COFF/obj/i386/link.exe"
        if not install_host_tool(link_exe, "LINK.EXE") then
            return 1
        end
        log(">>> LINK.EXE rebuilt with error message resources")
        return 0
    end
    -- LINK pulls five sibling .lib's together (CVTOMF / DISASM /
    -- DISASM68 / STUBS / the COFF link itself).  PDB/DBI is shared
    -- with cvpack so we deliberately leave it out of `clean:link` —
    -- a separate `clean:cvpack` pass handles that one if needed.
    clean_dirs.link = {
        NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/LINK/CVTOMF",
        NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/LINK/DISASM",
        NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/LINK/DISASM68",
        NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/LINK/STUBS",
        NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/LINK/COFF",
    }

    -- ----- MKMSG — message-resource compiler (host EXE).
    -- Tiny single-source tool; cvpack needs it to turn its msg.us /
    -- msg.eng files into .err + .h.  Built into wibo-tools so cvpack's
    -- nmake rule can spawn it via $(MKMSG_DIR)\mkmsg.
    targets.mkmsg = function()
        local dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/MSG"
        banner("MKMSG - message-resource compiler")
        if run_nmake(dir, "MKMSG - message-resource compiler") ~= 0 then return 1 end
        if not install_host_tool(dir .. "/obj/i386/mkmsg.exe", "MKMSG.EXE") then
            return 1
        end
        return 0
    end
    clean_dirs.mkmsg = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/MSG" }

    -- ----- CVPACK — post-LINK CodeView packer.  LINK 2.50 invokes
    -- cvpack.exe automatically when -debugtype:cv is on; without it
    -- we get LNK4027 (warning) → 0xff exit (fatal).  Pulls dbi.lib
    -- (already built by targets.link / targets.pdbdump) and mkmsg
    -- (built immediately above).
    targets.cvpack = function()
        if targets.mkmsg() ~= 0 then return 1 end
        local pdb_dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/PDB"
        if run_nmake(pdb_dir .. "/DBI", "pdb/dbi.lib") ~= 0 then return 1 end
        local dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/CVPACK"
        banner("CVPACK - CodeView packer")
        if run_nmake(dir, "CVPACK - CodeView packer") ~= 0 then return 1 end
        if not install_host_tool(dir .. "/obj/i386/cvpack.exe", "CVPACK.EXE") then
            return 1
        end
        -- cvpack looks up its localized error strings in cvpack.err next
        -- to the binary; without it we get the runtime "missing cvpack.err"
        -- warning and only numeric codes on errors.  Same install pattern
        -- as LINK.ERR / CL.ERR / RCPP.ERR already in wibo-tools.
        if not install_host_tool(dir .. "/obj/i386/cvpack.err", "cvpack.err") then
            return 1
        end
        return 0
    end
    clean_dirs.cvpack = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/CVPACK" }

    -- ----- IMAGEHLP — imagehlp.dll + the bind/binplace/splitsym/...
    -- post-link utilities.  splitsym is the era-native CV→.DBG
    -- extractor MAKEFILE.DEF's BINPLACE_CMD hook invokes when a
    -- component sets NTDEBUGTYPE=windbg.  binplace tags along (also
    -- handy for future _NTTREE-style staging).  Other tools (bind,
    -- rebase, editsym, ...) are produced as a side effect but not
    -- installed — the SOURCES is single-shot and rebuilds them all
    -- in one nmake invocation.
    targets.imagehlp = function()
        local dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/IMAGEHLP"
        banner("IMAGEHLP - imagehlp.dll + splitsym + binplace")
        -- IMAGEHLP/MAKEFILE.INC adds rtl_user's imagedir.obj to its
        -- OBJECTS list (BASEDIR\private\ntos\rtl\user\obj\i386\imagedir.obj).
        -- Build rtl_user first so that .obj exists.
        if targets.rtl_user() ~= 0 then return 1 end
        if run_nmake(dir, "IMAGEHLP - imagehlp.dll + utilities",
                     { "makedll=1" }, { keep_umappl = true }) ~= 0 then
            return 1
        end
        if not install_host_tool(
                NT_ROOT .. "/PUBLIC/SDK/LIB/I386/imagehlp.dll",
                "IMAGEHLP.DLL") then return 1 end
        if not install_host_tool(dir .. "/obj/i386/splitsym.exe",
                                 "SPLITSYM.EXE") then return 1 end
        if not install_host_tool(dir .. "/obj/i386/binplace.exe",
                                 "BINPLACE.EXE") then return 1 end
        return 0
    end
    clean_dirs.imagehlp = { NT_ROOT .. "/PRIVATE/SDKTOOLS/IMAGEHLP" }

    -- ----- CVDUMP — CodeView records inspector (reads .obj/.exe/.dbg).
    -- Counterpart of pdbdump for the .DBG sidecar workflow: pdbdump
    -- knows PDB 2.0 streams; cvdump walks the raw CV4 records that
    -- splitsym extracted out of the PE.  Links against imagehlp.lib
    -- (the import lib next to imagehlp.dll, both built by
    -- targets.imagehlp).
    targets.cvdump = function()
        if targets.imagehlp() ~= 0 then return 1 end
        local dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/CVDUMP"
        banner("CVDUMP - CodeView inspector")
        if run_nmake(dir, "CVDUMP - CodeView inspector") ~= 0 then return 1 end
        if not install_host_tool(dir .. "/obj/i386/cvdump.exe", "CVDUMP.EXE") then
            return 1
        end
        return 0
    end
    clean_dirs.cvdump = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/CVDUMP" }

    -- ----- DBG2DWF — CV4 sidecar .DBG → DWARF-2 ELF for gdb.
    -- Same shape as cvdump's target: links against imagehlp.lib (which
    -- exposes MapDebugInformation for the CV blob), emits an ELF with
    -- .symtab + .debug_line + .debug_info + .debug_abbrev + .debug_str.
    -- The output is consumed by gdb via `add-symbol-file <elf>`, no
    -- offset (symbol VAs are absolute = imagebase + RVA).
    targets.dbg2dwf = function()
        if targets.imagehlp() ~= 0 then return 1 end
        local dir = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/DBG2DWF"
        banner("DBG2DWF - .DBG → DWARF ELF")
        if run_nmake(dir, "DBG2DWF - .DBG to DWARF ELF") ~= 0 then return 1 end
        if not install_host_tool(dir .. "/obj/i386/dbg2dwf.exe", "DBG2DWF.EXE") then
            return 1
        end
        return 0
    end
    clean_dirs.dbg2dwf = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/DBG2DWF" }

    -- ----- PDBDUMP — PDB 2.0 inspector (lines/syms/types/secmap).
    -- Pulls dbi.lib via the same PDB/DBI nmake targets.link uses; running
    -- it twice is a no-op when the .lib is up to date, so pdbdump stays
    -- self-sufficient when invoked standalone (`build.sh pdbdump`).
    targets.pdbdump = function()
        local pdb_dir  = NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/PDB"
        local dump_dir = pdb_dir .. "/SRC/TOOLS/PDBDUMP"
        banner("PDBDUMP - PDB 2.0 inspector")

        if run_nmake(pdb_dir .. "/DBI", "pdb/dbi.lib") ~= 0 then return 1 end
        if run_nmake(dump_dir, "PDBDUMP - PDB inspector") ~= 0 then return 1 end

        if not install_host_tool(dump_dir .. "/obj/i386/pdbdump.exe", "PDBDUMP.EXE") then
            return 1
        end
        return 0
    end
    clean_dirs.pdbdump = { NT_ROOT .. "/PRIVATE/SDKTOOLS/VCTOOLS/PDB/SRC/TOOLS/PDBDUMP" }

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
    --
    -- Layout is fixed to 'split-fat' here (ESP FAT16 + system FAT16 —
    -- the canonical shape).  Operators wanting a different layout
    -- (single / split-ntfs) go through the Makefile route, which
    -- threads `--layout=` through ntosbe.main:
    --     make -C src disk LAYOUT=split-ntfs
    targets.disk = function()
        local out_dir = REPO_ROOT .. "/build/disk"
        local efi_bin = SCRIPT_DIR .. "/boot-efi/BOOTX64.EFI"
        local layout  = "split-fat"
        banner("boot disk image (layout=" .. layout .. ")")

        if not file_exists(efi_bin) then
            if targets.efi() ~= 0 then return 1 end
        end

        mkdir_p(out_dir)
        -- selfhost = the full disk (NT source tree + toolchain); the
        -- main.lua init override makes it an interactive image, the
        -- same shape `make boot` produces.
        return ntosbe.build_image {
            profile    = "selfhost",
            init       = { args = "\\SystemRoot\\lua\\main.lua" },
            efi_binary = efi_bin,
            output_dir = out_dir,
            src_root   = SCRIPT_DIR,
            layout     = layout,
        }
    end

    -- ------------------------------------------------------------------
    -- Group targets — order inside each list matters (deps first).
    -- ------------------------------------------------------------------

    -- `mc` builds early — right after `link` — because the message
    -- compiler is needed for codegen (ensure_bugcodes → bugcodes.h)
    -- before `imagehlp`: imagehlp links rtl_user's imagedir.obj, and
    -- rtl_user's sources include ntos.h → bugcodes.h.  mc itself only
    -- needs the CL/LINK seeds, so it has no earlier dependency.
    local TOOL_TARGETS = {
        "link", "mc", "mkmsg", "cvpack", "imagehlp", "cvdump", "dbg2dwf",
        "pdbdump", "rc", "gensrv",
    }

    local NTOSKRNL_TARGETS = {
        "geni386",
        "ke", "rtl", "ex", "ob", "se", "ps", "mm", "cache", "config",
        "lpc", "dbgk", "io", "kd", "fsrtl", "raw",
        "init",
        "hal",
    }

    local DRIVER_TARGETS = {
        "atdisk", "null", "fastfat", "lfs", "ntfs", "npfs", "msfs", "serial",
        "i8042prt", "kbdclass", "mouclass",
        "videoprt", "bochsvga",
        "ndis_wrapper",
        "virtio",
        "dd_class", "dd_scsiport", "dd_scsidisk", "dd_nvme2k", "dd_vioblk",
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

    -- `all` is the host build-everything target: every artifact —
    -- tools, kernel, drivers, userland, the cr runtime and the UEFI
    -- loader — but NOT a disk image.  Disk composition is a separate,
    -- profile-specific step (`build.sh disk`, `make disk` / smoke-disk
    -- / selftest / selfhost); baking one here would stage the whole NT
    -- source tree (the selfhost profile) on every plain `build.sh`.
    --
    -- `all` is host-only — `cr` (mingw) and `efi` (gcc/gnu-efi) go
    -- through run_make, which fails when not on_host.  The self-hosted
    -- build never runs `all`; test.ntosbe enumerates the four portable
    -- groups (tools/ntoskrnl/drivers/userland) explicitly.
    targets.all = function()
        for _, g in ipairs({ "tools", "ntoskrnl", "drivers", "userland", "cr", "efi" }) do
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
            log("Cleaning build/disk* ...")
            -- The default dir plus the per-profile dirs the Makefile
            -- composes into (build/disk-selftest, -selfhost, -smoke).
            rmrf(REPO_ROOT .. "/build/disk")
            rmrf(REPO_ROOT .. "/build/disk-selftest")
            rmrf(REPO_ROOT .. "/build/disk-selfhost")
            rmrf(REPO_ROOT .. "/build/disk-smoke")
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
            "atdisk.sys", "null.sys", "fastfat.sys", "ntfs.sys", "lfs.lib",
            "class.lib", "scsiport.lib", "scsiport.exp", "scsiport.sys",
            "scsidisk.sys", "nvme2k.sys", "vioblk.sys",
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
        local flag_summary = platform.on_host
            and "[--wibo-trace] [--no-syms]" or "[--no-syms]"
        platform.log("Usage: build.sh " .. flag_summary .. " [<target> ...]")
        platform.log("")
        platform.log("No arguments → builds 'all' (every group + cr + boot-efi; no disk image).")
        platform.log("")
        platform.log("Flags:")
        if platform.on_host then
            platform.log("  --wibo-trace  set WIBO_DEBUG=1 (one trace line per intercepted")
            platform.log("                syscall in the host wibo wrapper)")
        end
        platform.log("  --no-syms     skip /Z7 + sidecar .DBG/.dwf generation")
        platform.log("                (default: build with debug symbols — final binaries")
        platform.log("                 differ only by a 28-byte IMAGE_DEBUG_DIRECTORY entry)")
        platform.log("")
        platform.log("Top-level targets:")
        platform.log("  all, tools, ntoskrnl, drivers, userland, cr, efi, disk")
        platform.log("  rebuild            — alias for `clean all` (full nuke + full build)")
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
        platform.log("  clean:disk         — drops build/disk* (all profile dirs)")
    end

    -- argv parsing already handled at top of main() so the flags
    -- could feed toolchain.configure.

    -- After targets.clean wipes wibo-tools/, the toolchain symlink farm
    -- has to be repopulated AND cmd-stub rebuilt before any subsequent
    -- target can spawn an NT tool.  Both calls are idempotent (no-op
    -- when wibo-tools/ exists and cmd.exe is fresh), so it's safe to
    -- call them anywhere a clean might just have run.
    local function reprovision_after_clean()
        if not platform.on_host then return 0 end
        toolchain.setup_wibo_tools()
        return bootstrap_cmdstub_if_needed()
    end

    -- `rebuild` — clean nuke followed by full build.  Implemented inline
    -- (rather than as a list "clean", "all") because targets.clean wipes
    -- wibo-tools/ + cmd-stub, so we have to re-provision between phases.
    targets.rebuild = function()
        local rc = targets.clean()
        if rc ~= 0 then return rc end
        rc = reprovision_after_clean()
        if rc ~= 0 then return rc end
        return targets.all()
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
            -- targets.clean wipes wibo-tools/ + cmd-stub; if more targets
            -- are queued, re-provision before they run.
            if name == "clean" then
                rc = reprovision_after_clean()
                if rc ~= 0 then return rc end
            end
        end
    end
    return 0
end

return M
