-- io — Lua's standard `io` module, reimplemented over nt.dll.
--
-- This is a Lua-side replacement for LuaJIT's built-in lib_io.c, which
-- we compile out (see -DLUAJIT_DISABLE_LIB_IO in src/cr/Makefile). The
-- built-in expected POSIX/CRT FILE* primitives; we go straight to NT
-- via nt.dll.fs and avoid pulling those into librt.
--
-- Coverage (v1):
--   io.open(path, mode)             real file via NtCreateFile.
--   io.lines(path [, fmts...])      iterator that opens + closes path.
--   io.type(obj)                    "file" / "closed file" / nil.
--   io.close(file)                  delegates to file:close(); raises
--                                   without an explicit file (no
--                                   default output stream in v1).
--   io.popen, io.tmpfile            raise "not supported" — pipe needs
--                                   process creation, tmpfile needs an
--                                   NT-namespace tmp dir convention.
--   io.read, io.write,              raise — io.stdin/stdout/stderr need
--   io.input, io.output             PEB walk for the inherited handles.
--                                   Use file methods on real files,
--                                   or print() for console output.
--
-- File methods:
--   f:read(...)        formats: "*a"/"a", "*l"/"l", n (bytes).
--                      Multiple formats → multiple return values.
--                      "*n" / "*L" not implemented.
--   f:write(...)       strings + numbers.
--   f:seek(whence,off) "set"/"cur"/"end". Defaults to ("cur", 0).
--                      Returns new position. Invalidates read buffer.
--   f:close()          NtClose via NT_HANDLE __gc; idempotent.
--   f:flush()          no-op (we're unbuffered on the kernel side).
--   f:lines(...)       iterator over self:read(...).
--   f:setvbuf(...)     no-op stub returning the file (for chaining).

local ffi    = require('ffi')
local bit    = require('bit')
local fs     = require('nt.dll.fs')
local oa_mod = require('nt.dll.oa')

-- All NT-side constants come from nt.dll.fs — anything magic here
-- would be a duplicate that silently desyncs if the canonical value
-- ever changed. Compose the wide-open share mask we want for tools
-- running alongside us (selftest writers, future multi-process
-- tests); the bit definitions still live in fs.
local FILE_SHARE_ALL = fs.FILE_SHARE_READ + fs.FILE_SHARE_WRITE
                     + fs.FILE_SHARE_DELETE

local M = {}

-- ------------------------------------------------------------------
-- File class
-- ------------------------------------------------------------------
--
-- Lifetime: the underlying NT_HANDLE (self._h) owns the kernel handle.
-- :close() invokes NT_HANDLE:close() which NtCloses idempotently; the
-- File table's GC just lets the contained NT_HANDLE die naturally
-- (its own __gc closes the handle if not already explicit).
--
-- Read buffer: simple Lua-string accumulator. Filled in 4K chunks from
-- NtReadFile; consumed front-first by :read. :seek invalidates it.

local File = {}
File.__index = File

local READ_CHUNK = 4096

-- Refill internal buffer from the handle. Returns bytes appended; 0 on EOF.
function File:_fill(min_bytes)
    if self._eof then return 0 end
    local want = min_bytes and math.max(min_bytes, READ_CHUNK) or READ_CHUNK
    local cbuf = ffi.new('char[?]', want)
    local got, status = fs.NtReadFile(self._h, cbuf, want, nil)
    if got == 0 then
        self._eof = true
        return 0
    end
    self._buf = self._buf .. ffi.string(cbuf, got)
    -- Partial read with EOF marker: keep the bytes we got, but mark
    -- EOF so the next _fill call short-circuits.
    if status == fs.STATUS_END_OF_FILE then self._eof = true end
    return got
end

local function read_all(f)
    while not f._eof do f:_fill() end
    local s = f._buf
    f._buf = ""
    if s == "" then return nil end
    return s
end

local function read_line(f)
    while true do
        local nl = f._buf:find("\n", 1, true)
        if nl then
            local line = f._buf:sub(1, nl - 1)
            -- Strip trailing \r so CRLF line endings come back the same
            -- as LF. Lua's stock io does this.
            if line:sub(-1) == "\r" then line = line:sub(1, -2) end
            f._buf = f._buf:sub(nl + 1)
            return line
        end
        if f:_fill() == 0 then
            -- EOF without trailing newline. If we have something, return
            -- it; otherwise nil (stock Lua behavior).
            if #f._buf == 0 then return nil end
            local line = f._buf
            f._buf = ""
            return line
        end
    end
end

local function read_n(f, n)
    if n == 0 then
        -- Stock Lua: read(0) returns "" if not at EOF, nil at EOF.
        if #f._buf > 0 then return "" end
        if f:_fill() == 0 then return nil end
        return ""
    end
    while #f._buf < n and not f._eof do f:_fill(n - #f._buf) end
    if #f._buf == 0 then return nil end
    local take = math.min(n, #f._buf)
    local s = f._buf:sub(1, take)
    f._buf = f._buf:sub(take + 1)
    return s
end

local function read_one(f, fmt)
    if type(fmt) == "number" then
        return read_n(f, fmt)
    end
    if fmt == "*a" or fmt == "a" then return read_all(f) end
    if fmt == "*l" or fmt == "l" then return read_line(f) end
    error("bad read format: " .. tostring(fmt), 3)
end

function File:read(...)
    if self._closed then error("attempt to use a closed file", 2) end
    local n = select('#', ...)
    if n == 0 then return read_line(self) end       -- default = "*l"
    if n == 1 then return read_one(self, (...)) end
    local out = {}
    for i = 1, n do
        out[i] = read_one(self, (select(i, ...)))
        if out[i] == nil then break end
    end
    return unpack(out, 1, n)
end

function File:write(...)
    if self._closed then error("attempt to use a closed file", 2) end
    for i = 1, select('#', ...) do
        local arg = (select(i, ...))
        local s
        local at = type(arg)
        if at == "string" then
            s = arg
        elseif at == "number" then
            s = tostring(arg)
        else
            error("invalid argument #" .. i ..
                  " to 'write' (string expected, got " .. at .. ")", 2)
        end
        if #s > 0 then
            -- Allocate per-write — simpler than pooling; LuaJIT's
            -- short-lived ffi.new is cheap. Keep `cbuf` as a local so
            -- it stays reachable through NtWriteFile's argument copy.
            local cbuf = ffi.new('char[?]', #s)
            ffi.copy(cbuf, s, #s)
            fs.NtWriteFile(self._h, cbuf, #s, nil)
        end
    end
    return self
end

function File:seek(whence, offset)
    if self._closed then error("attempt to use a closed file", 2) end
    whence = whence or "cur"
    offset = offset or 0
    local target
    if whence == "set" then
        target = offset
    elseif whence == "cur" then
        -- _fill reads ahead in 4K chunks past the user's logical position;
        -- the kernel pointer reflects the read-ahead, not what the user
        -- has consumed. Subtract the unread tail of the buffer to get
        -- the logical "current" position the caller expects.
        target = fs.get_position(self._h) - #self._buf + offset
    elseif whence == "end" then
        local info = fs.query_standard(self._h)
        target = tonumber(info.EndOfFile.QuadPart) + offset
    else
        error("bad whence: " .. tostring(whence), 2)
    end
    fs.set_position(self._h, target)
    -- Drop the read-ahead buffer — it now points at the wrong file
    -- offset. Same with the EOF flag; we may have seeked back from EOF.
    self._buf = ""
    self._eof = false
    return target
end

function File:close()
    if self._closed then return true end
    self._closed = true
    if self._h then
        self._h:close()                 -- NT_HANDLE:close — idempotent
        self._h = nil
    end
    return true
end

function File:flush()
    if self._closed then error("attempt to use a closed file", 2) end
    -- We're unbuffered: every :write goes straight to NtWriteFile, which
    -- the kernel may itself buffer. NtFlushBuffersFile would push to
    -- disk; we don't bridge it yet (see fs.lua's TODO list). No-op for v1.
    return self
end

function File:lines(...)
    local fmts = {...}
    if #fmts == 0 then fmts = {"*l"} end
    local self_capture = self
    return function()
        if self_capture._closed then return nil end
        local n = #fmts
        if n == 1 then return read_one(self_capture, fmts[1]) end
        local out = {}
        for i = 1, n do
            out[i] = read_one(self_capture, fmts[i])
            if out[i] == nil then
                if i == 1 then return nil end
                break
            end
        end
        return unpack(out, 1, n)
    end
end

function File:setvbuf(_mode, _size)
    -- Stock signature: file:setvbuf(mode [, size]) where mode is "no"
    -- "full" "line". We have no buffering layer to configure; return
    -- self for caller chaining.
    if self._closed then error("attempt to use a closed file", 2) end
    return self
end

-- ------------------------------------------------------------------
-- Mode parsing
-- ------------------------------------------------------------------
-- Stock Lua modes: r, w, a, r+, w+, a+, with optional 'b' suffix
-- (binary; no-op on NT — there's no CRLF translation in NtRead/Write).

local function parse_mode(mode)
    mode = mode or "r"
    if type(mode) ~= "string" then
        return nil, "bad mode: " .. tostring(mode)
    end
    local m = mode:gsub("b", "")
    local access, disp, append, can_read, can_write
    if m == "r" then
        access, disp = fs.FILE_GENERIC_READ, fs.FILE_OPEN
        can_read = true
    elseif m == "w" then
        access, disp = fs.FILE_GENERIC_WRITE, fs.FILE_OVERWRITE_IF
        can_write = true
    elseif m == "a" then
        access, disp = fs.FILE_GENERIC_WRITE, fs.FILE_OPEN_IF
        can_write, append = true, true
    elseif m == "r+" then
        -- bit.bor, NOT `+`: FILE_GENERIC_READ and FILE_GENERIC_WRITE
        -- share SYNCHRONIZE and READ_CONTROL — adding them
        -- arithmetically carries those bits into garbage positions and
        -- the syscall rejects the resulting access mask.
        access = bit.bor(fs.FILE_GENERIC_READ, fs.FILE_GENERIC_WRITE)
        disp   = fs.FILE_OPEN
        can_read, can_write = true, true
    elseif m == "w+" then
        access = bit.bor(fs.FILE_GENERIC_READ, fs.FILE_GENERIC_WRITE)
        disp   = fs.FILE_OVERWRITE_IF
        can_read, can_write = true, true
    elseif m == "a+" then
        access = bit.bor(fs.FILE_GENERIC_READ, fs.FILE_GENERIC_WRITE)
        disp   = fs.FILE_OPEN_IF
        can_read, can_write, append = true, true, true
    else
        return nil, "bad mode: " .. mode
    end
    return {access=access, disp=disp, append=append,
            can_read=can_read, can_write=can_write}
end

-- ------------------------------------------------------------------
-- Module surface
-- ------------------------------------------------------------------

function M.open(path, mode)
    local mp, perr = parse_mode(mode)
    if not mp then return nil, perr end
    local noa = oa_mod.path(path)         -- fused NT_OA_PATH cdata
    -- FILE_GENERIC_READ / FILE_GENERIC_WRITE already include SYNCHRONIZE
    -- (bit 20 is in their composite definitions). DON'T add it again with
    -- `+` — that's arithmetic addition, not OR, so the same bit set twice
    -- carries into bit 21 and the access mask becomes garbage that
    -- NtCreateFile rejects. Synchronous-handle access is already covered
    -- by mp.access alone.
    local ok, h = pcall(fs.NtCreateFile,
        mp.access,
        noa.oa,
        nil,                              -- AllocationSize
        fs.FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_ALL,
        mp.disp,
        fs.FILE_SYNCHRONOUS_IO_NONALERT + fs.FILE_NON_DIRECTORY_FILE,
        nil, 0)
    if not ok then
        return nil, tostring(h)           -- h holds the err object on failure
    end
    -- Append mode: NT has no open-time "always write at EOF" without
    -- using FILE_APPEND_DATA-only access (which conflicts with read+
    -- access). Seek-to-end on open is good enough for v1; concurrent
    -- writers can still race past the EOF here, so document that if
    -- atomic-append callers ever appear.
    if mp.append then
        local info = fs.query_standard(h)
        fs.set_position(h, tonumber(info.EndOfFile.QuadPart))
    end
    return setmetatable({
        _h         = h,
        _buf       = "",
        _eof       = false,
        _closed    = false,
        _can_read  = mp.can_read,
        _can_write = mp.can_write,
    }, File)
end

function M.lines(path, ...)
    if path == nil then
        error("io.lines without filename: no default input stream "
              .. "(io.stdin not implemented in v1)", 2)
    end
    local fmts = {...}
    if #fmts == 0 then fmts = {"*l"} end
    local f, ferr = M.open(path, "r")
    if not f then error(ferr, 2) end
    return function()
        if f._closed then return nil end
        local n = #fmts
        if n == 1 then
            local v = read_one(f, fmts[1])
            if v == nil then f:close(); return nil end
            return v
        end
        local out = {}
        for i = 1, n do
            out[i] = read_one(f, fmts[i])
            if out[i] == nil then
                if i == 1 then f:close(); return nil end
                break
            end
        end
        return unpack(out, 1, n)
    end
end

function M.type(obj)
    if type(obj) ~= "table" then return nil end
    if getmetatable(obj) ~= File then return nil end
    return obj._closed and "closed file" or "file"
end

function M.close(file)
    if file == nil then
        error("io.close without argument: no default output stream "
              .. "(io.stdout not implemented in v1)", 2)
    end
    return file:close()
end

local function not_supported(name)
    return function()
        error("io." .. name .. ": not supported on micront", 2)
    end
end

M.popen   = not_supported("popen")
M.tmpfile = not_supported("tmpfile")
M.read    = not_supported("read")
M.write   = not_supported("write")
M.input   = not_supported("input")
M.output  = not_supported("output")

-- io.stdin / io.stdout / io.stderr — left undefined in v1. Wiring them
-- requires reading the inherited handles from
-- PEB->ProcessParameters->Standard{Input,Output,Error} via
-- NtQueryInformationProcess, which is its own follow-up alongside
-- process creation. For now: print() for output, file methods on real
-- files for everything else.

return M
