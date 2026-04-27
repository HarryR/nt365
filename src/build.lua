-- build.lua — LuaJIT port of build.sh, phase 1 of the self-hosting plan.
--
-- Faithful translation: same env vars, same wibo invocation shape, same
-- stale-.obj detection, same dispatch surface (./build.sh ke → luajit
-- build.lua ke).  Lives side-by-side with build.sh while it shakes out;
-- once parity is proven we can refactor patterns out.
--
-- Run via the host LuaJIT built by bootstrap.sh:
--    src/bootstrap.sh && build/host-tools/luajit src/build.lua <target>

local ffi = require('ffi')
local bit = require('bit')

-- ------------------------------------------------------------------
-- POSIX bindings.  posix_spawn gives us the same control build.sh
-- gets through `env -i ... wibo --chdir ...`: full env replacement,
-- explicit cwd, no shell interpolation.
-- ------------------------------------------------------------------

ffi.cdef[[
typedef int pid_t;

int posix_spawn(pid_t *pid, const char *path,
                const void *file_actions, const void *attrp,
                char *const argv[], char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file,
                 const void *file_actions, const void *attrp,
                 char *const argv[], char *const envp[]);
pid_t waitpid(pid_t pid, int *wstatus, int options);

int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int mkdir(const char *path, unsigned int mode);
int access(const char *path, int mode);
int symlink(const char *target, const char *linkpath);
int unlink(const char *path);

extern char **environ;
]]

local C = ffi.C

local function strvec(t)
    -- Convert {s1, s2, ...} to a NULL-terminated char*[] suitable for
    -- argv/envp.  The strings live in t for the duration; pin them by
    -- returning t alongside.
    local n = #t
    local arr = ffi.new('const char *[?]', n + 1)
    for i = 1, n do arr[i - 1] = t[i] end
    arr[n] = nil
    return arr
end

local function getcwd()
    local buf = ffi.new('char[?]', 4096)
    if C.getcwd(buf, 4096) == nil then error("getcwd failed") end
    return ffi.string(buf)
end

local function exit_status_of(wstatus)
    -- WIFEXITED + WEXITSTATUS; matches what bash $? returns for a
    -- normally-exited child.  Signal-killed children get 128+sig.
    if bit.band(wstatus, 0x7f) == 0 then
        return bit.band(bit.rshift(wstatus, 8), 0xff)
    end
    return 128 + bit.band(wstatus, 0x7f)
end

local function spawn_wait(path, argv, envp, cwd)
    -- Spawn a child with explicit env + cwd.  No shell, no quoting.
    -- cwd applied via chdir/restore around the spawn — posix_spawn's
    -- addchdir_np extension is glibc-only; this is portable.
    local saved_cwd
    if cwd then
        saved_cwd = getcwd()
        if C.chdir(cwd) ~= 0 then
            error("chdir failed: " .. cwd)
        end
    end

    local pid_box = ffi.new('pid_t[1]')
    local rc = C.posix_spawn(pid_box, path, nil, nil,
                             ffi.cast('char *const*', argv),
                             ffi.cast('char *const*', envp))

    if cwd then C.chdir(saved_cwd) end

    if rc ~= 0 then
        error(("posix_spawn(%s) failed: %d"):format(path, rc))
    end

    local status_box = ffi.new('int[1]')
    if C.waitpid(pid_box[0], status_box, 0) < 0 then
        error("waitpid failed")
    end
    return exit_status_of(status_box[0])
end

-- ------------------------------------------------------------------
-- Process env passthrough — for spawning non-wibo native helpers
-- (make, python3) where we want to inherit the parent's PATH/HOME.
-- ------------------------------------------------------------------

