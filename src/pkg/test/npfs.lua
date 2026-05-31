-- test.npfs — functional named-pipe round-trips through npfs.sys.
--
-- The hardening suite (test.fuzz.npfs) proves usermode can't bugcheck
-- the npfs create path; THIS suite proves the pipe actually works —
-- connect, bidirectional read/write, message framing, peek, state
-- queries, mode changes, disconnect, transceive, and the
-- \Device\NamedPipe directory / wait surface. Together they drive most
-- of npfs (READ.C, WRITE.C, FSCTRL.C, FILEINFO.C, STATESUP.C, DATASUP.C,
-- DIR.C) from usermode, lifting the kernel-coverage the create-only path
-- left flat. See [[project_kernel_coverage_tests]].
--
-- Almost everything here is single-threaded by design: a client open
-- auto-connects to the listening server instance (npfs CREATE.C), and on
-- a buffered pipe a write that fits completes synchronously, so a read
-- on the peer end has data waiting and returns at once — no second
-- thread, no blocking. The one operation that genuinely pends until a
-- peer replies (FSCTL_PIPE_TRANSCEIVE) uses an nt.thread responder.

local ffi    = require('ffi')
local bit    = require('bit')
local t      = require('test')
local fs     = require('nt.dll.fs')
local se     = require('nt.dll.se')
local thread = require('nt.thread')
local handle = require('nt.dll.handle')

-- DEVICE_TYPE returned by FileFsDeviceInformation on a named pipe
-- (DEVIOCTL.H FILE_DEVICE_NAMED_PIPE).
local FILE_DEVICE_NAMED_PIPE = 0x11

t.suite("npfs: functional round-trips")

local SYNC = fs.FILE_SYNCHRONOUS_IO_NONALERT

-- Fresh pipe name per test so instances never collide on a re-run.
local namecount = 0
local function fresh()
    namecount = namecount + 1
    return "fnp" .. namecount, "\\Device\\NamedPipe\\fnp" .. namecount
end

-- Read up to `n` bytes from a synchronous pipe handle into a fresh
-- buffer; return the bytes as a Lua string.
local function readstr(h, n)
    local buf = ffi.new('char[?]', n)
    local got = fs.NtReadFile(h, buf, n, nil)
    return ffi.string(buf, got)
end

-- Open a connected server+client pair on a fresh pipe. Returns both
-- wrapped handles; the caller t.defer's their close. `opts` is passed to
-- create_named_pipe (type / read_mode / etc.); SYNC + queue completion
-- are forced so reads block-until-data rather than pending.
local function pair(opts)
    local _, path = fresh()
    opts = opts or {}
    opts.name            = path
    opts.options         = SYNC
    opts.completion_mode = fs.FILE_PIPE_QUEUE_OPERATION
    local server = fs.create_named_pipe(opts)
    local client = fs.open_pipe(path)
    return server, client, path
end

-- ------------------------------------------------------------------
-- Connect + state.
-- ------------------------------------------------------------------

t.test("client open auto-connects; local info reflects CONNECTED", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    local si = fs.pipe_local_info(server)
    local ci = fs.pipe_local_info(client)
    t.eq(si.NamedPipeState, fs.FILE_PIPE_CONNECTED_STATE, "server state")
    t.eq(ci.NamedPipeState, fs.FILE_PIPE_CONNECTED_STATE, "client state")
    t.eq(si.NamedPipeEnd,   fs.FILE_PIPE_SERVER_END,       "server end")
    t.eq(ci.NamedPipeEnd,   fs.FILE_PIPE_CLIENT_END,       "client end")
    t.eq(si.CurrentInstances, 1, "one instance")
end)

-- ------------------------------------------------------------------
-- Byte-stream and message-mode data flow.
-- ------------------------------------------------------------------

t.test("byte-stream round-trip in both directions", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(server, "hello", 5, nil)
    t.eq(readstr(client, 16), "hello", "server -> client")

    fs.NtWriteFile(client, "world!", 6, nil)
    t.eq(readstr(server, 16), "world!", "client -> server")
end)

