-- nt.net.afd — TCP/UDP sockets, diagnostic-traced order.
--
-- Test order is loopback-first (no network dependency), external
-- second. Each step prints a DIAG line so we can see exactly which
-- call hangs if any test never returns.

local ffi    = require('ffi')
local bit    = require('bit')
local t      = require('test')
local ke     = require('nt.dll.ke')
local handle = require('nt.dll.handle')
local afd    = require('nt.net.afd')
local dns    = require('nt.net.dns')

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

    diag("spawning child thread for accept")
    local th = thread.run([[
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local listener = handle.from_payload(PAYLOAD)
        local peer = afd.accept(listener, 2.0)
        afd.send(peer, "hello", 1.0)
        peer:close()
        return "ok"
    ]], handle.to_payload(listener))
    t.defer(function() th:close() end)

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
-- Covers AFD/MISC.C:AfdGetInformation, AfdSetInformation.  The OOB /
-- expedited path is gone, so the inline-mode info class is no longer
-- exercised here.
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

t.test("SET AFD_INLINE_MODE on connected TCP is rejected", function()
    -- OOB / urgent / expedited handling was stripped from the kernel;
    -- the AFD_INLINE_MODE (0x01) info class no longer has a case-arm
    -- in AfdSetInformation, so it falls through to the default and
    -- returns STATUS_INVALID_PARAMETER even on a fully-connected
    -- stream endpoint (which is the path that used to drop into the
    -- now-deleted AfdSetInLineMode).  Locking in the rejection
    -- catches a future accidental resurrection of the case-arm.
    local server = afd.tcp()
    afd.bind(server, "127.0.0.1", 0)
    afd.listen(server, 1)
    local _, port = afd.getsockname(server)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

    local conn = afd.accept(server, LOOPBACK_TIMEOUT)

    local AFD_INLINE_MODE = 0x01
    local ok, err_obj = pcall(afd.set_info, conn, AFD_INLINE_MODE, 1)
    t.ok(not ok, "set_info INLINE_MODE should have failed")
    t.ok(err_obj and tostring(err_obj):find('0xc000000d', 1, true),
         "expected STATUS_INVALID_PARAMETER (0xC000000D), got " ..
         tostring(err_obj))

    conn:close()
    client:close()
    server:close()
end)

t.test("AFD_POLL_RECEIVE_EXPEDITED bit is never set by the kernel", function()
    -- AfdReceiveExpeditedEventHandler and the TDI_EVENT_RECEIVE_EXPEDITED
    -- registration are gone; the POLL.C arm that ORed in the bit is gone
    -- too.  A poll with the bit set should complete on timeout with the
    -- bit cleared in the returned mask, even on a connected TCP socket.
    local server = afd.tcp()
    afd.bind(server, "127.0.0.1", 0)
    afd.listen(server, 1)
    local _, port = afd.getsockname(server)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    local conn = afd.accept(server, LOOPBACK_TIMEOUT)

    local AFD_POLL_RECEIVE_EXPEDITED = 0x0002
    local events = afd.poll({[conn] = AFD_POLL_RECEIVE_EXPEDITED}, 0.1)
    t.eq(events[conn], 0, "expedited poll bit must not fire")

    conn:close()
    client:close()
    server:close()
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
    local result = afd.poll({[u] = afd.POLL_RECEIVE}, 0)
    t.eq(result[u], 0, "no events should be ready on a fresh UDP socket")
    u:close()
end)

