-- test.msfs — functional mailslot round-trips through msfs.sys.
--
-- Mailslots are datagram IPC: the server (read) end is created with
-- NtCreateMailslotFile, the client (write) end opens the same
-- \Device\Mailslot\<name> path. One write = one message; one read
-- returns one whole message, FIFO. Messages flow client -> server only.
--
-- This suite drives the mailslot FS end to end — create, open, message
-- round-trip and ordering, info query, peek, timeout behaviour,
-- zero-length and oversize-buffer edges, and the \Device\Mailslot
-- directory — exercising CREATE.C, CREATEMS.C, READ.C, WRITE.C,
-- READSUP.C, WRITESUP.C, DATASUP.C, FILEINFO.C, FSCONTRL.C and DIR.C,
-- which the (previously nonexistent) test coverage left almost entirely
-- dark. See [[project_kernel_coverage_tests]].
--
-- All single-threaded: the client write queues the datagram before the
-- server read runs, and a server created with read_timeout=0 returns
-- immediately (with the message if queued, STATUS_IO_TIMEOUT if not), so
-- nothing blocks.

local ffi = require('ffi')
local bit = require('bit')
local t   = require('test')
local fs  = require('nt.dll.fs')
local oa  = require('nt.dll.oa')
local se  = require('nt.dll.se')

t.suite("msfs: functional round-trips")

-- Fresh mailslot name per test so instances never collide on a re-run.
local namecount = 0
local function fresh()
    namecount = namecount + 1
    return "fms" .. namecount, "\\Device\\Mailslot\\fms" .. namecount
end

local function readstr(h, n)
    local buf = ffi.new('char[?]', n)
    local got = fs.NtReadFile(h, buf, n, nil)
    return ffi.string(buf, got)
end

-- A connected server+client pair on a fresh mailslot. `sopts` is passed
-- to create_mailslot; read_timeout defaults to 0 (immediate) so reads
-- never block the in-process runner.
local function pair(sopts)
    local _, path = fresh()
    sopts = sopts or {}
    sopts.name = path
    if sopts.read_timeout == nil then sopts.read_timeout = 0 end
    local server = fs.create_mailslot(sopts)
    local client = fs.open_mailslot(path)
    return server, client, path
end

-- ------------------------------------------------------------------
-- Single message round-trip.
-- ------------------------------------------------------------------

t.test("client write -> server read delivers one message", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(client, "datagram", 8, nil)
    t.eq(readstr(server, 64), "datagram", "message delivered")
end)

-- ------------------------------------------------------------------
-- FIFO ordering + info query (counts and next-size before draining).
-- ------------------------------------------------------------------

t.test("multiple messages are FIFO; query reports counts/size", function()
    local server, client = pair{ max_message_size = 1024 }
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(client, "first",    5, nil)
    fs.NtWriteFile(client, "second!!", 8, nil)

    local info = fs.mailslot_info(server)
    t.eq(info.MessagesAvailable,  2,    "two messages queued")
    t.eq(info.NextMessageSize,    5,    "next message size = len(first)")
    t.eq(info.MaximumMessageSize, 1024, "max message size round-trips")

    t.eq(readstr(server, 64), "first",    "first out")
    t.eq(readstr(server, 64), "second!!", "second out")
end)

t.test("empty mailslot reports MAILSLOT_NO_MESSAGE next size", function()
    local server = pair()
    t.defer(function() server:close() end)
    local info = fs.mailslot_info(server)
    t.eq(info.MessagesAvailable, 0, "no messages")
    t.eq(info.NextMessageSize, fs.MAILSLOT_NO_MESSAGE, "next size sentinel")
end)

-- ------------------------------------------------------------------
-- Peek — must not consume.
-- ------------------------------------------------------------------

t.test("peek reports the next message without consuming it", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(client, "peekme", 6, nil)
    local pk = fs.mailslot_peek(server, 64)
    t.eq(pk.message_length, 6, "peeked next message length")
    t.ok(pk.messages >= 1,     "at least one message visible")
    -- Still there: the real read returns it.
    t.eq(readstr(server, 64), "peekme", "peek did not consume")
end)

-- ------------------------------------------------------------------
-- Timeout behaviour — immediate read with no message.
-- ------------------------------------------------------------------

t.test("read with no message and timeout 0 is IO_TIMEOUT", function()
    local server = pair()    -- read_timeout = 0, nothing written
    t.defer(function() server:close() end)

    local ok, e = pcall(readstr, server, 64)
    t.ok(not ok, "read on an empty mailslot must fail (no block)")
    t.ok(tostring(e):match("c00000b5"),
         "expected IO_TIMEOUT, got " .. tostring(e))
end)

