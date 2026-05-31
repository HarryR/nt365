-- preamble.lua — runtime bootstrap, loaded by lua.dll's
-- __wrap_luaL_openlibs hook (src/cr/lua_main.c) *before* the entry
-- script named on the command line runs.  Staged on disk at
-- \SystemRoot\System32\preamble.lua (beside lua.exe / lua.dll), loaded
-- by absolute path so it needs no package.path of its own.
--
-- Three jobs, in order:
--   1. Set package.path/cpath to the single \SystemRoot\pkg\ root.
--   2. Install a STORED-zip require() searcher so a package can ship
--      as one  pkg\<name>.zip  (member <name>/<sub>.lua).  Appended
--      after the default file searcher → loose files shadow a zip.
--   3. Restore the io/os globals (LuaJIT's built-in lib_io/lib_os are
--      compiled out via -DLUAJIT_DISABLE_LIB_{IO,OS}; stock Lua code
--      expects io/os as globals without require()).
--
-- This is NOT nt.boot — that's the OS init-process startup task
-- (\NLS\, \DosDevices\C:) and stays in the boot entry scripts.  The
-- preamble runs for *every* lua.exe invocation, OS-init or not.
--
-- The zip searcher is deliberately self-contained: it binds the three
-- ntdll calls it needs by hand and MUST NOT require('nt.*') — nt
-- itself ships as pkg\nt.zip, so the searcher is what loads it
-- (circular otherwise).  It uses private struct names (Z* prefix) so
-- it never collides with nt.dll.init's canonical UNICODE_STRING /
-- OBJECT_ATTRIBUTES typedefs (LuaJIT errors on duplicate struct
-- typedefs), and declares the Nt* functions with void* pointer params
-- + passes ffi.cast('void*', …) arguments so the later canonical
-- re-declarations in nt.dll.fs / handle stay call-compatible (void*
-- converts both directions).

local ffi = require('ffi')

-- ------------------------------------------------------------------
-- 1. Package path — single \SystemRoot\pkg\ root.
-- ------------------------------------------------------------------
package.path  = "\\SystemRoot\\pkg\\?.lua;\\SystemRoot\\pkg\\?\\init.lua"
package.cpath = ""

-- ------------------------------------------------------------------
-- 2. STORED-zip require() searcher.
-- ------------------------------------------------------------------
ffi.cdef[[
#pragma pack(push, 4)
typedef struct _ZUS {
    unsigned short Length;
    unsigned short MaximumLength;
    wchar_t       *Buffer;
} ZUS;

typedef struct _ZOA {
    unsigned long  Length;
    void          *RootDirectory;
    ZUS           *ObjectName;
    unsigned long  Attributes;
    void          *SecurityDescriptor;
    void          *SecurityQualityOfService;
} ZOA;

/* OBJECT_ATTRIBUTES + UNICODE_STRING + wchar buffer fused into one
 * allocation so the internal pointers can't dangle (the Shape-1
 * aliasing hazard nt.dll.oa documents). */
typedef struct _ZOAPATH {
    ZOA     oa;
    ZUS     us;
    wchar_t wbuf[?];
} ZOAPATH;

typedef struct _ZIOSB { long Status; unsigned long Information; } ZIOSB;
#pragma pack(pop)

/* void* pointer params: bulletproof against the canonical typed
 * re-declarations nt.dll.fs / nt.dll.handle make later. */
long __stdcall NtOpenFile(void *FileHandle, unsigned long DesiredAccess,
                          void *ObjectAttributes, void *IoStatusBlock,
                          unsigned long ShareAccess, unsigned long OpenOptions);
long __stdcall NtReadFile(void *FileHandle, void *Event, void *ApcRoutine,
                          void *ApcContext, void *IoStatusBlock, void *Buffer,
                          unsigned long Length, void *ByteOffset, void *Key);
long __stdcall NtClose(void *Handle);
]]

local ntdll = ffi.load('ntdll')

local FILE_GENERIC_READ          = 0x00120089   -- includes SYNCHRONIZE
local FILE_SHARE_READ            = 0x00000001
local FILE_OPEN_OPTS             = 0x00000060   -- SYNCHRONOUS_IO_NONALERT | NON_DIRECTORY
local OBJ_CASE_INSENSITIVE       = 0x00000040
local STATUS_SUCCESS             = 0
local CHUNK                      = 65536
local STATUS_END_OF_FILE           = 0xC0000011
local STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034
local STATUS_OBJECT_PATH_NOT_FOUND = 0xC000003A