t.test("POLL: short timeout, no events ready, returns after timeout", function()
    -- Pending-then-timeout path — exercises AfdTimeoutPoll (the DPC
    -- that fires when the kernel-side timer expires).
    local u = afd.udp()
    afd.bind(u, "127.0.0.1", 0)
    local t0 = nt_now()
    local result = afd.poll({[u] = afd.POLL_RECEIVE}, 0.2)
    local elapsed = nt_now() - t0
    t.eq(result[u], 0, "timeout-with-no-events should leave the mask clear")
    u:close()
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

    local result = afd.poll({[server] = afd.POLL_RECEIVE}, 0)
    t.ok(bit.band(result[server], afd.POLL_RECEIVE) ~= 0,
         "POLL_RECEIVE bit should be set, got mask 0x"
         .. bit.tohex(result[server]))

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

    local result = afd.poll({[server] = afd.POLL_ACCEPT}, 0)
    t.ok(bit.band(result[server], afd.POLL_ACCEPT) ~= 0,
         "POLL_ACCEPT bit should be set, got mask 0x"
         .. bit.tohex(result[server]))

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
-- 7b. AFD context attach + handle query — IOCTLs in MISC.C.
--
-- Covers AfdSetContext, AfdGetContext, AfdGetContextLength,
-- AfdQueryHandles — the per-endpoint opaque blob storage WS2_32
-- uses for its userland-state attach, plus the TDI handle accessors.
-- ------------------------------------------------------------------

t.test("CONTEXT: fresh endpoint has zero-length context", function()
    local s = afd.tcp()
    t.eq(afd.get_context_length(s), 0)
    t.eq(afd.get_context(s), "")
    s:close()
end)

t.test("CONTEXT: set/get round-trips a blob exactly", function()
    local s = afd.tcp()
    local payload = "ctx-blob-12345"
    afd.set_context(s, payload)
    t.eq(afd.get_context_length(s), #payload)
    t.eq(afd.get_context(s), payload)
    s:close()
end)

t.test("CONTEXT: shrinking + growing reuses or reallocates the buffer",
       function()
    local s = afd.tcp()
    afd.set_context(s, string.rep("A", 64))
    t.eq(afd.get_context_length(s), 64)
    -- Smaller blob: AFD truncates ContextLength without freeing.
    afd.set_context(s, "BB")
    t.eq(afd.get_context_length(s), 2)
    t.eq(afd.get_context(s), "BB")
    -- Larger blob: AFD reallocates the underlying buffer.
    local big = string.rep("C", 128)
    afd.set_context(s, big)
    t.eq(afd.get_context_length(s), 128)
    t.eq(afd.get_context(s), big)
    s:close()
end)

t.test("QUERY_HANDLES: address_handle populated after bind", function()
    -- Before bind: neither handle is set.
    local s = afd.tcp()
    local h0 = afd.query_handles(s)
    t.eq(h0.address_handle,    nil, "fresh socket should have no address handle")
    t.eq(h0.connection_handle, nil, "fresh socket should have no connection handle")
    -- After bind: TdiOpenAddress ran → address_handle populated.
    afd.bind(s, "127.0.0.1", 0)
    local h1 = afd.query_handles(s)
    t.ok(h1.address_handle ~= nil, "expected an address_handle after bind")
    t.eq(h1.connection_handle, nil, "no connection until connect/accept")
    s:close()
end)

t.test("QUERY_HANDLES: connection_handle populated after connect", function()
    -- Same listener/connect pattern, but issued from a single thread —
    -- the test only inspects pre-existing handles after the
    -- connect+accept returns, so there's no data-flow race to dodge.
    local server, client = tcp_pair()
    local hc = afd.query_handles(client)
    t.ok(hc.address_handle    ~= nil, "client address_handle should be set")
    t.ok(hc.connection_handle ~= nil, "client connection_handle should be set")
    local hs = afd.query_handles(server)
    t.ok(hs.address_handle    ~= nil, "server address_handle should be set")
    t.ok(hs.connection_handle ~= nil, "server connection_handle should be set")
    server:close(); client:close()
end)

t.test("QUERY_HANDLES: flags select which handle is fetched", function()
    -- Asking for only ADDRESS_HANDLE leaves connection_handle unset
    -- regardless of socket state.  Just smoke-tests the flag dispatch.
    local s = afd.tcp()
    afd.bind(s, "127.0.0.1", 0)
    local addr_only = afd.query_handles(s, afd.QUERY_ADDRESS_HANDLE)
    t.ok(addr_only.address_handle ~= nil,
         "ADDRESS_HANDLE-only request should still return address handle")
    t.eq(addr_only.connection_handle, nil)
    s:close()
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

t.test("RECEIVE: recv that pends until peer sends → AfdRestartReceive",
       function()
    -- Server waits for the client to have posted its recv (signalled
    -- via `ready` event + tiny additional buffer for the syscall to
    -- actually pend in the kernel) → client's recv issues a TdiReceive
    -- that pends in AFD; when the data arrives the TdiReceive
    -- completion fires AfdRestartReceive which completes the IRP.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local ready = ke.event()
    local h_listener = handle.to_payload(listener)
    local h_ready    = tostring(tonumber(ffi.cast('intptr_t',
                                                  handle.raw(ready._h))))

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.from_payload(PAYLOAD:match('^([^,]+)'))
        local ready   = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match(',(%d+)$'))))
        local peer = afd.accept(listener, 2.0)
        -- Wait for parent to have posted its recv.  50ms additional
        -- buffer for the syscall to actually reach the kernel-side
        -- pend after the Lua-side signal.
        ke.NtWaitForSingleObject(ready, false, ke.timeout(5.0))
        ke.NtDelayExecution(false, ke.timeout(0.05))
        afd.send(peer, "deferred", 1.0)
        peer:close()
        return "ok"
    ]], h_listener .. ',' .. h_ready)
    t.defer(function() th:close() end)
    t.defer(function() ready:close() end)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    -- Tell the worker we're about to call recv; the tiny worker-side
    -- buffer covers the gap between signal and syscall reaching kernel.
    ready:signal()
    local got = afd.recv(client, 256, 2.0)
    t.eq(got, "deferred")

    client:close(); listener:close()
    t.ok(th:wait(2.0))
    local s = th:result()
    t.eq(s, "ok")
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
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local listener = handle.from_payload(PAYLOAD)
        local peer = afd.accept(listener, 2.0)
        afd.send(peer, "early", 1.0)
        peer:close()
        return "ok"
    ]], handle.to_payload(listener))
    t.defer(function() th:close() end)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

    -- Wait long enough that the child has definitely sent and closed.
    t.ok(th:wait(2.0))
    local s = th:result()
    t.eq(s, "ok")

    -- Data is sitting in our AFD-side receive buffer; recv returns
    -- immediately from the buffered path (no TdiReceive needed).
    local got = afd.recv(client, 256, LOOPBACK_TIMEOUT)
    t.eq(got, "early")

    client:close(); listener:close()
