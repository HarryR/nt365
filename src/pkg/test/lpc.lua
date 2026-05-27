-- nt.dll.lpc — struct layout and message-buffer helpers.

local ffi    = require('ffi')
local t      = require('test')
local lpc    = require('nt.dll.lpc')
local oa     = require('nt.dll.oa')
local thread = require('nt.thread')
local handle = require('nt.dll.handle')

t.suite("lpc")

t.test("PORT_MESSAGE matches NT 3.5 layout (24 bytes)", function()
    t.eq(ffi.sizeof('PORT_MESSAGE'), 24)
end)

t.test("PORT_VIEW / REMOTE_PORT_VIEW / PORT_DATA_ENTRY sizes", function()
    t.eq(ffi.sizeof('PORT_VIEW'),        24)
    t.eq(ffi.sizeof('REMOTE_PORT_VIEW'), 12)
    t.eq(ffi.sizeof('PORT_DATA_ENTRY'),  8)
end)

t.test("new_message allocates fused header + payload", function()
    local msg = lpc.new_message(100)
    -- Struct should expose both hdr and data fields.
    t.ok(msg.hdr ~= nil)
    t.ok(msg.data ~= nil)
    -- Payload is writable and reads back.
    msg.data[0] = 0x41
    msg.data[99] = 0x42
    t.eq(msg.data[0], 0x41)
    t.eq(msg.data[99], 0x42)
end)

t.test("init_message stamps DataLength/TotalLength/Type", function()
    local msg = lpc.new_message(64)
    lpc.init_message(msg, 64, lpc.LPC_REQUEST)
    t.eq(msg.hdr.u1.s1.DataLength,  64)
    t.eq(msg.hdr.u1.s1.TotalLength, 64 + lpc.PORT_HEADER_SIZE)
    t.eq(msg.hdr.u2.s2.Type,        lpc.LPC_REQUEST)
    t.eq(msg.hdr.MessageId,         0)
end)

t.test("init_message defaults type to LPC_REQUEST", function()
    local msg = lpc.new_message(16)
    lpc.init_message(msg, 16)
    t.eq(msg.hdr.u2.s2.Type, lpc.LPC_REQUEST)
end)

t.test("default_qos produces valid SECURITY_QUALITY_OF_SERVICE", function()
    local qos = lpc.default_qos()
    t.eq(qos.Length, ffi.sizeof('SECURITY_QUALITY_OF_SERVICE'))
    t.eq(qos.ImpersonationLevel, lpc.SecurityImpersonation)
end)

-- ------------------------------------------------------------------
-- Live round-trip — a real client/server LPC exchange.
--
-- LPC is a chain of blocking handoffs: each side blocks and only the
-- peer's progress unblocks it (a baton pass on the uniprocessor
-- scheduler). NT 3.5's LPC waits have NO timeout, so a sequencing bug
-- hangs the calling thread forever. Defence: both parties run in
-- cr_threads and the main thread makes no LPC call at all — it only
-- bounded-waits on the two workers, so a deadlock surfaces as a clean
-- FAIL instead of a silent hang of the whole selftest.
-- ------------------------------------------------------------------

