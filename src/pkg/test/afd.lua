-- nt.net.afd — TCP/UDP sockets, diagnostic-traced order.
--
-- Test order is loopback-first (no network dependency), external
-- second. Each step prints a DIAG line so we can see exactly which
-- call hangs if any test never returns.

local ffi  = require('ffi')
local bit  = require('bit')
local t    = require('test')
local ke   = require('nt.dll.ke')
local afd  = require('nt.net.afd')
local dns  = require('nt.net.dns')

t.suite("afd")

-- Wall-clock seconds via NtQuerySystemTime, anchored at module load.
-- Resolution is HAL clock-tick (~10ms on this build) which is fine
-- for correlating against pcap frame timestamps.
local NT_TICKS_PER_SEC = 1e7
local function nt_now() return tonumber(ke.NtQuerySystemTime().QuadPart) / NT_TICKS_PER_SEC end
local T0 = nt_now()
local function diag(s, ...)
    local msg = (select('#', ...) > 0) and string.format(s, ...) or s
    print(string.format("    [%7.3fs] DIAG: %s", nt_now() - T0, msg))
end

-- Loopback ops complete near-instantly; outbound ops have to clear
-- NAT + return traffic. Both timeouts are generous enough that real
-- delays don't trip them but tight enough that a black-hole socket
-- fails the suite in seconds rather than wedging it forever.
local LOOPBACK_TIMEOUT = 1.0
-- DNS round-trip via SLIRP NAT averages ~1.3s under the vionet
-- polling fallback (50ms ring drain). 5s gives plenty of slack
-- without dragging the suite when a peer is unreachable.
local OUTBOUND_TIMEOUT = 5.0

-- ------------------------------------------------------------------
-- 1. Local UDP loopback — no network dependency
-- ------------------------------------------------------------------

t.test("UDP loopback ping/pong on 127.0.0.1", function()
    diag("server udp")
    local server = afd.udp()
    diag("server bind 127.0.0.1:0")
    afd.bind(server, "127.0.0.1", 0)
    local _, server_port = afd.getsockname(server)
    diag("server bound to port %d", server_port)

    diag("client udp")
    local client = afd.udp()
    diag("client bind 127.0.0.1:0")
    afd.bind(client, "127.0.0.1", 0)
    local _, client_port = afd.getsockname(client)
    diag("client bound to port %d", client_port)

    diag("server connect → client")
    afd.connect(server, "127.0.0.1", client_port)
    diag("client connect → server")
    afd.connect(client, "127.0.0.1", server_port)

    diag("client send 'ping'")
    afd.send(client, "ping", LOOPBACK_TIMEOUT)
    diag("server recv (timeout %gs)", LOOPBACK_TIMEOUT)
    local got1 = afd.recv(server, 64, LOOPBACK_TIMEOUT)
    diag("server got: %s", got1)
    t.eq(got1, "ping")

    diag("server send 'pong'")
    afd.send(server, "pong", LOOPBACK_TIMEOUT)
    diag("client recv (timeout %gs)", LOOPBACK_TIMEOUT)
    local got2 = afd.recv(client, 64, LOOPBACK_TIMEOUT)
    diag("client got: %s", got2)
    t.eq(got2, "pong")

    server:close()
    client:close()
    diag("closed")
end)

-- ------------------------------------------------------------------
-- 2. Local TCP loopback — listener in child Lua VM
-- ------------------------------------------------------------------

t.test("TCP loopback exchange on 127.0.0.1", function()
    diag("setup listener")
    local thread = require('nt.thread')

    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 5)
    local _, listener_port = afd.getsockname(listener)
    diag("listener on port %d", listener_port)

    local handle_int = tostring(tonumber(ffi.cast('intptr_t',
        require('nt.dll.handle').raw(listener))))
    diag("spawning child thread for accept")
    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local listener_h = ffi.cast('HANDLE', tonumber(PAYLOAD))
        local listener  = handle.borrow(listener_h)
        local peer = afd.accept(listener, 2.0)
        afd.send(peer, "hello", 1.0)
        peer:close()
        return "ok"
    ]], handle_int)

    diag("client connect")
    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", listener_port, LOOPBACK_TIMEOUT)
    diag("client recv (timeout %gs)", LOOPBACK_TIMEOUT)
    local got = afd.recv(client, 16, LOOPBACK_TIMEOUT)
    diag("client got: %s", got)
    client:close()

    diag("wait for child")
    th:wait(2.0)
    local s, v = th:result()
    t.eq(s, "ok", "child thread crashed: " .. tostring(v))
    t.eq(got, "hello")

    listener:close()
    th:close()
    diag("cleaned up")