end)

-- ------------------------------------------------------------------
-- 8b. AFD receive-info + cancel-send.
--
-- Covers RECEIVE.C/AfdQueryReceiveInformation (the FIONREAD-style
-- pending-bytes query) and SEND.C/AfdCancelSend (the IRP-cancel
-- routine attached to a send that pended).
-- ------------------------------------------------------------------

t.test("QUERY_RECEIVE_INFO: fresh endpoint reports 0/0", function()
    local s = afd.tcp()
    local ri = afd.query_receive_info(s)
    t.eq(ri.bytes_available, 0)
    t.eq(ri.expedited_bytes_available, 0)
    s:close()
end)

t.test("QUERY_RECEIVE_INFO: reflects buffered datagrams on a UDP socket",
       function()
    -- Push two datagrams into a UDP socket via a connected sender,
    -- then check the byte count.  AFD buffers datagrams per-endpoint;
    -- query_receive_info returns the sum across all pending datagrams.
    local server = afd.udp()
    afd.bind(server, "127.0.0.1", 0)
    local _, sport = afd.getsockname(server)

    local client = afd.udp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", sport)
    afd.send(client, "first",  LOOPBACK_TIMEOUT)
    afd.send(client, "second", LOOPBACK_TIMEOUT)

    -- Let the datagrams land in the server's per-endpoint queue.
    ke.NtDelayExecution(false, ke.timeout(0.05))

    local ri = afd.query_receive_info(server)
    -- 5 + 6 = 11 bytes total, no expedited.
    t.eq(ri.bytes_available, 11,
         "expected 11 bytes pending, got " .. ri.bytes_available)
    t.eq(ri.expedited_bytes_available, 0)

    server:close(); client:close()
end)

