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
    local ok, ips_or_err = pcall(dns.resolve_all_a,
                                 "example.com", "8.8.8.8", OUTBOUND_TIMEOUT)
    if not ok then
        t.skip("DNS query failed (no outbound network?): " ..
               tostring(ips_or_err))
    end
    local ips = ips_or_err
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
-- by test 3. Skips if test 3 didn't resolve.
-- ------------------------------------------------------------------

t.test("TCP outbound HTTP HEAD to example.com:80", function()
    if not example_ip then
        t.skip("example.com IP not resolved (DNS test failed)")
    end

    diag("tcp socket")
    local s = afd.tcp()
    diag("bind")
    afd.bind(s, "0.0.0.0", 0)
    diag("connect %s:80", example_ip)
    local ok, err_obj = pcall(afd.connect, s, example_ip, 80, OUTBOUND_TIMEOUT)
    if not ok then
        s:close()
        t.skip("connect to example.com:80 failed: " .. tostring(err_obj))
    end

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
