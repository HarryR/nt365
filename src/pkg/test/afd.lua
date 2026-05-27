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