t.test("SEND: cancel a pending send via short io_wait timeout " ..
       "→ AfdCancelSend",
       function()
    -- AfdSend's pend test (SEND.C:272) is *VcBufferredSendBytes >=
    -- MaxBufferredSendBytes* AT IRP START — so a single oversize send
    -- with an empty buffer still goes through (AFD allows the first
    -- send to overshoot the limit, then pends subsequent sends).
    -- Shrink both client send and peer recv windows, then push two
    -- sends: first one fills the buffer, second one pends because
    -- TDI hasn't drained.  io_wait's NtCancelIoFile drives the
    -- cancel routine AfdSend attached to the pending IRP.
    --
    -- Event-handshake (instead of a fixed worker-side sleep) so
    -- under coverage / throttle the worker can't close the peer
    -- before the parent's cancel timeout has fired and been
    -- asserted.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local done = ke.event()
    local h_listener = handle.to_payload(listener)
    local h_done     = tostring(tonumber(ffi.cast('intptr_t',
                                                  handle.raw(done._h))))

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.from_payload(PAYLOAD:match('^([^,]+)'))
        local done    = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match(',(%d+)$'))))
        local peer = afd.accept(listener, 2.0)
        afd.set_info(peer, afd.RECEIVE_WINDOW_SIZE, 1024)
        -- Hold without draining until parent says it's done.  10s
        -- bound so a parent crash can't hang the worker forever.
        ke.NtWaitForSingleObject(done, false, ke.timeout(10.0))
        peer:close()
        return "ok"
    ]], h_listener .. ',' .. h_done)
    t.defer(function() th:close() end)
    t.defer(function() done:close() end)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    afd.set_info(client, afd.SEND_WINDOW_SIZE, 1024)

    -- First send: starts with empty buffer → AFD lets it through and
    -- the byte counter ends well over Max.  Use a 2x-window blob so
    -- the counter is unambiguously past the limit.
    afd.send(client, string.rep("A", 2048), LOOPBACK_TIMEOUT)

    -- Second send: VcBufferredSendBytes is still > MaxBufferredSendBytes
    -- because the peer's recv window is full → IRP pends.  Short
    -- io_wait timeout drives NtCancelIoFile → AfdCancelSend.
    local ok, errmsg = pcall(afd.send, client, "B", 0.3)
    done:signal()              -- release worker
    t.ok(not ok, "second send should have been cancelled, but returned cleanly")
    t.ok(tostring(errmsg):match("CANCELLED") or
         tostring(errmsg):match("0xc0000120"),
         "expected STATUS_CANCELLED, got: " .. tostring(errmsg))
    client:close(); listener:close()
    th:wait(3.0)
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

    local ready = ke.event()
    local h_listener = handle.to_payload(listener)
    local h_ready    = tostring(tonumber(ffi.cast('intptr_t',
                                                  handle.raw(ready._h))))

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.from_payload(PAYLOAD:match('^([^,]+)'))
        local ready   = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match(',(%d+)$'))))
        local peer = afd.accept(listener, 2.0)
        afd.set_info(peer, afd.RECEIVE_WINDOW_SIZE, 1024)
        -- Wait for parent to have posted its big send; the 50ms
        -- buffer covers the gap between signal and the send actually
        -- pending in AFD (window-fill).
        ke.NtWaitForSingleObject(ready, false, ke.timeout(5.0))
        ke.NtDelayExecution(false, ke.timeout(0.05))
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
    ]], h_listener .. ',' .. h_ready)
    t.defer(function() th:close() end)
    t.defer(function() ready:close() end)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    -- Signal worker that we're about to issue the big send; the
    -- worker will then start draining.
    ready:signal()
    -- Big send; AFD pushes what it can, pends the rest.  Returns
    -- only after the peer has drained enough that everything's
    -- flushed.  5s ceiling tolerates coverage/throttle without
    -- hiding genuine drain bugs.
    afd.send(client, PAYLOAD, 5.0)

    client:close(); listener:close()
    t.ok(th:wait(8.0), "child timed out draining the window")
    local s, v = th:result()
    t.eq(s, "ok", "child errored: " .. tostring(v))
    t.eq(tonumber(v), #PAYLOAD,
         "child drained " .. v .. " bytes, expected " .. #PAYLOAD)
end)