t.test("message-mode preserves message boundaries", function()
    -- Read on the SERVER end: its read mode stays MESSAGE (the client
    -- end gets reset to byte-stream at connect, npfs STATESUP.C).
    local server, client = pair{
        pipe_type = fs.FILE_PIPE_MESSAGE_TYPE,
        read_mode = fs.FILE_PIPE_MESSAGE_MODE,
    }
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(client, "AA",  2, nil)
    fs.NtWriteFile(client, "BBB", 3, nil)
    -- A 64-byte read still returns exactly one message at a time.
    t.eq(readstr(server, 64), "AA",  "first message framed")
    t.eq(readstr(server, 64), "BBB", "second message framed")
end)

t.test("set_pipe_mode switches the client end to message reads", function()
    local server, client = pair{
        pipe_type = fs.FILE_PIPE_MESSAGE_TYPE,
        read_mode = fs.FILE_PIPE_MESSAGE_MODE,
    }
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.set_pipe_mode(client, { read_mode = fs.FILE_PIPE_MESSAGE_MODE })
    fs.NtWriteFile(server, "one", 3, nil)
    fs.NtWriteFile(server, "two", 3, nil)
    t.eq(readstr(client, 64), "one", "framed read after mode switch")
    t.eq(readstr(client, 64), "two", "second framed read")
end)

-- ------------------------------------------------------------------
-- Peek — must not consume.
-- ------------------------------------------------------------------

t.test("peek reports queued data without consuming it", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.NtWriteFile(server, "peekme", 6, nil)
    local pk = fs.pipe_peek(client, 64)
    t.eq(pk.available, 6,                          "ReadDataAvailable")
    t.eq(pk.data,      "peekme",                   "peeked data")
    t.eq(pk.state,     fs.FILE_PIPE_CONNECTED_STATE, "state")
    -- Still there: a real read returns the same bytes.
    t.eq(readstr(client, 16), "peekme", "peek did not consume")
end)

-- ------------------------------------------------------------------
-- Disconnect — server tears the instance down under the client.
-- ------------------------------------------------------------------

t.test("server disconnect breaks the client end", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    fs.pipe_disconnect(server)
    -- The client's next read fails with STATUS_PIPE_DISCONNECTED.
    local ok, e = pcall(readstr, client, 16)
    t.ok(not ok, "read after disconnect must fail")
    t.ok(tostring(e):match("c00000b0"),
         "expected PIPE_DISCONNECTED, got " .. tostring(e))
end)

-- ------------------------------------------------------------------
-- FSCTL_PIPE_WAIT against the \Device\NamedPipe device root.
-- ------------------------------------------------------------------

t.test("FSCTL_PIPE_WAIT returns for a listening instance", function()
    -- A fresh server with no client connected stays in LISTENING state.
    local leaf, path = fresh()
    local server = fs.create_named_pipe{ name = path, options = SYNC }
    t.defer(function() server:close() end)
    t.ok(fs.pipe_wait(leaf, 1), "wait succeeded for a listening pipe")
end)

t.test("FSCTL_PIPE_WAIT on a missing pipe is OBJECT_NAME_NOT_FOUND", function()
    local ok, e = pcall(fs.pipe_wait, "fnp_does_not_exist", 1)
    t.ok(not ok, "wait on a missing pipe must fail")
    t.ok(tostring(e):match("c0000034"),
         "expected OBJECT_NAME_NOT_FOUND, got " .. tostring(e))
end)

-- ------------------------------------------------------------------
-- Directory enumeration of the \Device\NamedPipe root (npfs DIR.C).
-- ------------------------------------------------------------------

t.test("\\Device\\NamedPipe enumerates open pipe instances", function()
    local leaf, path = fresh()
    local server = fs.create_named_pipe{ name = path, options = SYNC }
    t.defer(function() server:close() end)

    -- Trailing backslash → the root DCB (a directory); "\Device\NamedPipe"
    -- with no slash opens the FS volume object, which isn't enumerable.
    local names = fs.list_dir("\\Device\\NamedPipe\\")
    local seen = false
    for _, n in ipairs(names) do
        if n == leaf then seen = true break end
    end
    t.ok(seen, "our pipe '" .. leaf .. "' appears in the device directory")
end)

-- ------------------------------------------------------------------
-- TRANSCEIVE — write-then-read in one FSCTL. Always pends until the peer
-- replies, so it needs a responder on another thread (nt.thread). The
-- responder ALWAYS writes a reply (even on its own error) so the main
-- thread's transceive can never hang the in-process runner.
-- ------------------------------------------------------------------