local function current_env()
    local out = {}
    local i = 0
    while C.environ[i] ~= nil do
        out[#out + 1] = ffi.string(C.environ[i])
        i = i + 1
    end
    return out
end

-- ------------------------------------------------------------------
-- Filesystem utilities.
-- ------------------------------------------------------------------

local function file_exists(path)
    return C.access(path, 0) == 0   -- F_OK
end

local function is_executable(path)
    return C.access(path, 1) == 0   -- X_OK
end

local function mkdir_p(path)
    -- mkdir -p in pure Lua via repeated mkdir(0755).  Ignores EEXIST.
    -- Path components separated by '/' (Linux only — host build).
    local accum = ""
    if path:sub(1, 1) == "/" then accum = "/"; path = path:sub(2) end
    for component in path:gmatch("[^/]+") do
        accum = accum .. component
        C.mkdir(accum, 0x1ed)        -- 0o755
        accum = accum .. "/"
    end
end

local function readdir(path)
    -- Cheap directory listing via ls (no native dirent in plain Lua,
    -- and pulling in lfs is overkill).  Returns array of names.
    local names = {}
    local p = io.popen(string.format("ls -A %q 2>/dev/null", path))
    if not p then return names end
    for name in p:lines() do names[#names + 1] = name end
    p:close()
    return names
end

local function find_iname(dir, glob)
    -- Case-insensitive single-level match — equivalent to
    -- `find $dir -maxdepth 1 -iname $glob`.  Returns first match or nil.
    local pat = "^" .. glob:gsub("[%-%.]", "%%%1"):gsub("%*", ".*") .. "$"
    local lower_pat = pat:lower()
    for _, name in ipairs(readdir(dir)) do
        if name:lower():match(lower_pat) then
            return dir .. "/" .. name
        end
    end
    return nil
end

local function mtime(path)
    -- Returns numeric mtime, or nil if missing.  Uses `stat` since
    -- LuaJIT doesn't ship a portable stat binding.
    local p = io.popen(string.format("stat -c %%Y %q 2>/dev/null", path))
    if not p then return nil end
    local s = p:read("*l")
    p:close()
    return s and tonumber(s) or nil
end

local function basename(path)
    return path:match("([^/]+)$") or path
end

local function stem(path)
    local b = basename(path)
    return b:gsub("%.[^.]*$", "")
end

local function dirname(path)
    local d = path:match("(.*)/[^/]*$")
    return d or "."
end

local function normpath(p)
    -- Collapse 'X/..' and '.' segments.  Doesn't follow symlinks — the
    -- only paths gen_objects sees are NT-source-tree paths with no
    -- symlinks in scope.
    local parts = {}
    for part in p:gmatch("[^/]+") do
        if part == ".." and #parts > 0 and parts[#parts] ~= ".." then
            parts[#parts] = nil
        elseif part ~= "." then
            parts[#parts + 1] = part
        end
    end
    local r = table.concat(parts, "/")
    if p:sub(1, 1) == "/" then r = "/" .. r end
    return r
end

local function trim(s)
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local data = f:read("*all")
    f:close()
    return data
end

local function split_lines(s)
    -- Split on \n / \r\n / \r.  Returns array of lines without trailing
    -- newline.  Does not produce a final empty element if input ends
    -- with a newline.
    local lines = {}
    for line in (s .. "\n"):gmatch("([^\r\n]*)\r?\n") do
        lines[#lines + 1] = line
    end
    -- Drop the trailing empty produced when input ended in \n.
    if lines[#lines] == "" then lines[#lines] = nil end
    return lines
end

local function resolve_ci(path)
    -- Case-insensitive single-component resolution: if `path` exists
    -- exactly, return it; otherwise scan its parent directory for a
    -- name matching the last component case-insensitively.
    if file_exists(path) then return path end
    local parent = dirname(path)
    if not file_exists(parent) then return nil end
    local target = basename(path):lower()
    for _, name in ipairs(readdir(parent)) do
        if name:lower() == target then
            return parent .. "/" .. name
        end
    end
    return nil
end

-- ------------------------------------------------------------------
-- Logging.
-- ------------------------------------------------------------------

local function log(s)
    io.write(s, "\n")
    io.flush()
end

local function banner(title)
    log("========================================")
    log("Building: " .. title)
    log("========================================")
end

-- ------------------------------------------------------------------
-- Project layout — derived from this script's location, identical to
-- build.sh's SCRIPT_DIR/NT_ROOT/NTOS chain.
-- ------------------------------------------------------------------

local SCRIPT_DIR = (function()
    -- arg[0] is the script path luajit was invoked with.  Resolve to
    -- absolute via dirname + getcwd if relative.
    local self = arg[0]
    local d = dirname(self)
    if d:sub(1, 1) ~= "/" then
        d = getcwd() .. "/" .. d
    end
    -- realpath via the shell since we don't have a binding.
    local p = io.popen(string.format("readlink -f %q", d))
    local resolved = p:read("*l")
    p:close()
    return resolved or d
end)()

local NT_ROOT      = SCRIPT_DIR .. "/NT"
local NTOS         = NT_ROOT .. "/PRIVATE/NTOS"
local REPO_ROOT    = dirname(SCRIPT_DIR)
local WIBO_BIN     = REPO_ROOT .. "/wibo-x86_64"
local WIBO_TOOLS   = SCRIPT_DIR .. "/wibo-tools"

if not is_executable(WIBO_BIN) then
    io.stderr:write(("ERROR: wibo binary not found or not executable: %s\n"):format(WIBO_BIN))
    io.stderr:write("Download from https://github.com/HarryR/wibo/releases (the MicroNT-patched fork)\n")
    io.stderr:write(("and place as %s, then chmod +x.\n"):format(WIBO_BIN))
    os.exit(1)
end

-- ------------------------------------------------------------------
-- Path translation.  Wibo strips Z:/C: drive prefixes only; everything
-- else routes through Z:\<host-abs-path>\... — same as build.sh.
-- ------------------------------------------------------------------

local function path_to_win(p)
    return "Z:" .. p:gsub("/", "\\")
end

local NT_ROOT_WIN    = path_to_win(NT_ROOT)
local WIBO_TOOLS_WIN = path_to_win(WIBO_TOOLS)
local _NTROOT_WIN    = NT_ROOT:gsub("/", "\\")

-- ------------------------------------------------------------------
-- wibo-tools symlink farm — first-run population mirrors build.sh:36.
-- Every tool in PUBLIC/OAK/BIN/I386 plus CRTDLL.DLL.
-- ------------------------------------------------------------------

local function setup_wibo_tools()
    if file_exists(WIBO_TOOLS) then return end
    log(">>> setting up " .. WIBO_TOOLS .. " (first-time)")
    mkdir_p(WIBO_TOOLS)
    local oak_bin = NT_ROOT .. "/PUBLIC/OAK/BIN/I386"
    for _, name in ipairs(readdir(oak_bin)) do
        C.symlink(oak_bin .. "/" .. name, WIBO_TOOLS .. "/" .. name)
    end
    -- CRTDLL.DLL lives in SDK/LIB, not OAK/BIN; NMAKE etc. import from
    -- it so wibo needs to find it alongside the host binaries.
    C.symlink(NT_ROOT .. "/PUBLIC/SDK/LIB/I386/CRTDLL.DLL",
              WIBO_TOOLS .. "/CRTDLL.DLL")
end
setup_wibo_tools()

-- ------------------------------------------------------------------
-- NT toolchain environment — mirrors the NT_ENV_ARR bash array.
-- Built once, fed to every wibo invocation via posix_spawn envp.
-- ------------------------------------------------------------------

local NT_ENV = {
    "_NTDRIVE=Z:",
    "_NTROOT="     .. _NTROOT_WIN,
    "BASEDIR="     .. NT_ROOT_WIN,
    "NTMAKEENV="   .. NT_ROOT_WIN .. "\\PUBLIC\\OAK\\BIN",
    "386=1",
    "TARGETCPU=I386",
    "NT_UP=1",
    "NTDEBUG=",
    "NTDEBUGTYPE=",
    "PATH="        .. WIBO_TOOLS_WIN,
    "WIBO_PATH="   .. WIBO_TOOLS,
    "COMSPEC="     .. WIBO_TOOLS_WIN .. "\\cmd.exe",
    "TEMP=Z:\\tmp",
    "TMP=Z:\\tmp",
    "INCLUDE="     .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC;"
                  .. NT_ROOT_WIN .. "\\PUBLIC\\OAK\\INC;"
                  .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC\\CRT",
    "LIB="         .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386",
}

local function build_envp(extra)
    -- Stripped-down env: HOME, TERM, optional WIBO_DEBUG, plus NT_ENV
    -- and any per-call overrides.  Equivalent to bash's
    --   env -i HOME=... TERM=... ${WIBO_DEBUG:+...} "${NT_ENV_ARR[@]}" ...
    local env = {
        "HOME=" .. (os.getenv("HOME") or ""),
        "TERM=" .. (os.getenv("TERM") or "dumb"),
    }
    local wibo_dbg = os.getenv("WIBO_DEBUG")
    if wibo_dbg then env[#env + 1] = "WIBO_DEBUG=" .. wibo_dbg end
    for _, e in ipairs(NT_ENV) do env[#env + 1] = e end
    if extra then
        for _, e in ipairs(extra) do env[#env + 1] = e end
    end
    return env
end

-- ------------------------------------------------------------------
-- Tool resolution — case-insensitive glob inside wibo-tools, with
-- automatic .exe suffix.
-- ------------------------------------------------------------------

local function wibo_tool_path(name)
    local match = find_iname(WIBO_TOOLS, name)
    if not match and not name:find("%.") then
        match = find_iname(WIBO_TOOLS, name .. ".exe")
    end
    return match
end

-- ------------------------------------------------------------------
-- gen_objects — port of tools/gen_objects.py.  Parses a SOURCES file
-- (handling !include directives and line continuations) and emits the
-- obj/_objects.mac fragment that NMAKE pulls in via $(386_OBJECTS).
--
-- BUILD.EXE normally produces this; we do it ourselves so the build
-- has zero Python dependency (phase-2 self-hosting requirement).
-- ------------------------------------------------------------------

local function flatten_sources(path, seen)
    -- Read `path`, inlining any `!include ..\\foo.inc` directives.
    -- Returns the logically-flattened list of lines.
    seen = seen or {}
    path = normpath(path)
    if seen[path] then return {} end
    seen[path] = true

    local raw = read_file(path)
    if not raw then return {} end

    local out = {}
    for _, line in ipairs(split_lines(raw)) do
        local inc = line:match("^!%s*[Ii][Nn][Cc][Ll][Uu][Dd][Ee]%s+(.+)")
        if inc then
            inc = trim(inc):gsub('^"', ""):gsub('"$', ""):gsub("\\", "/")
            local inc_path = dirname(path) .. "/" .. inc
            local resolved = resolve_ci(inc_path)
            if resolved then
                for _, l in ipairs(flatten_sources(resolved, seen)) do
                    out[#out + 1] = l
                end
            end
            -- missing include: silently skip (matches nmake's
            -- `!include if exist` semantics).
        else
            out[#out + 1] = line
        end
    end
    return out
end

local function extract_var(lines, varname)
    -- Pull every `VAR= ...` definition out of `lines` (honoring
    -- backslash continuations) and return the concatenated tokens.
    -- Strips `$(VAR)` self-references.  Case-insensitive on varname
    -- (real SOURCES files mix `i386_SOURCES=` and `I386_SOURCES=`).
    local tokens = {}
    local in_var = false
    local var_lower = varname:lower()
    -- Build a case-insensitive prefix matcher manually since Lua
    -- patterns lack the case flag.
    local function match_var_assign(line)
        local key, body = line:match("^([%w_]+)%s*=%s*(.*)$")
        if not key or key:lower() ~= var_lower then return nil end
        return body
    end
    local self_ref_pat = "^%$%(" .. varname:gsub("(.)", function(c)
        if c:match("[%a_]") then
            return "[" .. c:lower() .. c:upper() .. "]"
        else
            return c:gsub("([%-%.%(%)%[%]%+%*%?%^%$%%])", "%%%1")
        end
    end) .. "%)%s*"

    for _, line in ipairs(lines) do
        local body
        if not in_var then
            body = match_var_assign(line)
            if not body then goto continue end
            body = body:gsub(self_ref_pat, "")
            in_var = true
        else
            body = line
        end

        local rstripped = body:gsub("%s+$", "")
        local cont = rstripped:sub(-1) == "\\"
        if cont then rstripped = rstripped:sub(1, -2) end
        for tok in rstripped:gmatch("%S+") do tokens[#tokens + 1] = tok end
        if not cont then in_var = false end

        ::continue::
    end
    return tokens
end

local function src_to_obj(src)
    -- '..\\i386\\foo.c' → 'obj\\i386\\foo.obj'.
    -- '.rc' → '.res' (RC inference rule produces .res).
    -- '.res' / '.mc' → nil (pre-built / message-compiler input).
    local base = basename(src:gsub("\\", "/"))
    local stm, ext = base:match("^(.+)%.([^.]+)$")
    if not stm then return nil end
    ext = ext:lower()
    if ext == "res" or ext == "mc" then return nil end
    if ext == "rc" then return "obj\\i386\\" .. stm .. ".res" end
    return "obj\\i386\\" .. stm .. ".obj"
end

local function find_i386_sources_file(comp_dir)
    -- Look for arch-specific SOURCES at known spots, case-insensitively.
    local candidates = {
        comp_dir .. "/I386/SOURCES",
        dirname(comp_dir) .. "/I386/SOURCES",
        comp_dir .. "/i386/SOURCES",
        dirname(comp_dir) .. "/i386/SOURCES",
    }
    for _, c in ipairs(candidates) do
        local r = resolve_ci(c)
        if r then return r end
    end
    return nil
end

local function gen_objects(comp_dir)
    local sources = resolve_ci(comp_dir .. "/SOURCES")
    if not sources then
        io.stderr:write("ERROR: SOURCES not found: " .. comp_dir .. "/SOURCES\n")
        return false
    end

    local lines = flatten_sources(sources)
    local srcs  = extract_var(lines, "SOURCES")

    -- i386_SOURCES: either in a sibling/arch SOURCES file, or inline.
    local i386_file = find_i386_sources_file(comp_dir)
    local i386_srcs
    if i386_file then
        local i386_lines = flatten_sources(i386_file)
        i386_srcs = extract_var(i386_lines, "SOURCES")
        for _, t in ipairs(extract_var(i386_lines, "i386_SOURCES")) do
            i386_srcs[#i386_srcs + 1] = t
        end
    else
        i386_srcs = extract_var(lines, "i386_SOURCES")
    end

    local all_srcs = {}
    for _, t in ipairs(srcs)      do all_srcs[#all_srcs + 1] = t end
    for _, t in ipairs(i386_srcs) do all_srcs[#all_srcs + 1] = t end

    mkdir_p(comp_dir .. "/obj/i386")

    local objs = {}
    for _, s in ipairs(all_srcs) do
        local o = src_to_obj(s)
        if o then objs[#objs + 1] = o end
    end

    local out_path = comp_dir .. "/obj/_objects.mac"
    local f = io.open(out_path, "w")
    if not f then
        io.stderr:write("ERROR: cannot write " .. out_path .. "\n")
        return false
    end
    f:write("#\n# _objects.mac - generated by build.lua gen_objects\n#\n\n")
    if #objs > 0 then
        f:write("386_OBJECTS=" .. objs[1])
        for i = 2, #objs do f:write(" \\\n    " .. objs[i]) end
        f:write("\n")
    else
        f:write("386_OBJECTS=\n")
    end
    f:write("\n")
    f:close()

    log(("Generated %s with %d source files (%d objects)"):format(
        out_path, #all_srcs, #objs))
    return true
end

-- ------------------------------------------------------------------
-- Stale-.obj detection — ports the bash two-pass scheme verbatim.
-- Pass 1: per-source mtime > matching .obj → rm the .obj.
-- Pass 2: any .h/.inc newer than oldest .obj → nuke all .obj.
-- ------------------------------------------------------------------

local SRC_EXTS    = { "c", "cxx", "cpp", "asm" }
local HEADER_EXTS = { "h", "hxx", "hpp", "inc" }

local function ext_in(path, exts)
    local e = path:match("%.([^.]+)$")
    if not e then return false end
    e = e:lower()
    for _, x in ipairs(exts) do if e == x then return true end end
    return false
end

local function nuke_stale_objs(linux_dir)
    local obj_dir  = linux_dir .. "/obj/i386"
    local src_dirs = { linux_dir, linux_dir .. "/..", linux_dir .. "/i386" }

    -- Pass 1: stale .obj per source file.
    for _, d in ipairs(src_dirs) do
        if file_exists(d) then
            for _, name in ipairs(readdir(d)) do
                if ext_in(name, SRC_EXTS) then
                    local src      = d .. "/" .. name
                    local obj_stem = stem(name)
                    local obj      = find_iname(obj_dir, obj_stem .. ".obj")
                    if obj then
                        local sm, om = mtime(src), mtime(obj)
                        if sm and om and sm > om then
                            log(("  stale: %s (newer than %s)"):format(name, basename(obj)))
                            os.remove(obj)
                        end
                    end
                end
            end
        end
    end

    -- Pass 2: header edits invalidate every .obj.
    if file_exists(obj_dir) then
        local objs, oldest_obj, oldest_m = {}, nil, math.huge
        for _, name in ipairs(readdir(obj_dir)) do
            if name:lower():match("%.obj$") then
                local p = obj_dir .. "/" .. name
                local m = mtime(p)
                if m then
                    objs[#objs + 1] = p
                    if m < oldest_m then oldest_m, oldest_obj = m, p end
                end
            end
        end
        if oldest_obj then
            for _, d in ipairs(src_dirs) do
                if file_exists(d) then
                    for _, name in ipairs(readdir(d)) do
                        if ext_in(name, HEADER_EXTS) then
                            local src = d .. "/" .. name
                            local sm  = mtime(src)
                            if sm and sm > oldest_m then
                                log(("  header changed: %s (newer than %s) — nuking %s/*.obj"):format(
                                    name, basename(oldest_obj), obj_dir))
                                for _, o in ipairs(objs) do os.remove(o) end
                                return
                            end
                        end
                    end
                end
            end
        end
    end
end

-- ------------------------------------------------------------------
-- run_nmake — the workhorse.  Mirrors build.sh:96 line-for-line.
-- ------------------------------------------------------------------

local function run_nmake(linux_dir, desc, extra_args, opts)
    opts = opts or {}
    extra_args = extra_args or {}

    banner(desc)

    if not file_exists(linux_dir) then
        log("ERROR: directory not found: " .. linux_dir)
        return 1
    end

    mkdir_p(linux_dir .. "/obj/i386")
    -- Shared NTOS output dir (TARGETPATH=..\..\obj).
    mkdir_p(NTOS .. "/obj/i386")

    -- Always regenerate _objects.mac to stay in sync with SOURCES.
    if not gen_objects(linux_dir) then
        return 1
    end

    nuke_stale_objs(linux_dir)

    -- MAKEDIR = win-form of linux_dir.  We strip the NT_ROOT prefix
    -- and back-slashify the rest, prepending NT_ROOT_WIN.
    local rel_path     = linux_dir:sub(#NT_ROOT + 1)
    local makedir_win  = NT_ROOT_WIN .. rel_path:gsub("/", "\\")

    -- UMAPPL override.  KEEP_UMAPPL=1 in env preserves the SOURCES
    -- file's UMAPPL= directive (cowtest etc. need this).
    local umappl_override = "UMAPPL="
    if opts.keep_umappl or os.getenv("KEEP_UMAPPL") == "1" then
        umappl_override = nil
    end

    -- Build argv: wibo --chdir <linux_dir> NMAKE.EXE /NOLOGO NTTEST= UMTEST= [UMAPPL=] [extras]
    local argv = {
        "wibo",
        "--chdir", linux_dir,
        WIBO_TOOLS .. "/NMAKE.EXE",
        "/NOLOGO",
        "NTTEST=", "UMTEST=",
    }
    if umappl_override then argv[#argv + 1] = umappl_override end
    for _, a in ipairs(extra_args) do argv[#argv + 1] = a end

    local envp = build_envp({ "MAKEDIR=" .. makedir_win })
    local rc = spawn_wait(WIBO_BIN, strvec(argv), strvec(envp))

    if rc == 0 then
        log(">>> " .. desc .. ": OK")
    else
        log((">>> %s: FAILED (rc=%d)"):format(desc, rc))
    end
    return rc
end

-- ------------------------------------------------------------------
-- run_wibo_tool — single-tool invocation, no NMAKE wrapping.
-- ------------------------------------------------------------------

local function run_wibo_tool(cwd, tool_name, ...)
    local tool_path = wibo_tool_path(tool_name)
    if not tool_path then
        io.stderr:write("ERROR: tool not found in wibo-tools: " .. tool_name .. "\n")
        return 1
    end
    local argv = { "wibo", "--chdir", cwd, tool_path }
    for i = 1, select("#", ...) do argv[#argv + 1] = (select(i, ...)) end
    return spawn_wait(WIBO_BIN, strvec(argv), strvec(build_envp()))
end

-- ------------------------------------------------------------------
-- File-copy + mtime helpers used by both install_host_tool and the
-- _ensure_* routines below.
-- ------------------------------------------------------------------

local function copy_file(src, dst)
    local f_in  = io.open(src, "rb")
    if not f_in then return false, "open " .. src end
    local f_out = io.open(dst, "wb")
    if not f_out then f_in:close(); return false, "open " .. dst end
    f_out:write(f_in:read("*all"))
    f_in:close()
    f_out:close()
    return true
end

local function newer_than(a, b)
    local am, bm = mtime(a), mtime(b)
    return am and bm and am > bm
end

-- ------------------------------------------------------------------
-- wibo_spawn_args — like run_wibo_tool but takes an absolute tool path
-- (used when invoking just-built host tools that aren't in wibo-tools
-- yet, e.g. geni386.exe before it's installed).
-- ------------------------------------------------------------------

local function wibo_spawn_args(cwd, tool_path, args)
    local argv = { "wibo", "--chdir", cwd, tool_path }
    for _, a in ipairs(args) do argv[#argv + 1] = a end
    return spawn_wait(WIBO_BIN, strvec(argv), strvec(build_envp()))
end

-- ------------------------------------------------------------------
-- install_host_tool — copy a freshly-built wibo-runnable host tool to
-- PUBLIC/OAK/BIN/I386 and refresh its symlink in wibo-tools/.
-- ------------------------------------------------------------------

local function install_host_tool(built, name)
    if not file_exists(built) then
        io.stderr:write(("!!! %s: expected output %s not found\n"):format(name, built))
        return false
    end
    local dst = NT_ROOT .. "/PUBLIC/OAK/BIN/I386/" .. name
    if not copy_file(built, dst) then
        io.stderr:write("ERROR: copy failed: " .. built .. " -> " .. dst .. "\n")
        return false
    end
    -- Refresh wibo-tools symlink so subsequent invocations resolve
    -- to the new build.
    local link = WIBO_TOOLS .. "/" .. name
    C.unlink(link)
    if C.symlink(dst, link) ~= 0 then
        io.stderr:write("ERROR: symlink failed: " .. link .. "\n")
        return false
    end
    log(">>> installed " .. name)
    return true
end

-- ------------------------------------------------------------------
-- run_make / run_python — non-wibo native helpers.  Inherit the parent
-- env (PATH/HOME/etc.) since these tools are host-side, not NT-toolchain.
-- ------------------------------------------------------------------

local function run_make(cwd, target)
    local argv = { "make", "-C", cwd }
    if target then argv[#argv + 1] = target end
    return spawn_wait("/usr/bin/make", strvec(argv), strvec(current_env()))
end

local function run_python(script, ...)
    local argv = { "python3", script }
    for i = 1, select("#", ...) do argv[#argv + 1] = (select(i, ...)) end
    return spawn_wait("/usr/bin/python3", strvec(argv), strvec(current_env()))
end

-- ------------------------------------------------------------------
-- ensure_error_h — generate NTOS/RTL/error.h via tools/generr.lua
-- (the in-tree port of tools/generr.py).  Loaded via dofile so the
-- module file mirrors the Python sibling and stays self-contained.
-- ------------------------------------------------------------------

local generr_module
local function ensure_error_h()
    if not generr_module then
        generr_module = dofile(SCRIPT_DIR .. "/tools/generr.lua")
    end
    local ok, err = pcall(generr_module.run, NT_ROOT, NTOS .. "/RTL/error.h")
    if not ok then
        io.stderr:write("GENERR: " .. tostring(err) .. "\n")
        return false
    end
    return true
end

-- ------------------------------------------------------------------
-- _ensure_bugcodes — bugcodes.rc / bugcodes.h are generated by mc.exe
-- from NLS/BUGCODES.MC.  ntoskrnl.rc #includes bugcodes.rc; many
-- public headers reference bugcodes.h.
-- ------------------------------------------------------------------

local function ensure_bugcodes()
    local nls = NTOS .. "/NLS"
    if file_exists(nls .. "/bugcodes.rc")
       and file_exists(NTOS .. "/INC/bugcodes.h")
       and newer_than(nls .. "/bugcodes.rc", nls .. "/BUGCODES.MC") then
        return true
    end
    log(">>> mc bugcodes.mc -> bugcodes.h/.rc")
    if run_wibo_tool(nls, "mc", "BUGCODES.MC") ~= 0 then
        log("!!! mc on BUGCODES.MC failed")
        return false
    end
    -- mc.exe emits case-matching names on Windows; normalise for our
    -- case-sensitive Linux FS.
    copy_file(nls  .. "/BUGCODES.rc",  NTOS .. "/INIT/bugcodes.rc")
    copy_file(nls  .. "/BUGCODES.h",   NTOS .. "/INC/bugcodes.h")
    copy_file(nls  .. "/MSG00001.bin", NTOS .. "/INIT/msg00001.bin")
    return true
end

-- ------------------------------------------------------------------
-- _ensure_serlog — serial.sys's SERLOG.MC compiled to serlog.rc/.h
-- before the SERIAL build.
-- ------------------------------------------------------------------

local function ensure_serlog()
    local dir = NTOS .. "/DD/SERIAL"
    if file_exists(dir .. "/serlog.rc")
       and file_exists(dir .. "/serlog.h")
       and newer_than(dir .. "/serlog.rc", dir .. "/SERLOG.MC") then
        return true
    end
    log(">>> mc SERLOG.MC -> serlog.h/.rc")
    if run_wibo_tool(dir, "mc", "SERLOG.MC") ~= 0 then
        log("!!! mc on SERLOG.MC failed")
        return false
    end
    if file_exists(dir .. "/SERLOG.rc") then copy_file(dir .. "/SERLOG.rc", dir .. "/serlog.rc") end
    if file_exists(dir .. "/SERLOG.h")  then copy_file(dir .. "/SERLOG.h",  dir .. "/serlog.h")  end
    return true
end

-- ------------------------------------------------------------------
-- Targets — faithful translations of build.sh's per-component
-- functions.  Trivial 1-line wrappers stay 1-line here.
-- ------------------------------------------------------------------

local targets = {}

local function nmake_target(name, dir, desc, opts, ...)
    local extras = {...}
    targets[name] = function()
        if opts and opts.pre and not opts.pre() then return 1 end
        return run_nmake(dir, desc, extras, opts)
    end
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
    local rc = spawn_wait(WIBO_BIN, strvec(argv), strvec(envp))

    if rc == 0 then
        log(">>> " .. desc .. ": OK")
    else
        log((">>> %s: FAILED (rc=%d)"):format(desc, rc))
    end
    return rc
end

-- ----- GENI386 (struct offset generator → KS386.INC + HAL386.INC) -----
targets.geni386 = function()
    banner("GENI386 (struct offset generator)")

    local geni_src     = NT_ROOT .. "/PRIVATE/NTOS/KE/I386/GENI386.C"
    local geni_dir     = NTOS .. "/INIT/UP/obj/i386"
    mkdir_p(geni_dir)
    local geni_dir_win = path_to_win(geni_dir)

    if not file_exists(geni_src) then
        log("ERROR: GENI386.C not found")
        return 1
    end

    local cl_args = {
        "-nologo", "-c", "-Zp8", "-Gz", "-Di386=1", "-D_X86_=1", "-DNT_UP=1",
        "-DSTD_CALL", "-DCONDITION_HANDLING=1", "-DWIN32_LEAN_AND_MEAN=1",
        "-D_NTSYSTEM_", "-DDBG=0", "-DDEVL=1",
        "-I" .. NT_ROOT_WIN .. "\\PRIVATE\\NTOS\\INC",
        "-I" .. NT_ROOT_WIN .. "\\PRIVATE\\NTOS\\KE",
        "-I" .. NT_ROOT_WIN .. "\\PRIVATE\\INC",
        "-I" .. NT_ROOT_WIN .. "\\PUBLIC\\OAK\\INC",
        "-I" .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC",
        "-I" .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC\\CRT",
        NT_ROOT_WIN .. "\\PRIVATE\\NTOS\\KE\\I386\\GENI386.C",
        "-Fo" .. geni_dir_win .. "\\geni386.obj",
    }
    if run_wibo_tool(SCRIPT_DIR, "cl386", unpack(cl_args)) ~= 0 then return 1 end

    local link_args = {
        "-nologo", "-subsystem:console",
        "-out:" .. geni_dir_win .. "\\geni386.exe",
        geni_dir_win .. "\\geni386.obj",
        NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\LIBC.LIB",
        NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\KERNEL32.LIB",
    }
    if run_wibo_tool(SCRIPT_DIR, "link", unpack(link_args)) ~= 0 then return 1 end

    -- The just-built geni386.exe lives outside wibo-tools; invoke by
    -- absolute host path through wibo.
    if wibo_spawn_args(SCRIPT_DIR, geni_dir .. "/geni386.exe", {
        NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC\\KS386.INC",
        NT_ROOT_WIN .. "\\PRIVATE\\NTOS\\INC\\HAL386.INC",
    }) ~= 0 then return 1 end

    log(">>> GENI386: KS386.INC and HAL386.INC regenerated")
    return 0
end

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

    os.remove(src_dir .. "/cmd.obj")
    os.remove(src_dir .. "/cmd.exe")

    -- Stripped env — no NT_ENV here because COMSPEC isn't wired yet.
    local env = {
        "HOME=" .. (os.getenv("HOME") or ""),
        "TERM=" .. (os.getenv("TERM") or "dumb"),
        "INCLUDE=" .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC;"
                   .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\INC\\CRT",
        "LIB=" .. NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386",
        "PATH=" .. WIBO_TOOLS_WIN,
        "WIBO_PATH=" .. WIBO_TOOLS,
    }
    local wibo_dbg = os.getenv("WIBO_DEBUG")
    if wibo_dbg then env[#env + 1] = "WIBO_DEBUG=" .. wibo_dbg end

    local argv = {
        "wibo", "--chdir", src_dir,
        WIBO_TOOLS .. "/CL.EXE", "-nologo", "cmd.c",
        "-link", "-subsystem:console", "-out:cmd.exe",
        NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\libc.lib",
        NT_ROOT_WIN .. "\\PUBLIC\\SDK\\LIB\\I386\\kernel32.lib",
    }
    local rc = spawn_wait(WIBO_BIN, strvec(argv), strvec(env))
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

    local cflags = {
        "-nologo", "-c",
        "-I", ".",
        "-D_X86_=1", "-Di386=1", "-DWIN32_LEAN_AND_MEAN=1", "-DWIN32=100",
        "-DCOMMAND=1", "-DENABLE_NLS=0",
        "-DUNICODE", "-D_UNICODE",
        "-DSTD_CALL", "-DCONDITION_HANDLING=1",
        "-DDBG=0", "-DDEVL=1",
    }

    for _, src in ipairs({ "mc", "mclex", "mcparse", "mcout", "mcutil" }) do
        log(">>> CL " .. src .. ".c")
        local args = {}
        for _, f in ipairs(cflags) do args[#args + 1] = f end
        args[#args + 1] = "-Fo" .. "obj/i386/" .. src .. ".obj"
        args[#args + 1] = src .. ".c"
        if run_wibo_tool(mc_dir, "CL", unpack(args)) ~= 0 then return 1 end
    end

    log(">>> LINK mc.exe")
    if run_wibo_tool(mc_dir, "LINK",
        "-nologo", "-subsystem:console", "-machine:i386",
        "-out:obj/i386/mc.exe",
        "obj/i386/mc.obj", "obj/i386/mclex.obj", "obj/i386/mcparse.obj",
        "obj/i386/mcout.obj", "obj/i386/mcutil.obj",
        "user32.lib", "libc.lib", "kernel32.lib", "advapi32.lib") ~= 0 then
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

-- TODO: port tools/mkhive.py + tools/mkdisk.py to drop these last
-- Python deps from the host build.  generr is already in tools/generr.lua;
-- gen_objects is inline above.  After mkhive/mkdisk land the host build
-- has zero Python dependency.
targets.disk = function()
    local out_dir = REPO_ROOT .. "/build/disk"
    local efi_bin = SCRIPT_DIR .. "/boot-efi/BOOTX64.EFI"
    banner("boot disk image")

    if not file_exists(efi_bin) then
        if targets.efi() ~= 0 then return 1 end
    end

    mkdir_p(out_dir)
    if run_python(SCRIPT_DIR .. "/tools/mkhive.py",
                  out_dir .. "/SYSTEM") ~= 0 then return 1 end
    if run_python(SCRIPT_DIR .. "/tools/mkdisk.py",
                  "--output-dir", out_dir,
                  "--efi-binary", efi_bin) ~= 0 then return 1 end
    return 0
end

-- ------------------------------------------------------------------
-- Group targets — order inside each list matters (deps first), exactly
-- mirroring the bash arrays in build.sh.
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
    "rtl_user", "ntdll", "urtl",
}

local function build_group(name, list)
    log("")
    log("########################################")
    log("# Group: " .. name)
    log("########################################")
    for _, t in ipairs(list) do
        local rc = (targets[t] or function()
            io.stderr:write("Unknown target in group '" .. name .. "': " .. t .. "\n")
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

-- No targets are deferred any more; the table-based "not yet ported"
-- block was retired once this file reached parity with build.sh.
local NOT_YET_PORTED = {}

-- ------------------------------------------------------------------
-- Self-bootstrap of cmd-stub before any wibo invocation that touches
-- COMSPEC.  Same idempotent guard as build.sh.
-- ------------------------------------------------------------------

local function bootstrap_cmdstub_if_needed()
    local cmd_exe = WIBO_TOOLS .. "/cmd.exe"
    local cmd_src = SCRIPT_DIR .. "/cmd-stub/cmd.c"
    if not file_exists(cmd_exe)
       or (file_exists(cmd_src) and newer_than(cmd_src, cmd_exe)) then
        local rc = targets.cmdstub()
        if rc ~= 0 then os.exit(rc) end
    end
end

-- ------------------------------------------------------------------
-- Dispatch.
-- ------------------------------------------------------------------

local function usage()
    io.stderr:write("Usage: build.lua [--debug] [<target> ...]\n")
    io.stderr:write("\nNo arguments → builds 'all' (every group + cr + disk).\n")
    io.stderr:write("\nTop-level targets:\n")
    io.stderr:write("  all, tools, ntoskrnl, drivers, userland, cr, efi, disk\n")
    io.stderr:write("\nIndividual components (build order within each group):\n")
    io.stderr:write("  tools:    " .. table.concat(TOOL_TARGETS,    ", ") .. "\n")
    io.stderr:write("  ntoskrnl: " .. table.concat(NTOSKRNL_TARGETS, ", ") .. "\n")
    io.stderr:write("  drivers:  " .. table.concat(DRIVER_TARGETS,  ", ") .. "\n")
    io.stderr:write("  userland: " .. table.concat(USERLAND_TARGETS, ", ") .. "\n")
    os.exit(1)
end

-- Parse --debug etc. before target names.  WIBO_DEBUG is exported
-- exactly the way build.sh does it, so build_envp's later `os.getenv`
-- pulls it through to wibo.
local positional = {}
for _, a in ipairs(arg) do
    if a == "--debug" then
        -- LuaJIT 2.1 doesn't expose setenv on every libc; use os.execute-
        -- free FFI binding.  We declare it on demand here.
        ffi.cdef[[ int setenv(const char *, const char *, int); ]]
        ffi.C.setenv("WIBO_DEBUG", "1", 1)
    elseif a:sub(1, 2) == "--" then
        io.stderr:write("Unknown flag: " .. a .. "\n")
        os.exit(1)
    else
        positional[#positional + 1] = a
    end
end

bootstrap_cmdstub_if_needed()

if #positional == 0 then
    -- No args → build everything.  build.sh defaults to `all`.
    local rc = targets.all()
    os.exit(rc)
end

for _, name in ipairs(positional) do
    local fn = targets[name]
    if not fn then
        io.stderr:write("Unknown target: " .. name .. "\n")
        usage()
    end
    local rc = fn() or 0
    if rc ~= 0 then os.exit(rc) end
end
os.exit(0)