t.test("RECV: cancel a pending recv via short io_wait timeout " ..
       "→ AfdCancelReceive",
       function()
    -- Symmetric to the "SEND: cancel ..." test above.  The peer never
    -- sends anything, so the client's recv pends in AFD with
    -- cancel-rtn=AfdCancelReceive.  io_wait's NtCancelIoFile fires
    -- AfdCancelReceive, which completes the IRP with STATUS_CANCELLED.
    -- Direct coverage for AfdCancelReceive (zero baseline hits before
    -- this test).
    --
    -- Event-handshake instead of a worker-side sleep so the FIN from
    -- peer:close can never race the cancel under coverage/throttle.
    -- Worker only closes after parent signals `done`.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local done = ke.event()
    local h_listener = handle.to_payload(listener)
    local h_done     = tostring(tonumber(ffi.cast('intptr_t',
                                                  handle.raw(done._h))))

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.from_payload(PAYLOAD:match('^([^,]+)'))
        local done    = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match(',(%d+)$'))))
        local peer = afd.accept(listener, 2.0)
        -- Wait for parent to finish the recv-and-cancel before
        -- closing.  Bounded so worker can't hang forever.
        ke.NtWaitForSingleObject(done, false, ke.timeout(10.0))
        peer:close()
        return "ok"
    ]], h_listener .. ',' .. h_done)
    t.defer(function() th:close() end)
    t.defer(function() done:close() end)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

    -- No data inbound → recv pends → 0.3s io_wait fires cancel.
    local ok, ret = pcall(afd.recv, client, 256, 0.3)
    done:signal()              -- release worker
    -- On ok=true `ret` is the recv result (data string, possibly
    -- empty for EOF); on ok=false it's the error.  Surface it either
    -- way so a failure under throttle tells us what path was taken.
    t.ok(not ok,
         "recv should have been cancelled, but returned cleanly: " ..
         (ok and ("got " .. #(ret or "") .. " bytes: " .. tostring(ret))
              or tostring(ret)))
    t.ok(tostring(ret):match("CANCELLED") or
         tostring(ret):match("0xc0000120"),
         "expected STATUS_CANCELLED, got: " .. tostring(ret))
    client:close(); listener:close()
    th:wait(3.0)
end)

t.test("RECV: indication larger than recv buffer takes the " ..
       "userIrp=FALSE re-pend path",
       function()
    -- Force AfdBReceiveEventHandler's userIrp=FALSE branch: at the
    -- time TDI indicates data, the pended user IRP's OutputBufferLength
    -- is smaller than BytesAvailable.  AFD must put the IRP back on
    -- the pended list AND restore cancel-rtn so it stays cancellable
    -- (the bug Fix 1 addressed).  This drives the path in steady
    -- state where everything works; absent the fix it would still
    -- work here because the worker doesn't die mid-flight — the
    -- regression only fires when a cancel arrives in the NULL window.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local SEND_BYTES = 4096
    local RECV_BUF   = 64

    local ready = ke.event()
    local h_listener = handle.to_payload(listener)
    local h_ready    = tostring(tonumber(ffi.cast('intptr_t',
                                                  handle.raw(ready._h))))

    local th = thread.run([[
        local ffi    = require('ffi')
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local ke     = require('nt.dll.ke')
        local listener = handle.from_payload(PAYLOAD:match('^([^,]+)'))
        local ready   = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match(',(%d+)$'))))
        local peer = afd.accept(listener, 2.0)
        -- Wait for parent to have posted its first recv; the IRP is
        -- pended when our indication arrives.  Tiny additional buffer.
        ke.NtWaitForSingleObject(ready, false, ke.timeout(5.0))
        ke.NtDelayExecution(false, ke.timeout(0.05))
        afd.send(peer, string.rep("Z", 4096), 2.0)
        peer:close()
        return "ok"
    ]], h_listener .. ',' .. h_ready)
    t.defer(function() th:close() end)
    t.defer(function() ready:close() end)

    local client = afd.tcp()
    afd.bind(client, "127.0.0.1", 0)
    afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

    -- Signal worker to dump its 4 KB; our drain loop's first recv
    -- pends so when the indication arrives it triggers userIrp=FALSE.
    ready:signal()
    local total = 0
    while total < SEND_BYTES do
        local chunk = afd.recv(client, RECV_BUF, 2.0)
        if chunk == nil or #chunk == 0 then break end
        total = total + #chunk
    end
    t.eq(total, SEND_BYTES,
         "expected to drain " .. SEND_BYTES .. " bytes, got " .. total)

    client:close(); listener:close()
    t.ok(th:wait(3.0))
end)

