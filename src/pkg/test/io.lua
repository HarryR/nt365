-- io — Lua-side io module over nt.dll.fs.
--
-- Read-side coverage uses ntoskrnl.exe (large, well-known content) and
-- the registry hive's text artefacts. Write-side and seek/round-trip
-- coverage uses scratch files on the FAT root at \SystemRoot\.
-- Each write test cleans up after itself via os.remove (so a failed
-- pcall-isolated test doesn't leak a stale file into later tests).
--
-- Lifetime audit while reading these tests:
--   * Every io.open returns a File table holding an NT_HANDLE (self._h).
--     File:close goes through that NT_HANDLE wrapper — no raw HANDLE
--     ever surfaces to test code.
--   * Path strings to io.open / io.lines / os.remove are UTF-8 Lua
--     strings; conversion to UTF-16 happens inside oa.path / fs.set_rename.
--   * cdata buffers used internally by File:read / File:write are
--     local to the C-side wrappers; we never see them here.

local t   = require('test')
local io  = require('io')
local os  = require('os')

t.suite("io")

local SCRATCH = "\\SystemRoot\\__test_io.tmp"

-- Best-effort cleanup before each test that uses SCRATCH so leftover
-- state from a partially-failed prior run can't poison the result.
local function reset_scratch()
    os.remove(SCRATCH)
end

-- ------------------------------------------------------------------
-- Open / close / type
-- ------------------------------------------------------------------

t.test("io.open on missing file returns nil + errmsg", function()
    local f, err = io.open("\\SystemRoot\\__no_such_file.xyz", "r")
    t.eq(f, nil)
    t.ok(err and #err > 0, "got an error message")
end)

t.test("io.open on bad mode returns nil + errmsg", function()
    local f, err = io.open("\\SystemRoot\\anything", "zzz")
    t.eq(f, nil)
    t.ok(err and err:match("mode"), "errmsg mentions mode: " .. tostring(err))
end)

t.test("io.open ntoskrnl readable, io.type=='file'", function()
    local f = io.open("\\SystemRoot\\SYSTEM32\\NTOSKRNL.EXE", "rb")
    t.ne(f, nil)
    t.eq(io.type(f), "file")
    f:close()
    t.eq(io.type(f), "closed file")
end)

t.test("io.type returns nil for non-file values", function()
    t.eq(io.type(nil),     nil)
    t.eq(io.type("hello"), nil)
    t.eq(io.type({}),      nil)
    t.eq(io.type(42),      nil)
end)

t.test("close is idempotent", function()
    local f = io.open("\\SystemRoot\\SYSTEM32\\NTOSKRNL.EXE", "rb")
    t.eq(f:close(), true)
    t.eq(f:close(), true, "second close is a no-op, no raise")
end)

-- ------------------------------------------------------------------
-- Read formats
-- ------------------------------------------------------------------

t.test("read 'n' bytes — MZ magic from ntoskrnl", function()
    local f = io.open("\\SystemRoot\\SYSTEM32\\NTOSKRNL.EXE", "rb")
    local two = f:read(2)
    t.eq(two, "MZ")
    f:close()
end)

t.test("read past EOF returns nil", function()
    -- Open ntoskrnl, seek to end, read should give nil.
    local f = io.open("\\SystemRoot\\SYSTEM32\\NTOSKRNL.EXE", "rb")
    f:seek("end")
    t.eq(f:read(16), nil, "at EOF, read returns nil")
    f:close()
end)

t.test("read '*a' returns all bytes when small", function()
    -- Use a tiny scratch file so '*a' is fast.
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("hello, world\n")
    w:close()

    local r = io.open(SCRATCH, "rb")
    local all = r:read("*a")
    t.eq(all, "hello, world\n")
    r:close()
    os.remove(SCRATCH)
end)

t.test("read '*l' iterates lines, strips trailing \\r and \\n", function()
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("alpha\nbeta\r\ngamma")        -- last line has no trailing \n
    w:close()

    local r = io.open(SCRATCH, "rb")
    t.eq(r:read("*l"), "alpha")
    t.eq(r:read("*l"), "beta",  "CRLF stripped")
    t.eq(r:read("*l"), "gamma", "trailing line without \\n returned")
    t.eq(r:read("*l"), nil,     "next call returns nil at EOF")
    r:close()
    os.remove(SCRATCH)
end)

t.test("read with no args defaults to '*l'", function()
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("first\nsecond\n")
    w:close()
    local r = io.open(SCRATCH, "rb")
    t.eq(r:read(), "first")
    r:close()
    os.remove(SCRATCH)
end)

t.test("read multiple formats in one call returns multiple values", function()
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("ABCDEF\nXYZ\n")
    w:close()
    local r = io.open(SCRATCH, "rb")
    local a, b, c = r:read(3, "*l", "*l")
    t.eq(a, "ABC")
    t.eq(b, "DEF",  "after the 3 bytes, '*l' picks up the rest of line 1")
    t.eq(c, "XYZ")
    r:close()
    os.remove(SCRATCH)
end)

-- ------------------------------------------------------------------
-- Write
-- ------------------------------------------------------------------

t.test("write strings + numbers, chains via return self", function()
    reset_scratch()
    local f = io.open(SCRATCH, "wb")
    local same = f:write("A=", 42, " B=", 3.14, "\n")
    t.eq(same, f, "write returns the file for chaining")
    f:close()

    local r = io.open(SCRATCH, "rb")
    t.eq(r:read("*a"), "A=42 B=3.14\n")
    r:close()
    os.remove(SCRATCH)
end)

t.test("write rejects non-string/non-number", function()
    reset_scratch()
    local f = io.open(SCRATCH, "wb")
    t.raises(function() f:write({}) end, "write")
    f:close()
    os.remove(SCRATCH)
end)

-- ------------------------------------------------------------------
-- Seek
-- ------------------------------------------------------------------

t.test("seek 'set' / 'cur' / 'end' return new positions", function()
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("0123456789ABCDEF")             -- 16 bytes
    w:close()

    local r = io.open(SCRATCH, "rb")
    t.eq(r:seek("set", 4), 4)
    t.eq(r:read(2), "45")                    -- pos was 4, read 2
    t.eq(r:seek("cur", -3), 3)               -- back up 3 from cur (=6)
    t.eq(r:read(2), "34")
    t.eq(r:seek("end"), 16,  "seek to EOF reports total size")
    t.eq(r:read(1), nil,     "read at EOF returns nil")
    t.eq(r:seek("set", 0), 0)
    t.eq(r:read(4), "0123",  "seek invalidated buffer; re-read works")
    r:close()
    os.remove(SCRATCH)
end)

-- ------------------------------------------------------------------
-- Append mode
-- ------------------------------------------------------------------

t.test("'a' mode opens at EOF, preserves existing content", function()
    reset_scratch()
    local f = io.open(SCRATCH, "wb")
    f:write("first")
    f:close()

    local f2 = io.open(SCRATCH, "ab")
    f2:write(" second")
    f2:close()

    local r = io.open(SCRATCH, "rb")
    t.eq(r:read("*a"), "first second")
    r:close()
    os.remove(SCRATCH)
end)

-- ------------------------------------------------------------------
-- Lines iteration
-- ------------------------------------------------------------------

t.test("io.lines iterates a file then closes", function()
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("a\nb\nc\n")
    w:close()

    local got = {}
    for line in io.lines(SCRATCH) do
        got[#got+1] = line
    end
    t.eq(#got, 3)
    t.eq(got[1], "a")
    t.eq(got[2], "b")
    t.eq(got[3], "c")
    os.remove(SCRATCH)
end)

t.test("file:lines iterates without closing", function()
    reset_scratch()
    local w = io.open(SCRATCH, "wb")
    w:write("x\ny\n")
    w:close()

    local r = io.open(SCRATCH, "rb")
    local got = {}
    for line in r:lines() do got[#got+1] = line end
    t.eq(#got, 2)
    t.eq(io.type(r), "file", "file:lines doesn't auto-close")
    r:close()
    os.remove(SCRATCH)
end)

-- ------------------------------------------------------------------
-- Operations on closed files / unsupported APIs
-- ------------------------------------------------------------------

t.test("read on closed file raises", function()
    local f = io.open("\\SystemRoot\\SYSTEM32\\NTOSKRNL.EXE", "rb")
    f:close()
    t.raises(function() f:read(2) end, "closed")
end)

t.test("flush returns the file (no-op stub)", function()
    reset_scratch()
    local f = io.open(SCRATCH, "wb")
    t.eq(f:flush(), f)
    f:close()
    os.remove(SCRATCH)
end)

t.test("setvbuf returns the file (no-op stub)", function()
    reset_scratch()
    local f = io.open(SCRATCH, "wb")
    t.eq(f:setvbuf("no"),   f)
    t.eq(f:setvbuf("full", 4096), f)
    f:close()
    os.remove(SCRATCH)
end)

t.test("io.popen / tmpfile / read / write raise with clear message", function()
    t.raises(io.popen,   "popen")
    t.raises(io.tmpfile, "tmpfile")
    t.raises(io.read,    "read")
    t.raises(io.write,   "write")
end)

t.test("io.lines without filename raises (no default stdin)", function()
    t.raises(function() io.lines() end, "stdin")
end)