t.test("set_mailslot_timeout adjusts the read timeout", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    -- Re-arm to immediate (idempotent) — just exercise the set path; a
    -- queued message still reads back fine afterwards.
    fs.set_mailslot_timeout(server, 0)
    fs.NtWriteFile(client, "after-set", 9, nil)
    t.eq(readstr(server, 64), "after-set", "read works after set timeout")
end)

t.test("read with a finite timeout expires via the timer DPC", function()
    -- A short positive read timeout with no message queued: the read
    -- pends, a timer is armed, and when it fires the DPC (msfs DPC.C)
    -- completes the IRP with STATUS_IO_TIMEOUT. This is the timed path
    -- the timeout=0 (immediate) case above never reaches.
    local _, path = fresh()
    local server = fs.create_mailslot{ name = path, read_timeout = 0.1 }
    t.defer(function() server:close() end)

    local ok, e = pcall(readstr, server, 64)
    t.ok(not ok, "a finite-timeout read on an empty mailslot must time out")
    t.ok(tostring(e):match("c00000b5"),
         "expected IO_TIMEOUT, got " .. tostring(e))
end)

-- ------------------------------------------------------------------
-- Security and volume info — light up SEINFO.C and VOLINFO.C, which the
-- data round-trips never touch.
-- ------------------------------------------------------------------

t.test("NtQuerySecurityObject returns the mailslot's security descriptor", function()
    local server = pair()
    t.defer(function() server:close() end)
    local sd = se.get_object_security(server)
    t.ok(sd ~= nil, "got a security descriptor")
end)

t.test("FileFsAttributeInformation reports the FS name MSFS", function()
    -- msfs answers volume queries on the VCB only (MsCommonQueryVolume-
    -- Information rejects any other node type) — so open the file-system
    -- volume object itself: "\Device\Mailslot" with NO trailing name.
    local voa = oa.path("\\Device\\Mailslot")
    local vcb = fs.NtOpenFile(
        bit.bor(fs.FILE_GENERIC_READ, fs.SYNCHRONIZE), voa.oa,
        bit.bor(fs.FILE_SHARE_READ, fs.FILE_SHARE_WRITE),
        fs.FILE_SYNCHRONOUS_IO_NONALERT)
    t.defer(function() vcb:close() end)
    local vi = fs.volume_attribute_info(vcb)
    t.eq(vi.fs_name, "MSFS", "file-system name")
end)

-- ------------------------------------------------------------------
-- Edge cases — zero-length message, oversize-message vs small buffer.
-- ------------------------------------------------------------------

t.test("a zero-length message round-trips as zero bytes", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(client, "", 0, nil)
    local info = fs.mailslot_info(server)
    t.eq(info.MessagesAvailable, 1, "the empty message is queued")
    t.eq(info.NextMessageSize,   0, "next message size = 0")
    t.eq(readstr(server, 64), "", "reads back as zero bytes")
end)

t.test("reading into too small a buffer is BUFFER_TOO_SMALL", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(client, "toolong", 7, nil)
    -- Buffer smaller than the message: the read fails and the message is
    -- NOT consumed (msfs READSUP.C).
    local ok, e = pcall(readstr, server, 3)
    t.ok(not ok, "undersized read must fail")
    t.ok(tostring(e):match("c0000023"),
         "expected BUFFER_TOO_SMALL, got " .. tostring(e))
    -- Message survived: a properly sized read still gets it.
    t.eq(readstr(server, 64), "toolong", "message not consumed by failed read")
end)

-- ------------------------------------------------------------------
-- Directory enumeration of the \Device\Mailslot root (msfs DIR.C).
-- ------------------------------------------------------------------

t.test("\\Device\\Mailslot enumerates open mailslots", function()
    local leaf, path = fresh()
    local server = fs.create_mailslot{ name = path, read_timeout = 0 }
    t.defer(function() server:close() end)

    -- Trailing backslash → the root DCB (a directory); "\Device\Mailslot"
    -- with no slash opens the FS volume object, which isn't enumerable.
    local names = fs.list_dir("\\Device\\Mailslot\\")
    local seen = false
    for _, n in ipairs(names) do
        if n == leaf then seen = true break end
    end
    t.ok(seen, "our mailslot '" .. leaf .. "' appears in the device directory")
end)