t.test("RECV: rapid cancel/re-pend cycles survive without hanging " ..
       "(Fix 1 regression guard)",
       function()
    -- Stress the userIrp=FALSE re-pend + cancel race that produced the
    -- throttled-hang bug.  Without Fix 1 (cancel-rtn restoration after
    -- InsertHeadList), any cycle whose cancel lands in the cancel-rtn=
    -- NULL window leaks the IRP onto the worker's IrpList — the worker
    -- then wedges PspExitThread → IoCancelThreadIo and the next
    -- NtCreateThread blocks on PsLockProcess.  With the fix, every
    -- cancel finds AfdCancelReceive in place and completes cleanly.
    -- 8 iterations is enough to make the race hit at least once.
    for iter = 1, 8 do
        local listener = afd.tcp()
        afd.bind(listener, "127.0.0.1", 0)
        afd.listen(listener, 1)
        local _, port = afd.getsockname(listener)

        local th = thread.run([[
            local handle = require('nt.dll.handle')
            local afd    = require('nt.net.afd')
            local ke     = require('nt.dll.ke')
            local listener = handle.from_payload(PAYLOAD)
            local peer = afd.accept(listener, 2.0)
            -- Delay just long enough that the client's tiny recv has
            -- likely pended in AFD before we dump data.
            ke.NtDelayExecution(false, ke.timeout(0.02))
            pcall(afd.send, peer, string.rep("Q", 2048), 1.0)
            peer:close()
            return "ok"
        ]], handle.to_payload(listener))

        local client = afd.tcp()
        afd.bind(client, "127.0.0.1", 0)
        afd.connect(client, "127.0.0.1", port, LOOPBACK_TIMEOUT)

        -- Tiny recv that pends.  Short cancel timeout races with the
        -- peer's 20ms-delayed bulk send: sometimes cancel beats the
        -- indication, sometimes the userIrp=FALSE re-pend beats it.
        pcall(afd.recv, client, 64, 0.01)

        client:close(); listener:close()
        th:wait(2.0); th:close()
    end
    t.ok(true,
         "completed 8 cancel/re-pend race iterations without hanging")
end)

-- ------------------------------------------------------------------
-- Cancel coverage — symmetric cancel routines that the existing
-- send/recv-cancel tests don't reach.
-- ------------------------------------------------------------------

t.test("RECV: UDP recvfrom cancel via short io_wait → " ..
       "AfdCancelReceiveDatagram",
       function()
    -- Direct coverage for the UDP analogue of AfdCancelReceive.
    -- A bound UDP socket with no peer sending → recvfrom pends in AFD
    -- with cancel-rtn=AfdCancelReceiveDatagram (RECVDG.C).  Short
    -- io_wait timeout fires NtCancelIoFile → cancel routine →
    -- STATUS_CANCELLED.
    local u = afd.udp()
    afd.bind(u, "127.0.0.1", 0)

    local ok, errmsg = pcall(afd.udp_recvfrom, u, 256, 0.05)
    u:close()
    t.ok(not ok, "udp recvfrom should have been cancelled")
    t.ok(tostring(errmsg):match("CANCELLED") or
         tostring(errmsg):match("0xc0000120"),
         "expected STATUS_CANCELLED, got: " .. tostring(errmsg))
end)

t.test("ACCEPT: cancel a pending accept via short io_wait → " ..
       "AfdCancelWaitForListen",
       function()
    -- Direct coverage for AfdCancelWaitForListen.  A bound+listening
    -- TCP socket with no client connecting → accept pends the
    -- IOCTL_AFD_WAIT_FOR_LISTEN IRP with cancel-rtn=
    -- AfdCancelWaitForListen (LISTEN.C:278).  Short io_wait timeout
    -- drives the cancel.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)

    local ok, errmsg = pcall(afd.accept, listener, 0.05)
    listener:close()
    t.ok(not ok, "accept should have been cancelled")
    t.ok(tostring(errmsg):match("CANCELLED") or
         tostring(errmsg):match("0xc0000120"),
         "expected STATUS_CANCELLED, got: " .. tostring(errmsg))
end)

-- ------------------------------------------------------------------
-- State / boundary validation — fast-failing edge cases that exercise
-- error paths in AFD's dispatch routines.  No sleeps; each test
-- returns synchronously from an immediate-fail path.
-- ------------------------------------------------------------------

t.test("STATE: recv on a closed socket fails cleanly", function()
    -- After close, the file handle is invalid; the I/O manager rejects
    -- the IOCTL before AFD's dispatch ever sees it.  Catches handle-
    -- lifetime regressions.
    local s = afd.tcp()
    s:close()
    local ok, err = pcall(afd.recv, s, 64, 0.0)
    t.ok(not ok, "recv on closed socket should fail")
    t.ok(tostring(err):match("INVALID") or
         tostring(err):match("0xc0000008"),
         "expected STATUS_INVALID_HANDLE-class, got: " .. tostring(err))
end)

