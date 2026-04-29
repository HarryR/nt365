-- ntosbe.platform — host-vs-OS abstraction.
--
-- The build environment runs in two places: on the host (LuaJIT built
-- by bootstrap.sh, full lib_io / lib_os, POSIX file I/O) and inside
-- MicroNT (LuaJIT-on-NT, lib_io / lib_os disabled, file I/O via
-- nt.dll.fs).  This module hides the difference so hive.lua, disk.lua,
-- the build orchestrator, and everything in pkg/ntosbe/ don't need to
-- branch on host-vs-OS at every callsite.
--
-- Detection: presence of io.open is the load-bearing signal.  The
-- MicroNT build of LuaJIT excludes lib_io entirely (see bootstrap.sh's
-- LJLIB_O note for why the host build flips it back on).  That gives
-- us a one-liner check that doesn't need any nt.* modules to be
-- importable yet.
--
-- Surface (current — grows as ports need more):
--
--   File I/O
--     read_file(path)           -> bytes | nil
--     write_file(path, bytes)
--     copy_file(src, dst)       -> bool, err
--     file_size(path)           -> bytes | nil
--     file_exists(path)         -> bool
--     is_dir(path)              -> bool
--     is_executable(path)       -> bool
--     mtime(path)               -> unix-timestamp | nil
--
--   Directory ops
--     list_dir(path)            -> array of names (excluding . / ..)
--     list_tree(root)           -> array of root-relative file paths
--     mkdir_p(path)
--     unlink(path)              -> bool
--     rmdir(path)               -> bool
--     rmrf(path)                -> bool
--     realpath(path)            -> resolved abs path | nil
--     getcwd()                  -> abs path
--
--   Time
--     now()                     -> unix-timestamp
--     localtime(t)              -> { year, month, day, hour, min, sec }
--
--   Process env
--     setenv(name, value)
--     getenv(name)              -> value | nil
--     environ()                 -> array of "KEY=VAL" strings
--
--   Process spawn
--     spawn_wait{               -> exit status (128+sig if signaled)
--       argv  = {"prog", ...},      argv[0] is the program (or its name
--                                   for PATH lookup if search_path=true)
--       path  = "/abs/prog",        optional; overrides argv[0] as the
--                                   binary to spawn (argv[0] still goes
--                                   to the child).  Wibo wants this so
--                                   the child sees argv[0]="wibo" but
--                                   we exec WIBO_BIN.
--       env   = {"K=V", ...},       optional; default = inherit current
--       cwd   = "/path",            optional; default = inherit
--       search_path = bool,         optional; posix_spawnp vs posix_spawn
--     }
--
--   Logging
--     log(msg)                  -> stderr (host) / DbgPrint (in-OS)
--     die(msg)                  -> log + exit(1)
--
--   Flags
--     on_host                   -> bool, for callers that need to branch

local ffi = require('ffi')
local bit = require('bit')

local M = {}

local on_host = (type(io) == 'table' and type(io.open) == 'function')
M.on_host = on_host

-- ----------------------------------------------------------------
-- Host backend — POSIX FFI, Linux x86_64.
--
-- Layouts (struct stat, struct dirent) are the modern glibc x86_64
-- versions.  bootstrap.sh builds LuaJIT for the host arch; if we ever
-- support 32-bit hosts the layout block below grows an arch branch.
-- ----------------------------------------------------------------

if on_host then

ffi.cdef[[
typedef struct DIR DIR;

typedef struct {
    int64_t  d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
} ntosbe_dirent_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} ntosbe_timespec_t;

typedef struct {
    uint64_t           st_dev;
    uint64_t           st_ino;
    uint64_t           st_nlink;
    uint32_t           st_mode;
    uint32_t           st_uid;
    uint32_t           st_gid;
    int32_t            __pad0;
    uint64_t           st_rdev;
    int64_t            st_size;
    int64_t            st_blksize;
    int64_t            st_blocks;
    ntosbe_timespec_t  st_atim;
    ntosbe_timespec_t  st_mtim;
    ntosbe_timespec_t  st_ctim;
    int64_t            __unused[3];
} ntosbe_stat_t;

DIR             *opendir(const char *name);
ntosbe_dirent_t *readdir(DIR *dirp);
int              closedir(DIR *dirp);

int   stat(const char *path, ntosbe_stat_t *statbuf);
int   lstat(const char *path, ntosbe_stat_t *statbuf);
int   access(const char *pathname, int mode);
int   unlink(const char *pathname);
int   rmdir(const char *pathname);
int   mkdir(const char *pathname, uint32_t mode);

char *getcwd(char *buf, size_t size);
char *realpath(const char *path, char *resolved_path);
int   chdir(const char *path);

int   setenv(const char *name, const char *value, int overwrite);
char *getenv(const char *name);

extern char **environ;

typedef int ntosbe_pid_t;
int posix_spawn(ntosbe_pid_t *pid, const char *path,
                const void *file_actions, const void *attrp,
                char *const argv[], char *const envp[]);
int posix_spawnp(ntosbe_pid_t *pid, const char *file,
                 const void *file_actions, const void *attrp,
                 char *const argv[], char *const envp[]);
ntosbe_pid_t waitpid(ntosbe_pid_t pid, int *wstatus, int options);

int errno;
]]