end)

-- ------------------------------------------------------------------
-- 3. External UDP — DNS A-record lookup of example.com via 8.8.8.8.
-- The resolved IP is shared with test 4 below so the TCP test doesn't
-- need a hard-coded literal that rots when example.com's hosting
-- changes (which it has — the famous 93.184.216.34 stopped routing
-- there in 2024).
-- ------------------------------------------------------------------

local example_ip   -- set by test 3, read by test 4

t.test("DNS A-record lookup of example.com via 8.8.8.8", function()
    diag("dns.resolve_all_a('example.com', '8.8.8.8')")
    -- No defensive skip: if outbound UDP doesn't work, that's a
    -- real failure in the network stack and we want to see it.
    local ips = dns.resolve_all_a("example.com", "8.8.8.8",
                                  OUTBOUND_TIMEOUT)
    t.ok(#ips >= 1, "no A records returned")
    for i, ip in ipairs(ips) do
        diag("  A[%d] = %s", i, ip)
        t.ok(ip:match("^%d+%.%d+%.%d+%.%d+$"),
             "expected dotted-quad, got: " .. tostring(ip))
    end
    example_ip = ips[1]
end)

-- ------------------------------------------------------------------
-- 4. External TCP — HEAD against example.com using the IP resolved
-- by test 3.  Test 3 sets example_ip on success; if test 3 failed,
-- this test will fail too via the assertion below — that's the
-- intended cascade.  No defensive skips.
-- ------------------------------------------------------------------

t.test("TCP outbound HTTP HEAD to example.com:80", function()
    t.ok(example_ip,
         "example.com IP not resolved by test 3 — outbound TCP " ..
         "depends on outbound DNS")

    diag("tcp socket")
    local s = afd.tcp()
    diag("bind")
    afd.bind(s, "0.0.0.0", 0)
    diag("connect %s:80", example_ip)
    afd.connect(s, example_ip, 80, OUTBOUND_TIMEOUT)

    diag("send request")
    local req = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n"
    afd.send(s, req, OUTBOUND_TIMEOUT)
    diag("recv response (timeout %gs)", OUTBOUND_TIMEOUT)
    local resp = afd.recv(s, 512, OUTBOUND_TIMEOUT)
    s:close()
    diag("got %d bytes", #resp)

    t.ok(#resp > 0, "no HTTP response received")
    t.ok(resp:match("^HTTP/1%.[01] %d%d%d "),
         "response doesn't look like HTTP: " ..
         resp:sub(1, math.min(80, #resp)))
end)

-- ------------------------------------------------------------------
-- 5. AFD information ioctls — IOCTL_AFD_GET/SET_INFORMATION.
--
-- Covers AFD/MISC.C:AfdGetInformation, AfdSetInformation, and
-- AfdSetInLineMode (which fires only for connected stream endpoints
-- when INLINE_MODE is set).
-- ------------------------------------------------------------------

t.test("GET AFD_MAX_SEND_SIZE is positive on TCP and UDP", function()
    local s = afd.tcp()
    local n_tcp = afd.get_info(s, afd.MAX_SEND_SIZE)
    s:close()
    t.ok(n_tcp > 0, "TCP MaxSendSize should be > 0, got " .. tostring(n_tcp))

    local u = afd.udp()
    local n_udp = afd.get_info(u, afd.MAX_SEND_SIZE)
    u:close()
    t.ok(n_udp > 0, "UDP MaxDatagramSize should be > 0, got " .. tostring(n_udp))
end)

t.test("GET AFD_RECEIVE_WINDOW_SIZE / SEND_WINDOW_SIZE return defaults", function()
    local s = afd.tcp()
    local rwnd = afd.get_info(s, afd.RECEIVE_WINDOW_SIZE)
    local swnd = afd.get_info(s, afd.SEND_WINDOW_SIZE)
    s:close()
    t.ok(rwnd > 0, "default RECEIVE_WINDOW_SIZE should be > 0, got " .. rwnd)
    t.ok(swnd > 0, "default SEND_WINDOW_SIZE should be > 0, got " .. swnd)
end)

t.test("GET AFD_SENDS_PENDING on a fresh endpoint is 0", function()
    -- Fresh TCP endpoint is unconnected; AfdGetInformation returns 0
    -- because endpoint->Type != AfdBlockTypeVcConnecting yet.
    local s = afd.tcp()
    local n = afd.get_info(s, afd.SENDS_PENDING)
    s:close()
    t.eq(n, 0)
end)

t.test("SET AFD_NONBLOCKING_MODE accepts BOOLEAN values", function()
    -- The flag toggles endpoint->NonBlocking; observable behaviour
    -- (immediate STATUS_DEVICE_NOT_READY on starved recv) needs a
    -- wrapper that doesn't auto-wait, which we don't expose yet.
    -- The smoke is: the ioctl path runs cleanly for 0 and 1.
    local s = afd.tcp()
    afd.set_info(s, afd.NONBLOCKING_MODE, 1)
    afd.set_info(s, afd.NONBLOCKING_MODE, 0)
    s:close()
    t.ok(true, "set_info NONBLOCKING_MODE round-tripped")
end)

t.test("SET AFD_INLINE_MODE on connected TCP exercises AfdSetInLineMode", function()
    -- AfdSetInLineMode (MISC.C:879) only runs when the endpoint is in
    -- AfdBlockTypeVcConnecting state, i.e. after the TCP connection
    -- handshake has populated the connection block.  Loopback gives
    -- us that state without any external dependency.
    local server = afd.tcp()
    afd.bind(server, "127.0.0.1", 0)
    afd.listen(server, 1)
    local _, port = afd.getsockname(server)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

    -- Server-side accepted endpoint is the connected one we want.
    local conn = afd.accept(server, LOOPBACK_TIMEOUT)

    -- Toggle INLINE_MODE on the connected endpoint — drops into
    -- AfdSetInLineMode, which issues IOCTL_TDI_SET_EVENT_HANDLER
    -- against the TDI provider to register the inline mode.
    afd.set_info(conn, afd.INLINE_MODE, 1)
    afd.set_info(conn, afd.INLINE_MODE, 0)

    conn:close()
    client:close()
    server:close()
    t.ok(true, "set_info INLINE_MODE on connected TCP round-tripped")
end)

t.test("SET / GET AFD_RECEIVE_WINDOW_SIZE roundtrip on UDP", function()
    -- The window-size SET path requires AfdBlockTypeDatagram or
    -- AfdBlockTypeVcConnecting; a fresh UDP endpoint is the former.
    -- The GET path always returns the global default, so we observe
    -- the SET's success implicitly by not getting an error.
    local u = afd.udp()
    afd.bind(u, "127.0.0.1", 0)
    afd.set_info(u, afd.RECEIVE_WINDOW_SIZE, 32 * 1024)
    afd.set_info(u, afd.SEND_WINDOW_SIZE,    32 * 1024)
    u:close()
    t.ok(true, "window-size sets round-tripped on UDP")
end)

-- ------------------------------------------------------------------
-- 6. IOCTL_AFD_POLL — AFD/POLL.C.
--
-- Covers AfdPoll (the dispatch), AfdTimeoutPoll (DPC that fires when
-- the kernel-side timer expires), and AfdFreePollInfo (the cleanup
-- the IRP-completion path calls in both immediate and timed-out
-- branches).  AfdCancelPoll is exercised by closing a socket while
-- a poll is pending against it — the close cancels the poll IRP.
-- ------------------------------------------------------------------

t.test("POLL: zero-timeout, no events ready, returns empty mask", function()
    -- AfdPoll's "Timeout==0 && no events" path frees the internal
    -- poll info synchronously and completes the IRP with no handles
    -- in the output buffer.  Hits AfdPoll + AfdFreePollInfo without
    -- pending.
    local u = afd.udp()
    afd.bind(u, "127.0.0.1", 0)
    local result = afd.poll({{u, afd.POLL_RECEIVE}}, 0)
    u:close()
    t.eq(result[1], 0, "no events should be ready on a fresh UDP socket")
end)

t.test("POLL: short timeout, no events ready, returns after timeout", function()
    -- Pending-then-timeout path — exercises AfdTimeoutPoll (the DPC
    -- that fires when the kernel-side timer expires).
    local u = afd.udp()
    afd.bind(u, "127.0.0.1", 0)
    local t0 = nt_now()
    local result = afd.poll({{u, afd.POLL_RECEIVE}}, 0.2)
    local elapsed = nt_now() - t0
    u:close()
    t.eq(result[1], 0, "timeout-with-no-events should leave the mask clear")
    -- Lower bound: did the kernel timer actually engage?  Allow some
    -- slop for the HAL clock granularity (10ms ticks under our PIT).
    t.ok(elapsed >= 0.15, "poll returned too early: " .. elapsed .. "s")
    -- Upper bound: just a "didn't hang forever" sanity check; QEMU+TCG
    -- under the agent harness can stack a lot of DPC-drain jitter
    -- before our AfdTimeoutPoll callback runs.
    t.ok(elapsed <  5.0,  "poll returned too late: "  .. elapsed .. "s")
end)

t.test("POLL: UDP socket with already-delivered datagram reports RECEIVE",
       function()
    -- Send a datagram into the socket BEFORE polling, so AFD's
    -- per-endpoint datagram queue is non-empty when AfdPoll's event
    -- check runs.  Hits the AFD_POLL_RECEIVE branch in the immediate-
    -- ready path (POLL.C:367-373).
    local server = afd.udp()
    afd.bind(server, "127.0.0.1", 0)
    local _, sport = afd.getsockname(server)

    local client = afd.udp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", sport)
    afd.send(client, "hello", LOOPBACK_TIMEOUT)

    -- Tiny delay for the datagram to land in server's queue —
    -- vionet's TX/RX is synchronous on loopback, but giving the
    -- kernel one scheduling pass is cheap insurance.
    ke.NtDelayExecution(false, ke.timeout(0.02))

    local result = afd.poll({{server, afd.POLL_RECEIVE}}, 0)
    t.ok(bit.band(result[1], afd.POLL_RECEIVE) ~= 0,
         "POLL_RECEIVE bit should be set, got mask 0x"
         .. bit.tohex(result[1]))

    server:close(); client:close()
end)

t.test("POLL: TCP listener with pending connection reports ACCEPT",
       function()
    -- Same shape, but on the listener side: AFD reports an inbound
    -- connection as either POLL_RECEIVE (legacy) OR POLL_ACCEPT.
    -- Stock POLL.C:388-398 normalises POLL_RECEIVE on a listening
    -- endpoint into POLL_ACCEPT — so passing either bit gives us
    -- the same coverage of the accept branch.
    local server = afd.tcp()
    afd.bind(server, "127.0.0.1", 0)
    afd.listen(server, 1)
    local _, sport = afd.getsockname(server)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", sport, LOOPBACK_TIMEOUT)

    -- Connection lands in the listener's pending-accept queue.
    -- Give scheduler one pass for the TDI Connect indication.
    ke.NtDelayExecution(false, ke.timeout(0.02))

    local result = afd.poll({{server, afd.POLL_ACCEPT}}, 0)
    t.ok(bit.band(result[1], afd.POLL_ACCEPT) ~= 0,
         "POLL_ACCEPT bit should be set, got mask 0x"
         .. bit.tohex(result[1]))

    -- Drain so closing the listener doesn't strand an accepted-but-
    -- unaccepted connection.
    local accepted = afd.accept(server, LOOPBACK_TIMEOUT)
    accepted:close(); client:close(); server:close()
end)

-- ------------------------------------------------------------------
-- 7. IOCTL_AFD_PARTIAL_DISCONNECT — graceful half-close + abortive.
--
-- Covers AFD/DISCONN.C — AfdPartialDisconnect (dispatch + datagram
-- and stream branches), AfdBeginAbort (RST kickoff), AfdRestartAbort
-- (the abort completion routine).
-- ------------------------------------------------------------------

-- Bring up a TCP loopback pair; returns the (server, client) accepted
-- connection endpoints both sides see.  Caller closes both.
local function tcp_pair()
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)
    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    local server = afd.accept(listener, LOOPBACK_TIMEOUT)
    listener:close()
    return server, client
end

t.test("SHUTDOWN: TCP 'send' half-close delivers FIN as EOF",
       function()
    -- Client shuts down its SEND side → AFD issues an orderly FIN
    -- via AfdBeginDisconnect.  Server's next recv returns 0 bytes
    -- (STATUS_END_OF_FILE), which our read_io wrapper surfaces as
    -- an empty string.  We skip the "did the data flow" check —
    -- exercising that on loopback from a single thread is racy
    -- (the existing TCP loopback test uses two threads precisely
    -- because send+recv in the same thread doesn't reliably
    -- complete the TDI scheduling round-trip).  The point of this
    -- test is the half-close codepath, not the data path.
    local server, client = tcp_pair()
    afd.shutdown(client, "send")
    local eof = afd.recv(server, 256, LOOPBACK_TIMEOUT)
    t.eq(eof, "", "expected empty string after FIN, got " ..
                  tostring(#eof) .. " bytes")
    client:close(); server:close()
end)

t.test("SHUTDOWN: TCP 'receive' half-close on a quiescent connection",
       function()
    -- 'receive' with no pending unread data falls through the
    -- AfdPartialDisconnect quiescent-RX branch — just updates
    -- endpoint->DisconnectMode flags, no TDI traffic.
    local server, client = tcp_pair()
    afd.shutdown(client, "receive")
    client:close(); server:close()
    t.ok(true, "shutdown receive completed")
end)

t.test("SHUTDOWN: TCP 'abort' drops the connection with RST",
       function()
    -- Abortive close fires AfdBeginAbort → AfdRestartAbort.  The
    -- peer's reads should fail with a connection-reset class status
    -- (STATUS_CONNECTION_RESET / STATUS_CONNECTION_ABORTED).  Our
    -- recv wrapper raises on those — pcall captures the error.
    local server, client = tcp_pair()
    afd.shutdown(client, "abort")
    local ok, _ = pcall(afd.recv, server, 256, LOOPBACK_TIMEOUT)
    t.ok(not ok, "recv should raise after peer abortive close")
    client:close(); server:close()
end)

t.test("SHUTDOWN: UDP 'send' / 'abort' update endpoint disconnect flags",
       function()
    -- Datagram endpoints don't actually disconnect — AfdPartialDisconnect
    -- just records the flag bits on endpoint->DisconnectMode.  Exercises
    -- the datagram branch (DISCONN.C:94-124).
    local u = afd.udp()
    afd.bind(u, "127.0.0.1", 0)
    afd.shutdown(u, "send")
    afd.shutdown(u, "abort")
    u:close()
    t.ok(true, "UDP shutdown round-tripped")
end)

-- ------------------------------------------------------------------
-- 8. TCP send/receive sequencing — paths that need a real
-- producer/consumer in another thread.
--
-- Covers RECEIVE.C — AfdRestartReceive (the IRP-completion routine
-- that fires when TdiReceive completes asynchronously for a recv
-- that pended because no data was buffered), AfdQueryReceiveInformation
-- — and SEND.C — AfdRestartSend, AfdSendPossibleEventHandler (the
-- TDI indication when peer drains its window).
--
-- All three tests use the same pattern as the existing "TCP loopback
-- exchange" test: cr_thread on the listener side, main thread on
-- the client side.  This is the only reliable way to exercise async
-- send/recv on loopback — single-thread TDI scheduling races
-- non-deterministically as documented in the SHUTDOWN 'send' test.
-- ------------------------------------------------------------------

local thread = require('nt.thread')
local nt_handle = require('nt.dll.handle')

local function listener_handle_int(listener)
    return tostring(tonumber(ffi.cast('intptr_t', nt_handle.raw(listener))))
end

t.test("RECEIVE: recv that pends until peer sends → AfdRestartReceive",
       function()
    -- Server delays before sending → client's recv issues a TdiReceive
    -- that pends in AFD; when the data arrives the TdiReceive
    -- completion fires AfdRestartReceive which completes the IRP.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.borrow(ffi.cast('HANDLE', tonumber(PAYLOAD)))
        local peer = afd.accept(listener, 2.0)
        -- Long enough for the client's recv IRP to definitely pend.
        ke.NtDelayExecution(false, ke.timeout(0.15))
        afd.send(peer, "deferred", 1.0)
        peer:close()
        return "ok"
    ]], listener_handle_int(listener))

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    -- Post recv FIRST, before the child sends.  Pends in AFD until
    -- AfdRestartReceive fires from the TdiReceive completion.
    local got = afd.recv(client, 256, 2.0)
    t.eq(got, "deferred")

    client:close(); listener:close()
    t.ok(th:wait(2.0))
    t.eq(({th:result()})[1], "ok")
    th:close()
end)

t.test("RECEIVE: peer sends before our recv → AfdReceiveEventHandler",
       function()
    -- Mirror of the test above: the child SENDS first, then the
    -- main thread waits past the child's completion before issuing
    -- recv.  Data is delivered to AFD's receive queue via the TDI
    -- receive event handler; main's recv reads from buffer.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local listener = handle.borrow(ffi.cast('HANDLE', tonumber(PAYLOAD)))
        local peer = afd.accept(listener, 2.0)
        afd.send(peer, "early", 1.0)
        peer:close()
        return "ok"
    ]], listener_handle_int(listener))

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

    -- Wait long enough that the child has definitely sent and closed.
    t.ok(th:wait(2.0))
    t.eq(({th:result()})[1], "ok")
    th:close()

    -- Data is sitting in our AFD-side receive buffer; recv returns
    -- immediately from the buffered path (no TdiReceive needed).
    local got = afd.recv(client, 256, LOOPBACK_TIMEOUT)
    t.eq(got, "early")

    client:close(); listener:close()
end)

t.test("SEND: large send pends and completes when peer drains the window",
       function()
    -- Shrink the server's receive window so we don't have to push
    -- a 64KB+ buffer to make the send block.  AFD honours the value
    -- on AfdBlockTypeVcConnecting endpoints (the accepted server
    -- side post-handshake).  The client sends MORE bytes than the
    -- window holds: AfdSend will issue partial TdiSend calls,
    -- pending the IRP when the window fills; AfdSendPossibleEventHandler
    -- fires when the peer drains it, and AfdRestartSend completes
    -- the IRP.
    local SMALL_WIN = 1024
    local PAYLOAD   = string.rep("X", SMALL_WIN * 4)   -- 4× the window

    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.borrow(ffi.cast('HANDLE', tonumber(PAYLOAD)))
        local peer = afd.accept(listener, 2.0)
        -- Shrink the receive window on the accepted (connected) endpoint.
        afd.set_info(peer, afd.RECEIVE_WINDOW_SIZE, 1024)
        -- Let the client's send fill the window (pend) before draining.
        ke.NtDelayExecution(false, ke.timeout(0.15))
        -- Drain everything — each recv frees window, the peer's
        -- AfdSendPossibleEventHandler fires, AfdRestartSend completes
        -- the pended send.
        local total = 0
        while total < 1024 * 4 do
            local chunk = afd.recv(peer, 8192, 2.0)
            if chunk == nil or #chunk == 0 then break end
            total = total + #chunk
        end
        peer:close()
        return tostring(total)
    ]], listener_handle_int(listener))

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    -- Big send; AFD pushes what it can, pends the rest.  Returns
    -- only after the peer has drained enough that everything's
    -- flushed.  2s ceiling for the full handshake.
    afd.send(client, PAYLOAD, 2.0)

    client:close(); listener:close()
    t.ok(th:wait(3.0), "child timed out draining the window")
    local s, v = th:result()
    t.eq(s, "ok", "child errored: " .. tostring(v))
    t.eq(tonumber(v), #PAYLOAD,
         "child drained " .. v .. " bytes, expected " .. #PAYLOAD)
    th:close()
end)