-- NtOpenFile / NtReadFile return a signed `long`; fold it to the
-- conventional unsigned 0xC.......  NTSTATUS form so it compares against
-- the constants above (and prints correctly).
local function nt_status(st) return st % 0x100000000 end

-- Open an NT-namespace path and return its entire contents as a Lua
-- string, or nil if it can't be opened (e.g. archive doesn't exist).
local function read_whole_file(nt_path)
    -- Build the fused OBJECT_ATTRIBUTES.  Path is ASCII, so widen byte
    -- by byte (no UTF-8 decode needed).
    local n   = #nt_path
    local oap = ffi.new('ZOAPATH', n + 1)
    for i = 1, n do oap.wbuf[i - 1] = nt_path:byte(i) end
    oap.wbuf[n] = 0
    oap.us.Buffer        = oap.wbuf
    oap.us.Length        = n * 2
    oap.us.MaximumLength = (n + 1) * 2
    -- OBJECT_ATTRIBUTES.Length must equal sizeof(OBJECT_ATTRIBUTES)
    -- exactly (the kernel's ObpCaptureObjectAttributes rejects any
    -- other value).  ZOA mirrors that layout — 24 bytes on i386.
    oap.oa.Length        = ffi.sizeof('ZOA')
    oap.oa.ObjectName    = oap.us
    oap.oa.Attributes    = OBJ_CASE_INSENSITIVE

    local h    = ffi.new('void *[1]')
    local iosb = ffi.new('ZIOSB')
    local st   = ntdll.NtOpenFile(ffi.cast('void *', h), FILE_GENERIC_READ,
                                  ffi.cast('void *', oap),
                                  ffi.cast('void *', iosb),
                                  FILE_SHARE_READ, FILE_OPEN_OPTS)
    if st ~= STATUS_SUCCESS then
        -- A genuinely-absent archive (OBJECT_{NAME,PATH}_NOT_FOUND) is the
        -- normal "fall through to the next searcher" case — no status to
        -- surface.  Any other failure (sharing, resources, ...) is
        -- transient/unexpected: hand the status back so the caller reports it.
        local s = nt_status(st)
        if s == STATUS_OBJECT_NAME_NOT_FOUND or s == STATUS_OBJECT_PATH_NOT_FOUND then
            return nil
        end
        return nil, s
    end

    local buf    = ffi.new('unsigned char[?]', CHUNK)
    local chunks = {}
    local last_rst = STATUS_SUCCESS
    while true do
        iosb.Status = 0; iosb.Information = 0
        local rst = ntdll.NtReadFile(h[0], nil, nil, nil,
                                     ffi.cast('void *', iosb),
                                     ffi.cast('void *', buf), CHUNK, nil, nil)
        last_rst = rst
        local got = tonumber(iosb.Information)
        if got > 0 then chunks[#chunks + 1] = ffi.string(buf, got) end
        -- STATUS_SUCCESS with a short/zero read, or any non-success
        -- (STATUS_END_OF_FILE etc.), ends the loop.
        if rst ~= STATUS_SUCCESS or got == 0 then break end
    end
    ntdll.NtClose(h[0])
    -- A clean read ends on STATUS_SUCCESS (short/zero read) or
    -- STATUS_END_OF_FILE.  Anything else is a real, partial read: discard
    -- the truncated blob and surface the status rather than handing back a
    -- corrupt archive (which would fail the central-dir parse downstream).
    local s = nt_status(last_rst)
    if s ~= STATUS_SUCCESS and s ~= STATUS_END_OF_FILE then
        return nil, s
    end
    return table.concat(chunks)
end

-- Little-endian readers over a Lua string (1-based index).
local function u16(s, i) local a, b = s:byte(i, i + 1); return a + b * 256 end
local function u32(s, i)
    local a, b, c, d = s:byte(i, i + 3)
    return a + b * 256 + c * 65536 + d * 16777216
end

-- Parse a STORED zip's central directory into name -> {off, size}.
-- `off` is the absolute byte offset of the member's data; `size` is
-- the stored (== uncompressed) length.  Returns nil on a malformed or
-- non-zip blob.  Members using a compression method other than STORED
-- are skipped (we only ever emit STORED).
local function parse_central_dir(blob)
    local len = #blob
    if len < 22 then return nil end
    -- No archive comment in our writer, so EOCD is the final 22 bytes.
    local eo = len - 21
    if u32(blob, eo) ~= 0x06054b50 then return nil end
    local count   = u16(blob, eo + 10)
    local cd_size = u32(blob, eo + 12)
    local cd_off  = u32(blob, eo + 16)
    local index = {}
    local p = cd_off + 1
    for _ = 1, count do
        if u32(blob, p) ~= 0x02014b50 then return nil end
        local method   = u16(blob, p + 10)
        local comp_size = u32(blob, p + 20)
        local namelen  = u16(blob, p + 28)
        local extralen = u16(blob, p + 30)
        local cmtlen   = u16(blob, p + 32)
        local lh_off   = u32(blob, p + 42)
        local name     = blob:sub(p + 46, p + 46 + namelen - 1)
        if method == 0 then
            -- Local header: 30 fixed bytes + name + (local) extra.
            local lh_namelen  = u16(blob, lh_off + 1 + 26)
            local lh_extralen = u16(blob, lh_off + 1 + 28)
            local data_off    = lh_off + 30 + lh_namelen + lh_extralen
            index[name] = { off = data_off, size = comp_size }
        end
        p = p + 46 + namelen + extralen + cmtlen
    end
    return index
end

-- Per-archive cache: archive base name -> { blob = <bytes>, index = <table> }.
-- SUCCESS only — never a negative/absent entry (see load_archive).
local archive_cache = {}

-- Returns the cached archive entry, or (nil[, status]) on failure.
--
-- Caches SUCCESS only: a hit short-circuits the whole read+parse, so we
-- never re-read or re-parse nt.zip on a later require('nt.*').  Failures
-- (absent OR transient) are deliberately NOT cached — like Lua's stock
-- searchers, a miss is simply re-probed next time.  Caching absence here
-- used to turn a one-off read hiccup into a PERMANENT require failure for
-- the whole lua_State (a worker thread that fumbled its first nt.zip read
-- could never require('nt.*') again).  `status` is the surfaced NTSTATUS
-- for a non-not-found read failure (nil for genuine absence).
local function load_archive(base)
    local cached = archive_cache[base]
    if cached then return cached end
    local blob, status = read_whole_file("\\SystemRoot\\pkg\\" .. base .. ".zip")
    if not blob then return nil, status end
    local index = parse_central_dir(blob)
    if not index then return nil end
    local entry = { blob = blob, index = index }
    archive_cache[base] = entry
    return entry
end

-- The searcher: module a.b.c -> archive pkg\a.zip, member a/b/c.lua
-- (or a/b/c/init.lua).  Returns a loader chunk, or an error string
-- (LuaJIT appends it to the "module not found" report).
local function zip_searcher(modname)
    local base = modname:match("^([^.]+)")
    if not base then return "\n\tzip: bad module name" end
    local arc, status = load_archive(base)
    if not arc then
        -- Genuine absence -> the plain message (the module just isn't here).
        -- A surfaced status means the archive read FAILED unexpectedly
        -- (sharing, resources, partial read, ...) -- report it so a flaky
        -- load is diagnosable instead of masquerading as "not found".
        if status then
            return string.format(
                "\n\tzip: pkg\\%s.zip read failed (NTSTATUS 0x%08X)", base, status)
        end
        return "\n\tzip: no pkg\\" .. base .. ".zip"
    end

    local rel = modname:gsub("%.", "/")
    for _, member in ipairs({ rel .. ".lua", rel .. "/init.lua" }) do
        local m = arc.index[member]
        if m then
            local src = arc.blob:sub(m.off + 1, m.off + m.size)
            local chunk, err = loadstring(src, "@" .. base .. ".zip:" .. member)
            if not chunk then error(err) end
            return chunk
        end
    end
    return "\n\tzip: no member for " .. modname .. " in " .. base .. ".zip"
end

-- Append after the default loaders (loose .lua wins over a zip member).
package.loaders[#package.loaders + 1] = zip_searcher

-- ------------------------------------------------------------------
-- 3. Restore io / os globals (built-ins compiled out).  io.lua and
--    os.lua stay loose (single-file packages), resolved by the
--    default file searcher above — no dependency on the zip path.
-- ------------------------------------------------------------------
_G.io = require('io')
_G.os = require('os')