-- Server worker. PAYLOAD = "<connection-port handle int>\n<reply text>".
-- cr_thread marshals only strings, and the lpc.Nt* wrappers raise a
-- structured-error *table* — so the chunk pcalls its work and returns
-- "OK:<payload>" or "ERR:<stringified error>" rather than letting a
-- non-string error escape (which cr_thread reports as an empty "error").
local SERVER_CHUNK = [[
-- package.path/searcher/io/os set by the runtime preamble (sibling
-- states run the wrapped luaL_openlibs too).
local ffi    = require('ffi')
local lpc    = require('nt.dll.lpc')
local handle = require('nt.dll.handle')
local ke     = require('nt.dll.ke')

local porth, reply_text = PAYLOAD:match("^([^\n]*)\n(.*)$")

local ok, result = pcall(function()
    local conn = handle.borrow(ffi.cast('HANDLE', tonumber(porth)))

    -- 6s watchdog < the 8s join timeout in the test main thread.
    -- NtReplyWaitReceivePortEx raises STATUS_TIMEOUT if no message
    -- arrives in time, which pcall catches — no deadlock.
    local tmo = ke.timeout(6.0)

    -- 1. Block for a connection request (NtListenPort loop with timeout).
    local connreq
    repeat
        connreq = lpc.new_message()
        lpc.NtReplyWaitReceivePortEx(conn, nil, nil, connreq, tmo)
    until connreq.hdr.u2.s2.Type == lpc.LPC_CONNECTION_REQUEST

    -- 2. Accept it -> per-client communication port; wake the client.
    local commport = lpc.NtAcceptConnectPort(conn, nil, connreq, true, nil, nil)
    lpc.NtCompleteConnectPort(commport)

    -- 3. Block for the client's request (received on the connection
    --    port — the server's single receive funnel).
    local recv = lpc.new_message()
    lpc.NtReplyWaitReceivePortEx(conn, nil, nil, recv, tmo)
    local got = ffi.string(recv.data, recv.hdr.u1.s1.DataLength)

    -- 4. Reply by reusing the received message buffer: the kernel
    --    already stamped its MessageId + ClientId, which identify the
    --    waiting client. Overwrite the payload, re-stamp length + type.
    ffi.copy(recv.data, reply_text, #reply_text)
    recv.hdr.u1.s1.DataLength  = #reply_text
    recv.hdr.u1.s1.TotalLength = lpc.PORT_HEADER_SIZE + #reply_text
    recv.hdr.u2.s2.Type        = lpc.LPC_REPLY
    lpc.NtReplyPort(commport, recv)

    commport:close()
    return got
end)

return ok and ("OK:" .. result) or ("ERR:" .. tostring(result))
]]

-- Client worker. PAYLOAD = "<connection-port name>\n<request text>".
-- Same OK:/ERR: string-marshalling discipline as the server chunk.
local CLIENT_CHUNK = [[
-- package.path/searcher/io/os set by the runtime preamble (sibling
-- states run the wrapped luaL_openlibs too).
local ffi = require('ffi')
local lpc = require('nt.dll.lpc')

local name, request_text = PAYLOAD:match("^([^\n]*)\n(.*)$")

local ok, result = pcall(function()
    -- Connect to the named port (blocks through the connect handshake).
    local cport = lpc.NtConnectPort(name)

    -- Send the request, block for the reply.
    local req = lpc.new_message(#request_text)
    ffi.copy(req.data, request_text, #request_text)
    -- Type 0 (a fresh message). NtRequestWaitReplyPort reads
    -- Type == LPC_REQUEST as "this is a callback" (LPCSEND.C:674); a
    -- normal client request must be Type 0 — the kernel itself stamps
    -- LPC_REQUEST. (lpc.init_message's LPC_REQUEST default is wrong for
    -- this call; passing 0 explicitly overrides it.)
    lpc.init_message(req, #request_text, 0)

    local reply = lpc.new_message()
    lpc.NtRequestWaitReplyPort(cport, req, reply)
    local got = ffi.string(reply.data, reply.hdr.u1.s1.DataLength)

    cport:close()
    return got
end)

return ok and ("OK:" .. result) or ("ERR:" .. tostring(result))
]]

t.test("live LPC round-trip", function()
    local PORT_NAME = "\\MicroNTLpcRT"
    local REQUEST   = "lpc-round-trip-request"
    local REPLY     = "lpc-round-trip-reply"

    -- The connection port must exist before the client connects, so the
    -- main thread creates it — a non-blocking call. After that the main
    -- thread issues NO LPC call (NT 3.5 LPC waits cannot time out).
    local noa  = oa.path(PORT_NAME)
    local port = lpc.NtCreatePort(noa.oa)
    local port_int = tonumber(ffi.cast('uintptr_t', handle.raw(port)))

    local srv = thread.run(SERVER_CHUNK, port_int .. "\n" .. REPLY)
    local cli = thread.run(CLIENT_CHUNK, PORT_NAME .. "\n" .. REQUEST)

    -- Watchdog: bounded joins only. A deadlock (either side waiting on
    -- a message that never comes) shows up as wait()==false.
    local srv_done = srv:wait(8.0)
    local cli_done = cli:wait(8.0)
    t.ok(srv_done, "server thread finished — no deadlock")
    t.ok(cli_done, "client thread finished — no deadlock")

    -- Each chunk returns "OK:<payload>" or "ERR:<stringified error>".
    if srv_done then
        local status, value = srv:result()
        t.eq(status, "ok", "server thread ran cleanly (no crash)")
        local payload = tostring(value):match("^OK:(.*)$")
        t.ok(payload, "server: " .. tostring(value))
        if payload then
            t.eq(payload, REQUEST, "server received the request payload")
        end
    end
    if cli_done then
        local status, value = cli:result()
        t.eq(status, "ok", "client thread ran cleanly (no crash)")
        local payload = tostring(value):match("^OK:(.*)$")
        t.ok(payload, "client: " .. tostring(value))
        if payload then
            t.eq(payload, REPLY, "client received the reply payload")
        end
    end

    srv:close()
    cli:close()
    port:close()
end)
