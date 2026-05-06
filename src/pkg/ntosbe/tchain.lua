-- ntosbe.tchain — toolchain bridge from build.lua's host orchestration
-- to the NT toolchain (CL, LINK, NMAKE, RC, MC, MIDL, GENSRV, ...).
-- Module file is tchain.lua, not toolchain.lua, so the 8.3-char stem
-- constraint of the FAT16 disk image we stage pkg/ntosbe/ into is
-- satisfied.  Owns the single host-vs-NT divergence: on host, every
-- CL / LINK / NMAKE invocation is wrapped by `wibo --chdir <cwd>
-- <tool> <args>`; on NT the same tools run natively, no wibo prefix.
--
-- Configuration is one-shot per process via `configure{...}`:
--
--   nt_root      — absolute path to NT/ in the host filesystem (or NT
--                  filesystem, when self-hosting).  Used to derive Win-
--                  side paths (NT_ROOT_WIN) and to locate PUBLIC/OAK/BIN.
--   wibo_tools   — directory holding the toolchain binaries we exec.
--                  On host this is the symlink farm src/wibo-tools/;
--                  on NT it'd be the natural location of CL / LINK.
--   wibo_bin     — absolute path to the `wibo` binary.  REQUIRED on
--                  host; nil on NT (toolchain runs natively).
--   drive_root   — drive letter prefix prepended to Linux paths in env
--                  vars (BASEDIR, INCLUDE, LIB, ...).  Defaults to "Z:"
--                  on host (matches wibo's drive map); on NT typically
--                  "C:" or wherever the NT tree is mounted.
--   targetpath   — string prefix that appears in NMAKE TARGETPATH lines
--                  pointing into the NT root.  Defaults to "$(BASEDIR)";
--                  callers normally don't override.
--
-- After configure(), the surface is:
--
--   path_to_win(p)              "Z:" + Linux path with backslashes.
--   build_envp(extra)           env vector for the toolchain (NT_ENV
--                               + HOME / TERM / WIBO_DEBUG passthrough
--                               + per-call extras).
--   wibo_tool_path(name)        Locate name (case-insensitive, .exe-
--                               aware) inside wibo_tools.
--   run_nmake(...)              The workhorse: gen_objects + nuke
--                               stale .obj + spawn nmake.
--   run_wibo_tool(cwd, name,    Single tool, no NMAKE wrapping.  For
--                 ...)          NLS message compiler etc.
--   wibo_spawn_args(cwd, abs,   Like run_wibo_tool but with an
--                   args)       absolute-path tool (e.g. just-built
--                               geni386.exe before it's installed).
--   install_host_tool(built,    Copy a freshly-built host tool to
--                     name)     PUBLIC/OAK/BIN/I386, refresh the
--                               wibo-tools symlink (or copy on NT).
--   setup_wibo_tools()          First-time wibo-tools population.
--                               Idempotent.

local platform = require('ntosbe.platform')
local sources  = require('ntosbe.sources')

local M = {}

-- ----------------------------------------------------------------
-- Module state, set by configure().
-- ----------------------------------------------------------------

local cfg            -- table set by configure()
local NT_ENV         -- precomputed env array
local nt_root_win    -- "Z:\path\to\NT"
local nt_root_naked  -- "\path\to\NT"  (no drive prefix)
local wibo_tools_win -- "Z:\path\to\wibo-tools"

-- Host-only: symlink the wibo-tools farm.  On NT the toolchain
-- already lives at its natural location and no symlinks are needed.
-- We FFI-bind symlink/unlink lazily (only when on host) so the module
-- imports cleanly inside MicroNT.

local C, ffi
if platform.on_host then
    ffi = require('ffi')
    ffi.cdef[[
    int symlink(const char *target, const char *linkpath);
    int unlink(const char *path);
    ]]
    C = ffi.C
end

-- ----------------------------------------------------------------
-- Path translation.
-- ----------------------------------------------------------------

function M.path_to_win(p)
    -- Optional NT-namespace prefix strip.  On guest, paths internally
    -- start with /SystemRoot/ (the kernel's symlink) but the toolchain
    -- expects DOS form rooted at C:\ (which is the same volume as
    -- SystemRoot).  Strip the prefix if configured, then prepend
    -- drive_root.  On host, path_strip is nil and we just prepend.
    if cfg.path_strip and p:sub(1, #cfg.path_strip) == cfg.path_strip then
        p = p:sub(#cfg.path_strip + 1)
    end
    return cfg.drive_root .. p:gsub("/", "\\")
end

-- ----------------------------------------------------------------
-- NT_ENV — the env vector every toolchain spawn inherits.  Mirrors
-- the original NT 3.5 build environment script.
-- ----------------------------------------------------------------

local function build_nt_env()
    local env = {
        "_NTDRIVE="    .. cfg.drive_root,
        "_NTROOT="     .. nt_root_naked,
        "BASEDIR="     .. nt_root_win,
        "NTMAKEENV="   .. nt_root_win .. "\\PUBLIC\\OAK\\BIN",
        "386=1",
        "TARGETCPU=I386",
        "NT_UP=1",
        "NTDEBUG=",
        "NTDEBUGTYPE=",
        "PATH="        .. wibo_tools_win,
        "COMSPEC="     .. wibo_tools_win .. "\\cmd.exe",
        "TEMP="        .. cfg.drive_root .. "\\tmp",
        "TMP="         .. cfg.drive_root .. "\\tmp",
        "INCLUDE="     .. nt_root_win .. "\\PUBLIC\\SDK\\INC;"
                       .. nt_root_win .. "\\PUBLIC\\OAK\\INC;"
                       .. nt_root_win .. "\\PUBLIC\\SDK\\INC\\CRT",
        "LIB="         .. nt_root_win .. "\\PUBLIC\\SDK\\LIB\\I386",
    }
    -- WIBO_PATH is only meaningful on host (where wibo is the
    -- runtime translator).  Guest runs everything natively against
    -- our own kernel32, so propagating /SystemRoot/-form unix paths
    -- as env vars to a Win32 toolchain child is just confusing
    -- (mixed slash style + meaningless on guest).
    if cfg.wibo_bin then
        env[#env + 1] = "WIBO_PATH=" .. cfg.wibo_tools
    end
    return env
end

function M.build_envp(extra)
    -- Stripped-down env: HOME, TERM, optional WIBO_DEBUG, plus NT_ENV
    -- and any per-call overrides.  Equivalent to bash's
    --   env -i HOME=... TERM=... ${WIBO_DEBUG:+...} "${NT_ENV_ARR[@]}" ...
    local env = {
        "HOME=" .. (platform.getenv("HOME") or ""),
        "TERM=" .. (platform.getenv("TERM") or "dumb"),
    }
    local wibo_dbg = platform.getenv("WIBO_DEBUG")
    if wibo_dbg then env[#env + 1] = "WIBO_DEBUG=" .. wibo_dbg end
    for _, e in ipairs(NT_ENV) do env[#env + 1] = e end
    if extra then
        for _, e in ipairs(extra) do env[#env + 1] = e end
    end
    return env
end

-- ----------------------------------------------------------------
-- Tool lookup.
-- ----------------------------------------------------------------

function M.wibo_tool_path(name)
    local match = sources.find_iname(cfg.wibo_tools, name)
    if not match and not name:find("%.") then
        match = sources.find_iname(cfg.wibo_tools, name .. ".exe")
    end
    return match
end

-- ----------------------------------------------------------------
-- Tool spawn — the single host/NT divergence.  On host, prepend
-- `wibo --chdir <cwd>` to argv and exec WIBO_BIN.  On NT, run the
-- tool directly with cwd set via spawn_wait's cwd arg.
-- ----------------------------------------------------------------

local function spawn_tool(cwd, tool_path, tool_args, envp)
    if cfg.wibo_bin then
        -- Host: wibo wrapper.
        local argv = { "wibo", "--chdir", cwd, tool_path }
        for _, a in ipairs(tool_args) do argv[#argv + 1] = a end
        return platform.spawn_wait{
            argv = argv, env = envp, path = cfg.wibo_bin,
        }
    else
        -- NT: native, with explicit cwd + DllPath pointing at the
        -- toolchain bundle so msvc20-shipped runtime DLLs (CRTDLL,
        -- MSVCRT20, RCDLL, DBI) resolve from there.  Toolchain dir
        -- comes first so its DLLs win over any same-name DLL in
        -- System32.
        --
        -- The exe path AND cwd MUST be DOS form (e.g. "C:\pkg\msvc20\
        -- NMAKE.EXE", "C:\src\NT\..."), not NT-namespace ("\SystemRoot
        -- \..."), because Win32 toolchain children read both via
        -- GetModuleFileName / GetCurrentDirectory and combine them
        -- with relative paths through DOS-namespace rules.  A "\..."-
        -- prefixed path with no drive letter is interpreted as
        -- drive-relative and resolves wrong (nmake.err / MAKEFILE
        -- searches expand to nonexistent C:\SystemRoot\... paths).
        -- ps.spawn handles the kernel-side NT-namespace conversion
        -- internally (prepends \??\ to DOS paths for the syscall).
        -- DllPath stays NT-namespace because the kernel image loader
        -- walks it directly via NtCreateFile, not Win32 path rules.
        local tools_nt = cfg.wibo_tools:gsub("/", "\\")
        local exe_dos  = M.path_to_win(tool_path)
        local argv = { exe_dos }
        for _, a in ipairs(tool_args) do argv[#argv + 1] = a end
        return platform.spawn_wait{
            argv     = argv,
            env      = envp,
            cwd      = M.path_to_win(cwd),
            dll_path = { tools_nt, "\\SystemRoot\\System32" },
        }
    end
end

-- ----------------------------------------------------------------
-- run_nmake — gen_objects + stale-obj nuke + spawn nmake.
-- ----------------------------------------------------------------

-- Per-component build-mode stamp.  Lets us detect when the operator
-- ran without --syms last time and is now running with --syms (or
-- vice versa).  nmake's mtime check would otherwise skip recompilation
-- on unchanged .c files, producing a binary whose .obj files were
-- compiled with stale /Z7-vs-no-/Z7 flags.  When the stamp mismatches
-- we wipe obj/ before running nmake; on success the new mode is
-- written back.
local function _read_stamp(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*l")
    f:close()
    return s
end

local function _write_stamp(path, val)
    local f = io.open(path, "w")
    if not f then return end
    f:write(val)
    f:close()
end

function M.run_nmake(linux_dir, desc, extra_args, opts)
    opts = opts or {}
    extra_args = extra_args or {}

    platform.log("========================================")
    platform.log("Building: " .. desc)
    platform.log("========================================")

    if not platform.file_exists(linux_dir) then
        platform.log("ERROR: directory not found: " .. linux_dir)
        return 1
    end

    -- Detect --syms ↔ retail mode flip and wipe obj/ if needed before
    -- nmake gets a chance to silently reuse stale .obj files.
    do
        local obj_root  = linux_dir .. "/obj"
        local obj_arch  = obj_root .. "/i386"
        local stamp     = obj_arch .. "/.syms-stamp"
        local cur_mode  = cfg.debug_symbols and "syms" or "retail"
        local prev_mode = _read_stamp(stamp)
        if prev_mode and prev_mode ~= cur_mode then
            platform.log((">>> build-mode change (%s → %s); wiping %s")
                :format(prev_mode, cur_mode, obj_root))
            platform.rmrf(obj_root)
        end
    end

    platform.mkdir_p(linux_dir .. "/obj/i386")
    -- Shared NTOS output dir (TARGETPATH=..\..\obj).
    platform.mkdir_p(cfg.nt_root .. "/PRIVATE/NTOS/obj/i386")

    -- Always regenerate _objects.mac to stay in sync with SOURCES.
    if not sources.gen_objects(linux_dir) then return 1 end

    sources.nuke_stale_objs(linux_dir)

    -- MAKEDIR = win-form of linux_dir.  We strip the NT_ROOT prefix
    -- and back-slashify the rest, prepending nt_root_win.
    local rel_path     = linux_dir:sub(#cfg.nt_root + 1)
    local makedir_win  = nt_root_win .. rel_path:gsub("/", "\\")

    -- UMAPPL override.  KEEP_UMAPPL=1 in env preserves the SOURCES
    -- file's UMAPPL= directive (cowtest etc. need this).
    local umappl_override = "UMAPPL="
    if opts.keep_umappl or platform.getenv("KEEP_UMAPPL") == "1" then
        umappl_override = nil
    end

    local nmake = M.wibo_tool_path("NMAKE.EXE")
            or (cfg.wibo_tools .. "/NMAKE.EXE")
    local tool_args = { "/NOLOGO" }

    -- NTTEST= / UMTEST= overrides clear the developer's local-build
    -- NMAKE convention (passing NTTEST=foo to build just one .c).
    -- Almost every component wants these neutralised.  The kernel
    -- INIT target is the exception: its SOURCES file relies on
    -- NTTEST=ntoskrnl to select the EXE link rule via MAKEFILE.DEF,
    -- so opts.keep_nttest=true skips the override.
    if not opts.keep_nttest then
        tool_args[#tool_args + 1] = "NTTEST="
        tool_args[#tool_args + 1] = "UMTEST="
    end

    if umappl_override then tool_args[#tool_args + 1] = umappl_override end
    if cfg.debug_symbols then
        tool_args[#tool_args + 1] = "NTDEBUG=ntsdnodbg"
        tool_args[#tool_args + 1] = "NTDEBUGTYPE=windbg"
    end
    for _, a in ipairs(extra_args) do tool_args[#tool_args + 1] = a end

    local envp = M.build_envp({ "MAKEDIR=" .. makedir_win })
    local rc = spawn_tool(linux_dir, nmake, tool_args, envp)

    if rc == 0 then
        _write_stamp(linux_dir .. "/obj/i386/.syms-stamp",
                     cfg.debug_symbols and "syms" or "retail")
        platform.log(">>> " .. desc .. ": OK")
    else
        platform.log((">>> %s: FAILED (rc=%d)"):format(desc, rc))
    end
    return rc
end

-- ----------------------------------------------------------------
-- run_wibo_tool — single-tool, no NMAKE wrapping.  Tool is named
-- (case-insensitive lookup inside wibo_tools).
-- ----------------------------------------------------------------

function M.run_wibo_tool(cwd, tool_name, ...)
    local tool_path = M.wibo_tool_path(tool_name)
    if not tool_path then
        platform.log("ERROR: tool not found in wibo-tools: " .. tool_name)
        return 1
    end
    local args = {}
    for i = 1, select("#", ...) do args[i] = (select(i, ...)) end
    return spawn_tool(cwd, tool_path, args, M.build_envp())
end

-- ----------------------------------------------------------------
-- wibo_spawn_args — like run_wibo_tool but with an absolute-path tool
-- (used when invoking just-built host tools not yet in wibo-tools).
-- ----------------------------------------------------------------

function M.wibo_spawn_args(cwd, tool_path, args)
    return spawn_tool(cwd, tool_path, args, M.build_envp())
end

-- ----------------------------------------------------------------
-- install_host_tool — copy a freshly-built tool to PUBLIC/OAK/BIN/I386
-- and refresh its presence in wibo-tools.  On host that's a symlink;
-- on NT we just copy (no symlink semantics in NT 3.5 era).
-- ----------------------------------------------------------------

function M.install_host_tool(built, name)
    if not platform.file_exists(built) then
        platform.log(("!!! %s: expected output %s not found"):format(
            name, built))
        return false
    end
    local dst = cfg.nt_root .. "/PUBLIC/OAK/BIN/I386/" .. name
    local ok, err = platform.copy_file(built, dst)
    if not ok then
        platform.log("ERROR: copy failed: " .. (err or "?"))
        return false
    end

    -- Refresh wibo-tools entry.  Host: symlink; NT: copy.
    local link = cfg.wibo_tools .. "/" .. name
    if platform.on_host then
        C.unlink(link)
        if C.symlink(dst, link) ~= 0 then
            platform.log("ERROR: symlink failed: " .. link)
            return false
        end
    else
        platform.unlink(link)
        local ok2, err2 = platform.copy_file(dst, link)
        if not ok2 then
            platform.log("ERROR: copy to wibo-tools failed: " .. (err2 or "?"))
            return false
        end
    end
    platform.log(">>> installed " .. name)
    return true
end

-- ----------------------------------------------------------------
-- setup_wibo_tools — first-time population of the wibo-tools farm
-- on host (every tool in PUBLIC/OAK/BIN/I386 plus CRTDLL.DLL).
-- Idempotent; bails silently if wibo_tools already exists.  On NT
-- the toolchain already lives at its target path; no-op there.
-- ----------------------------------------------------------------

function M.setup_wibo_tools()
    if not platform.on_host then return end
    if platform.file_exists(cfg.wibo_tools) then return end

    platform.log(">>> setting up " .. cfg.wibo_tools .. " (first-time)")
    platform.mkdir_p(cfg.wibo_tools)

    local oak_bin = cfg.nt_root .. "/PUBLIC/OAK/BIN/I386"
    for _, name in ipairs(platform.list_dir(oak_bin)) do
        C.symlink(oak_bin .. "/" .. name, cfg.wibo_tools .. "/" .. name)
    end
    -- CRTDLL.DLL lives in SDK/LIB, not OAK/BIN; NMAKE etc. import from
    -- it so wibo needs to find it alongside the host binaries.
    C.symlink(cfg.nt_root .. "/PUBLIC/SDK/LIB/I386/CRTDLL.DLL",
              cfg.wibo_tools .. "/CRTDLL.DLL")
end

-- ----------------------------------------------------------------
-- configure — one-shot setup.  Caller passes nt_root + wibo_tools +
-- (host) wibo_bin + optional drive_root.  Builds derived paths +
-- NT_ENV, then runs setup_wibo_tools().
-- ----------------------------------------------------------------

function M.configure(opts)
    cfg = {
        nt_root    = assert(opts.nt_root,    "configure: nt_root required"),
        wibo_tools = assert(opts.wibo_tools, "configure: wibo_tools required"),
        wibo_bin   = opts.wibo_bin,
        drive_root = opts.drive_root or "Z:",
        -- Optional prefix to strip from /-form paths before prepending
        -- drive_root.  On guest: "/SystemRoot" (no trailing slash) —
        -- the leading '/' survives the strip and becomes '\\' after
        -- gsub, so "/SystemRoot/pkg/msvc20" → "/pkg/msvc20" →
        -- "\pkg\msvc20" → "C:\pkg\msvc20" (valid absolute DOS path).
        -- Stripping with a trailing slash would produce "C:pkg\..."
        -- (drive-relative — toolchain quietly resolves wrong).
        -- On host: nil (host filesystem is mounted at Z:\).
        path_strip = opts.path_strip,
        -- When true, every nmake invocation gets NTDEBUG=ntsdnodbg +
        -- NTDEBUGTYPE=windbg passed on the command line, which makes
        -- MAKEFILE.DEF emit /Z7 + -debug:full -debugtype:cv.  The
        -- caller (build.lua) pairs this with a post-link splitsym
        -- pass to extract CV into sidecar .DBG files.
        debug_symbols = opts.debug_symbols and true or false,
    }
    if platform.on_host and not cfg.wibo_bin then
        error("ntosbe.toolchain.configure: wibo_bin required on host", 2)
    end
    nt_root_win    = M.path_to_win(cfg.nt_root)
    nt_root_naked  = (cfg.path_strip and cfg.nt_root:sub(#cfg.path_strip + 1)
                                     or cfg.nt_root):gsub("/", "\\")
    wibo_tools_win = M.path_to_win(cfg.wibo_tools)
    NT_ENV = build_nt_env()
    M.setup_wibo_tools()
end

return M