local RESPONDER = [[
local ffi    = require('ffi')
local fs     = require('nt.dll.fs')
local handle = require('nt.dll.handle')
-- PAYLOAD = the server handle's integer value (same process => same
-- handle table). Borrow it; the parent retains ownership.
local server = handle.borrow(ffi.cast('HANDLE', tonumber(PAYLOAD)))
local ok, req = pcall(function()
    local buf = ffi.new('char[?]', 64)
    local n = fs.NtReadFile(server, buf, 64, nil)
    return ffi.string(buf, n)
end)
local reply = ok and ("re:" .. req) or "ERR"
pcall(fs.NtWriteFile, server, reply, #reply, nil)
return ok and ("served:" .. req) or ("err: " .. tostring(req))
]]

t.test("transceive writes a request and reads the peer's reply", function()
    -- Full-duplex message pipe; the caller (client) must be in message
    -- read mode with an empty read queue (npfs FSCTRL.C).
    local server, client = pair{
        pipe_type = fs.FILE_PIPE_MESSAGE_TYPE,
        read_mode = fs.FILE_PIPE_MESSAGE_MODE,
    }
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)
    fs.set_pipe_mode(client, { read_mode = fs.FILE_PIPE_MESSAGE_MODE })

    local h_int = tonumber(ffi.cast('intptr_t', handle.raw(server)))
    local th = thread.run(RESPONDER, tostring(h_int))
    t.defer(function() th:close() end)

    local reply = fs.pipe_transceive(client, "ping", 64)
    t.eq(reply, "re:ping", "transceive reply")

    th:wait(2)
    local status, val = th:result()
    t.eq(status, "ok", "responder thread status: " .. tostring(val))
end)

-- Validation paths that return synchronously (no peer, no hang) — they
-- still exercise the transceive entry/validation in FSCTRL.C.

t.test("transceive on a byte-mode pipe is INVALID_READ_MODE", function()
    local server, client = pair()    -- byte stream, byte read mode
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)

    local ok, e = pcall(fs.pipe_transceive, client, "x", 16)
    t.ok(not ok, "transceive on a byte-mode pipe must fail")
    t.ok(tostring(e):match("c00000b4"),
         "expected INVALID_READ_MODE, got " .. tostring(e))
end)

-- ------------------------------------------------------------------
-- Security, volume info, flush — each lights up a file the round-trips
-- never touch (SEINFO.C, VOLINFO.C, FLUSHBUF.C).
-- ------------------------------------------------------------------

t.test("NtQuerySecurityObject returns the pipe's security descriptor", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)
    -- The handle carries READ_CONTROL (part of FILE_GENERIC_READ), so the
    -- DACL/owner query the create-time SD assigned (npfs SEINFO.C).
    local sd = se.get_object_security(server)
    t.ok(sd ~= nil, "got a security descriptor")
end)

t.test("FileFsAttributeInformation reports the FS name NPFS", function()
    local _, path = fresh()
    local server = fs.create_named_pipe{ name = path, options = SYNC }
    t.defer(function() server:close() end)
    local vi = fs.volume_attribute_info(server)
    t.eq(vi.fs_name, "NPFS", "file-system name")
end)

t.test("FileFsDeviceInformation reports the named-pipe device type", function()
    local _, path = fresh()
    local server = fs.create_named_pipe{ name = path, options = SYNC }
    t.defer(function() server:close() end)
    local di = fs.volume_device_info(server)
    t.eq(di.DeviceType, FILE_DEVICE_NAMED_PIPE, "device type")
end)

t.test("flush succeeds once the peer has drained the pipe", function()
    local server, client = pair()
    t.defer(function() server:close() end)
    t.defer(function() client:close() end)
    fs.NtWriteFile(server, "drained", 7, nil)
    t.eq(readstr(client, 16), "drained", "peer read the data")
    -- Write queue is now empty, so flush completes immediately. (The
    -- buffered-but-unread path pends and would block a sync handle, so
    -- it's left for a future threaded test.)
    fs.NtFlushBuffersFile(server)
    t.ok(true, "flush returned without error")
end)