local C = ffi.C

-- POSIX file mode bits (sys/stat.h on Linux x86_64).
local S_IFMT  = 0xF000
local S_IFDIR = 0x4000
local S_IFREG = 0x8000

-- access(2) modes.
local F_OK = 0
local X_OK = 1

-- DT_* values from <dirent.h>.  DT_UNKNOWN means the FS didn't fill
-- d_type at readdir time — fall back to stat().
local DT_UNKNOWN = 0
local DT_DIR     = 4
local DT_REG     = 8

-- ---------------- Stat helpers ----------------

local stat_buf = ffi.new('ntosbe_stat_t')   -- reused; single-threaded

local function stat_or_nil(path)
    if C.stat(path, stat_buf) ~= 0 then return nil end
    return stat_buf
end

function M.file_exists(path)
    return C.stat(path, stat_buf) == 0
end

function M.is_dir(path)
    if C.stat(path, stat_buf) ~= 0 then return false end
    return bit.band(stat_buf.st_mode, S_IFMT) == S_IFDIR
end

function M.is_executable(path)
    return C.access(path, X_OK) == 0
end

function M.file_size(path)
    if C.stat(path, stat_buf) ~= 0 then return nil end
    return tonumber(stat_buf.st_size)
end

function M.mtime(path)
    if C.stat(path, stat_buf) ~= 0 then return nil end
    return tonumber(stat_buf.st_mtim.tv_sec)
end

-- ---------------- File I/O (host: stdio is fine) ----------------

function M.read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

function M.write_file(path, bytes)
    local f, err = io.open(path, "wb")
    if not f then
        error("ntosbe.platform.write_file " .. path .. ": " .. err, 2)
    end
    f:write(bytes)
    f:close()
end

function M.copy_file(src, dst)
    local fin, err = io.open(src, "rb")
    if not fin then return false, "open " .. src .. ": " .. (err or "") end
    local fout
    fout, err = io.open(dst, "wb")
    if not fout then
        fin:close()
        return false, "open " .. dst .. ": " .. (err or "")
    end
    -- Stream in 64 KB chunks so we don't materialise the whole file as a
    -- Lua string for large inputs (NT 3.5 binaries are small, but the
    -- hive + disk pipeline can carry larger blobs).
    while true do
        local chunk = fin:read(64 * 1024)
        if not chunk or #chunk == 0 then break end
        fout:write(chunk)
    end
    fin:close()
    fout:close()
    return true
end

-- ---------------- Directory ops ----------------