t.test("SHUTDOWN: rejects unknown 'how' values", function()
    local u = afd.udp()
    local ok, err = pcall(afd.shutdown, u, "halfways")
    u:close()
    t.ok(not ok, "shutdown 'halfways' should raise")
    t.ok(tostring(err):match("how must be"),
         "error should mention valid 'how' values, got: " .. tostring(err))
end)

t.test("POLL: multi-handle — one ready, one not, mask reflects both",
       function()
    -- Two UDP sockets, one with a datagram queued, one quiet.
    -- Exercises the per-handle event-check loop with mixed states.
    local hot = afd.udp()
    afd.bind(hot, "127.0.0.1", 0)
    local _, hot_port = afd.getsockname(hot)
    local cold = afd.udp()
    afd.bind(cold, "127.0.0.1", 0)

    local poker = afd.udp()
    afd.bind(poker, "127.0.0.1", 0)
    afd.connect(poker, "127.0.0.1", hot_port)
    afd.send(poker, "ping", LOOPBACK_TIMEOUT)
    ke.NtDelayExecution(false, ke.timeout(0.02))

    local result = afd.poll({
        {hot,  afd.POLL_RECEIVE},
        {cold, afd.POLL_RECEIVE},
    }, 0)

    t.ok(bit.band(result[1], afd.POLL_RECEIVE) ~= 0,
         "hot socket should report RECEIVE, got 0x" .. bit.tohex(result[1]))
    t.eq(result[2], 0,
         "cold socket should report nothing, got 0x" .. bit.tohex(result[2]))

    hot:close(); cold:close(); poker:close()
end)