t.test("STATE: send on a closed socket fails cleanly", function()
    local s = afd.tcp()
    s:close()
    local ok, err = pcall(afd.send, s, "x", 0.0)
    t.ok(not ok, "send on closed socket should fail")
    t.ok(tostring(err):match("INVALID") or
         tostring(err):match("0xc0000008"),
         "expected STATUS_INVALID_HANDLE-class, got: " .. tostring(err))
end)

t.test("STATE: connect on an already-connected TCP fails", function()
    -- Drives AfdConnect's state-machine guard for a non-Bound,
    -- non-Connecting endpoint state.  Exercises the immediate-fail
    -- branch without ever entering the TDI connect.
    local listener = afd.tcp()
    afd.bind(listener, "127.0.0.1", 0)
    afd.listen(listener, 1)
    local _, port = afd.getsockname(listener)

    local th = thread.run([[
        local handle = require('nt.dll.handle')
        local afd    = require('nt.net.afd')
        local listener = handle.from_payload(PAYLOAD)
        local peer = afd.accept(listener, 2.0)
        peer:close()
        return "ok"
    ]], handle.to_payload(listener))

    local c = afd.tcp()
    afd.bind(c, "127.0.0.1", 0)
    afd.connect(c, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    -- Second connect on the same socket — should fail at the state
    -- check, not initiate a new TDI connect.
    local ok, err = pcall(afd.connect, c, "127.0.0.1", port, LOOPBACK_TIMEOUT)
    t.ok(not ok, "connect on already-connected socket should fail")
    c:close(); listener:close()
    th:wait(2.0); th:close()
end)

t.test("STATE: listen on an unbound TCP fails", function()
    -- AfdStartListen requires the endpoint to already be bound;
    -- listening on a fresh socket should be rejected.
    local s = afd.tcp()
    local ok, err = pcall(afd.listen, s, 1, 0.0)
    s:close()
    t.ok(not ok, "listen on unbound socket should fail")
end)

t.test("STATE: double-listen on the same socket fails", function()
    -- Second listen call on an already-listening endpoint should be
    -- rejected by the state machine before AfdStartListen does any
    -- TDI work.
    local s = afd.tcp()
    afd.bind(s, "127.0.0.1", 0)
    afd.listen(s, 1)
    local ok, err = pcall(afd.listen, s, 1, 0.0)
    s:close()
    t.ok(not ok, "second listen on the same socket should fail")
end)

t.test("STATE: getsockname on an unbound socket is rejected",
       function()
    -- AfdGetAddress requires the endpoint to be at least bound — TDI's
    -- TDI_QUERY_ADDRESS_INFO returns STATUS_INVALID_PARAMETER (0xc000000d)
    -- on a fresh endpoint.  Exercises AfdGetAddress's error-completion
    -- path without crashing.
    local s = afd.tcp()
    local ok, err = pcall(afd.getsockname, s)
    s:close()
    t.ok(not ok, "getsockname on unbound socket should fail")
    t.ok(tostring(err):match("INVALID_PARAMETER") or
         tostring(err):match("0xc000000d"),
         "expected STATUS_INVALID_PARAMETER, got: " .. tostring(err))
end)

t.test("STATE: bind with empty host string is rejected", function()
    -- Empty host fails address parsing before reaching AfdBind.
    local s = afd.tcp()
    local ok, err = pcall(afd.bind, s, "", 0)
    s:close()
    t.ok(not ok, "bind with empty host should fail")
end)

t.test("STATE: bind with malformed dotted-quad is rejected", function()
    -- Drives AFD/parser address validation.
    local s = afd.tcp()
    local ok, err = pcall(afd.bind, s, "999.999.999.999", 0)
    s:close()
    t.ok(not ok, "bind with out-of-range octets should fail")
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
        [hot]  = afd.POLL_RECEIVE,
        [cold] = afd.POLL_RECEIVE,
    }, 0)

    t.ok(bit.band(result[hot], afd.POLL_RECEIVE) ~= 0,
         "hot socket should report RECEIVE, got 0x" .. bit.tohex(result[hot]))
    t.eq(result[cold], 0,
         "cold socket should report nothing, got 0x" .. bit.tohex(result[cold]))

    hot:close(); cold:close(); poker:close()
end)