function M.list_dir(path)
    local d = C.opendir(path)
    if d == nil then return {} end
    local names = {}
    while true do
        local ent = C.readdir(d)
        if ent == nil then break end
        local name = ffi.string(ent.d_name)
        if name ~= "." and name ~= ".." then
            names[#names + 1] = name
        end
    end
    C.closedir(d)
    return names
end

-- Recursive walk; returns paths relative to root, files only (directories
-- implicit).  Sorted at each level for deterministic disk-image layouts.
-- Uses dirent.d_type when present; falls back to stat() on filesystems
-- that don't fill it (DT_UNKNOWN).
function M.list_tree(root)
    local out = {}
    local function walk(rel)
        local full = (rel == "") and root or (root .. "/" .. rel)
        local d = C.opendir(full)
        if d == nil then return end

        -- Snapshot dir contents first (name + dir-or-file), then sort
        -- and recurse.  Holding readdir state across recursion could
        -- otherwise interleave; cleaner to drain first.
        local kids = {}
        while true do
            local ent = C.readdir(d)
            if ent == nil then break end
            local name = ffi.string(ent.d_name)
            if name ~= "." and name ~= ".." then
                local is_dir
                if ent.d_type == DT_DIR then
                    is_dir = true
                elseif ent.d_type == DT_REG then
                    is_dir = false
                else
                    -- DT_UNKNOWN, DT_LNK etc. — resolve via stat.
                    local sub_full = full .. "/" .. name
                    is_dir = M.is_dir(sub_full)
                end
                kids[#kids + 1] = { name = name, is_dir = is_dir }
            end
        end
        C.closedir(d)

        table.sort(kids, function(a, b) return a.name < b.name end)
        for _, k in ipairs(kids) do
            local sub = (rel == "") and k.name or (rel .. "/" .. k.name)
            if k.is_dir then
                walk(sub)
            else
                out[#out + 1] = sub
            end
        end
    end
    walk("")
    return out
end

-- mkdir -p in pure FFI: walk components, mkdir(0755) each, ignore
-- errors (a later op will surface a real failure if the path is bogus).
function M.mkdir_p(path)
    local accum
    if path:sub(1, 1) == "/" then
        accum = "/"
        path  = path:sub(2)
    else
        accum = ""
    end
    for component in path:gmatch("[^/]+") do
        accum = accum .. component
        C.mkdir(accum, 0x1ed)        -- 0o755
        accum = accum .. "/"
    end
end

function M.unlink(path)
    return C.unlink(path) == 0
end

function M.rmdir(path)
    return C.rmdir(path) == 0
end

-- Recursive remove.  Walks once, deletes files, then deletes empty dirs
-- post-order.  No `rm -rf` shell-out, no `find`.
function M.rmrf(path)
    if not M.file_exists(path) then return true end
    if not M.is_dir(path) then
        return M.unlink(path)
    end
    -- Post-order: collect dir paths going in, delete files going down,
    -- then rmdir going back up.
    local dirs = {}
    local function walk(p)
        dirs[#dirs + 1] = p
        local d = C.opendir(p)
        if d == nil then return end
        local children = {}
        while true do
            local ent = C.readdir(d)
            if ent == nil then break end
            local name = ffi.string(ent.d_name)
            if name ~= "." and name ~= ".." then
                children[#children + 1] = { name = name, type = ent.d_type }
            end
        end
        C.closedir(d)
        for _, c in ipairs(children) do
            local cp = p .. "/" .. c.name
            local is_dir
            if c.type == DT_DIR then
                is_dir = true
            elseif c.type == DT_REG or c.type == 10 then  -- DT_LNK
                is_dir = false
            else
                is_dir = M.is_dir(cp)
            end
            if is_dir then
                walk(cp)
            else
                C.unlink(cp)
            end
        end
    end
    walk(path)
    -- rmdir leaves last (deepest first).
    for i = #dirs, 1, -1 do
        C.rmdir(dirs[i])
    end
    return not M.file_exists(path)
end

local CWD_BUF = ffi.new('char[?]', 4096)

function M.getcwd()
    if C.getcwd(CWD_BUF, 4096) == nil then
        error("ntosbe.platform.getcwd: failed (path > 4095?)", 2)
    end
    return ffi.string(CWD_BUF)
end

local REAL_BUF = ffi.new('char[?]', 4096)

function M.realpath(path)
    -- Linux man: with resolved_path != NULL, glibc requires it to be
    -- PATH_MAX (4096) bytes; we pass a 4096-byte buf so this is safe.
    if C.realpath(path, REAL_BUF) == nil then return nil end
    return ffi.string(REAL_BUF)
end

-- ---------------- Time ----------------

function M.now()
    return os.time()
end

function M.localtime(t)
    local d = os.date('*t', t)
    return {
        year = d.year, month = d.month, day = d.day,
        hour = d.hour, min = d.min, sec = d.sec,
    }
end

-- ---------------- Env ----------------

function M.setenv(name, value)
    C.setenv(name, value, 1)
end

function M.getenv(name)
    -- Lua's os.getenv works on host; no need to FFI for this.
    return os.getenv(name)
end

function M.environ()
    local out = {}
    local i = 0
    while C.environ[i] ~= nil do
        out[#out + 1] = ffi.string(C.environ[i])
        i = i + 1
    end
    return out
end

-- ---------------- Process spawn ----------------

local function strvec(t)
    -- Convert {s1, s2, ...} to a NULL-terminated char*[] suitable for
    -- argv / envp.  The Lua strings in `t` must stay alive through the
    -- spawn syscall (their immutable byte arrays back the cdata
    -- pointers); callers hold them in argv/env tables across the call.
    local n = #t
    local arr = ffi.new('const char *[?]', n + 1)
    for i = 1, n do arr[i - 1] = t[i] end
    arr[n] = nil
    return arr
end

local function exit_status_of(wstatus)
    -- WIFEXITED + WEXITSTATUS; matches what bash $? returns for a
    -- normally-exited child.  Signal-killed children get 128+sig.
    if bit.band(wstatus, 0x7f) == 0 then
        return bit.band(bit.rshift(wstatus, 8), 0xff)
    end
    return 128 + bit.band(wstatus, 0x7f)
end

-- Spawn + wait.  cwd is applied via chdir/restore around the spawn —
-- posix_spawn_file_actions_addchdir_np is glibc-only and we want this
-- portable to MicroNT's eventual native impl.  Single-threaded by
-- design; callers serialize.
function M.spawn_wait(opts)
    local argv = opts.argv
    if not argv or #argv == 0 then
        error("ntosbe.platform.spawn_wait: argv required", 2)
    end

    local env_table = opts.env or M.environ()

    local saved_cwd
    if opts.cwd then
        saved_cwd = M.getcwd()
        if C.chdir(opts.cwd) ~= 0 then
            error("spawn_wait: chdir(" .. opts.cwd .. ") failed", 2)
        end
    end

    -- Hold both vectors in named locals so the cdata isn't GC'd before
    -- posix_spawn returns; the underlying Lua strings live in argv /
    -- env_table on the caller's stack frame.
    local argv_vec = strvec(argv)
    local env_vec  = strvec(env_table)

    local prog = opts.path or argv[1]

    local pid_box = ffi.new('ntosbe_pid_t[1]')
    local rc
    if opts.search_path then
        rc = C.posix_spawnp(pid_box, prog, nil, nil,
                            ffi.cast('char *const*', argv_vec),
                            ffi.cast('char *const*', env_vec))
    else
        rc = C.posix_spawn(pid_box, prog, nil, nil,
                           ffi.cast('char *const*', argv_vec),
                           ffi.cast('char *const*', env_vec))
    end

    if opts.cwd then C.chdir(saved_cwd) end

    if rc ~= 0 then
        error(("spawn_wait: posix_spawn(%s) failed: %d"):format(prog, rc))
    end

    local status_box = ffi.new('int[1]')
    if C.waitpid(pid_box[0], status_box, 0) < 0 then
        error("spawn_wait: waitpid failed")
    end
    return exit_status_of(status_box[0])
end

-- ---------------- Logging ----------------

function M.log(msg)
    io.stderr:write(msg)
    io.stderr:write("\n")
end

function M.die(msg)
    M.log("ntosbe: " .. msg)
    os.exit(1)
end

else

-- ----------------------------------------------------------------
-- MicroNT backend — stubs.
--
-- When self-host arrives, these route through nt.dll.fs (NtCreateFile
-- / NtReadFile / NtWriteFile) and nt.tree (directory enumeration).
-- Until then they error so callers get a clear "build inside the
-- OS isn't wired yet" message rather than a confusing nil deref.
--
-- log() works today via the base library's print, which goes to
-- the inherited stdio handle (Control\Init\Stdio = COM1) — handy
-- for any pkg/ntosbe consumer that wants to be importable from
-- the OS side even before the file-I/O backend lands.
-- ----------------------------------------------------------------

local function todo(name)
    return function(...)
        error("ntosbe.platform." .. name ..
              ": MicroNT backend not implemented yet", 2)
    end
end

M.read_file     = todo("read_file")
M.write_file    = todo("write_file")
M.copy_file     = todo("copy_file")
M.file_size     = todo("file_size")
M.file_exists   = todo("file_exists")
M.is_dir        = todo("is_dir")
M.is_executable = todo("is_executable")
M.mtime         = todo("mtime")
M.now           = todo("now")
M.localtime     = todo("localtime")
M.list_dir      = todo("list_dir")
M.list_tree     = todo("list_tree")
M.mkdir_p       = todo("mkdir_p")
M.unlink        = todo("unlink")
M.rmdir         = todo("rmdir")
M.rmrf          = todo("rmrf")
M.realpath      = todo("realpath")
M.getcwd        = todo("getcwd")
M.setenv        = todo("setenv")
M.getenv        = todo("getenv")
M.environ       = todo("environ")
M.spawn_wait    = todo("spawn_wait")
M.die           = todo("die")

function M.log(msg)
    print(msg)
end

end

return M
