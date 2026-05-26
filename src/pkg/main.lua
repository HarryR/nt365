-- main.lua — MicroNT connect-back agent (fast experiment loop).
--
-- Replaces the namespace-walk init (backed up at stuff/main-backup.lua).
-- Purpose: boot the guest ONCE, then drive it over the network — upload
-- and run arbitrary programs — without baking a disk layer per test or
-- rebooting.  Uploads land on the existing writable split-ntfs volume.
--
-- Model: bring the net up (DHCP), then loop — connect back to the host
-- (the slirp gateway learned from the DHCP lease) on a fixed port, and
-- for each connection read a length-prefixed Lua chunk and execute it
-- with the socket as its argument.  The chunk implements whatever
-- protocol it wants (receive a file, spawn a binary, stream output back),
-- so the harness is iterated over the wire and the image never rebuilds
-- for it.  Base protocol on the wire: u32-LE length, then that many bytes
-- of Lua source.  An immediate close (no length) is a valid "did it
-- connect?" probe.
--
-- package.path + io/os globals come from the runtime preamble
-- (\SystemRoot\System32\preamble.lua).

local dhcp   = require('nt.net.dhcp')
local afd    = require('nt.net.afd')
local ke     = require('nt.dll.ke')
local thread = require('nt.thread')  -- spawns the harderr daemon below

-- HOST defaults to the DHCP-learned default gateway, which under QEMU
-- user-mode networking is the slirp router (10.0.2.2) and reaches a
-- listener on the host's loopback.  PORT is the host-side stub's port.
local PORT          = 4444
local CONNECT_WAIT  = 3      -- per-attempt connect timeout (s)
local RETRY_WAIT    = 3      -- pause between failed connect attempts (s)
local CHUNK_TIMEOUT = 30     -- read timeout while pulling a chunk (s)
local MAX_CHUNK     = 64 * 1024 * 1024

local function sleep(secs)
    ke.NtDelayExecution(false, ke.timeout(secs))
end

-- Read exactly n bytes.  TCP is a stream, so recv may return short;
-- loop until satisfied.  Returns the string, or nil on EOF before n.
-- afd.recv raises on timeout / hard error — the caller pcalls the
-- whole exchange.
local function recv_exact(sock, n)
    local parts, got = {}, 0
    while got < n do
        local chunk = afd.recv(sock, n - got, CHUNK_TIMEOUT)
        if #chunk == 0 then return nil end          -- peer closed
        parts[#parts + 1] = chunk
        got = got + #chunk
    end
    return table.concat(parts)
end

-- One control connection: u32-LE length, then that many bytes of Lua
-- source; compile and run with the socket.  The chunk owns the protocol
-- from here on.
local function serve(sock)
    local hdr = recv_exact(sock, 4)
    if not hdr then
        print("AGENT: peer closed with no chunk")
        return
    end
    local b1, b2, b3, b4 = string.byte(hdr, 1, 4)
    local len = b1 + (b2 * 256) + (b3 * 65536) + (b4 * 16777216)
    if len == 0 or len > MAX_CHUNK then
        print(string.format("AGENT: bad chunk length %d", len))
        return
    end
    local code = recv_exact(sock, len)
    if not code then
        print("AGENT: short chunk (peer closed mid-transfer)")
        return
    end
    local chunk, cerr = load(code, "=remote")
    if not chunk then
        print("AGENT: compile error: " .. tostring(cerr))
        pcall(afd.send, sock, "COMPILE-ERR " .. tostring(cerr))
        return
    end
    local ok, res = pcall(chunk, sock)
    if not ok then
        print("AGENT: chunk error: " .. tostring(res))
        pcall(afd.send, sock, "ERR " .. tostring(res))
    end
end

print("AGENT: boot")

-- Boot prelude — \NLS\ sections, \DosDevices\C: symlink, etc.  Idempotent.
require('nt.boot').run()

-- ------------------------------------------------------------------
-- Hard-error port daemon.
--
-- Without a registered default hard-error port, any user-mode raise of
-- an NT_ERROR (typical example: LoadLibrary on a missing static dep)
-- halts the kernel via ExpSystemErrorHandler(CallShutdown=TRUE).  See
-- docs-wip/HALT-ON-USER-ERROR.md.  Registering ourselves here means
-- the kernel LPCs us instead -- we log + reply RTC, the raising
-- process gets STATUS_DLL_NOT_FOUND (etc.) back as a normal error,
-- everyone's happy.
--
-- The daemon runs in a sibling cr_thread so the main accept-and-run
-- loop below isn't blocked on recv().  Both threads share this NT
-- process: that means HARDERR.C:404-417's recursion guard fires if
-- WE raise a hard error -- the kernel would bugcheck the box rather
-- than LPC back to us.  In practice:
--
--   * Hard errors come from CHILD processes (ps.spawn'd binaries like
--     python.exe, lua.exe with a missing DLL, etc.).  Those are fine
--     -- the guard tests the calling process, not ours.
--   * Arriving Lua chunks (line `pcall(chunk, sock)` below) run
--     in-process.  Chunks that need to exercise possibly-failing
--     LoadLibrary / similar MUST do it via ps.spawn so the failure
--     hits a child, not us.  This is already the agenthost.py /
--     stager convention.
--
-- The daemon listens forever.  It survives any single recv hiccup by
-- looping the pcall; only a port-closed status breaks out.
-- ------------------------------------------------------------------

local HARDERR_DAEMON = [[
local harderr = require('nt.harderr')
local bit     = require('bit')

local port = harderr.listen("\\HardErrorPort", { default = true })
print("HARDERR: daemon listening on \\HardErrorPort")

while true do
    local ok, msg = pcall(function() return port:recv() end)
    if not ok then
        print("HARDERR: recv failed: " .. tostring(msg))
        break
    end

    -- Log a single-line summary.  msg.params already decoded -- string
    -- entries for slots flagged in UnicodeStringParameterMask, numbers
    -- for the rest.  Status as 8-digit hex matches kernel STOP-banner
    -- formatting.
    local parts = {
        string.format("HARDERR: pid=%d tid=%d status=%08x",
                      msg.pid, msg.tid, msg.status),
    }
    for i, p in ipairs(msg.params) do
        if type(p) == 'string' then
            parts[#parts + 1] = string.format("p%d=%q", i, p)
        else
            parts[#parts + 1] = string.format("p%d=0x%08x", i, p)
        end
    end
    print(table.concat(parts, " "))

    -- ResponseReturnToCaller = let the raising process handle the
    -- failure normally.  Python's `try: import _ssl except
    -- ImportError` and similar fallbacks all depend on this answer.
    port:reply(msg, harderr.RESPONSE.RETURN_TO_CALLER)
end

port:close()
return "harderr daemon exited"
]]

local harderr_thread = thread.run(HARDERR_DAEMON, "")
print("AGENT: harderr daemon thread up")

-- Bring the interface up.  Under the QEMU dev setup slirp serves DHCP
-- (gateway 10.0.2.2).  Retry a few times before giving up.
local lease
for attempt = 1, 12 do
    local ok, res = pcall(dhcp.acquire, { timeout = 5 })
    if ok then lease = res; break end
    print(string.format("AGENT: dhcp attempt %d/12 failed: %s",
        attempt, tostring(res)))
end
if not lease then
    print("AGENT: FATAL — no DHCP lease (is the NIC up / harness running?)")
    while true do end
end

local host = lease.gateway_str or "10.0.2.2"
print(string.format("AGENT: dhcp ok ip=%s gw=%s — dialling %s:%d",
    lease.address_str, tostring(lease.gateway_str), host, PORT))

-- Connect-back loop.  Each successful connection is one chunk slot.
-- MicroNT's TCP won't SYN from an endpoint with no local address, so we
-- bind an ephemeral local port (0.0.0.0:0) before connect.  Failures
-- print their reason once per distinct error (not every retry) so a
-- broken connect is visible without spamming while the host listener is
-- simply not up yet.
local n = 0
local last_err
while true do
    local sock = afd.tcp()
    local ok, cerr = pcall(function()
        afd.bind(sock, "0.0.0.0", 0)
        afd.connect(sock, host, PORT, CONNECT_WAIT)
    end)
    if ok then
        n = n + 1
        print(string.format("AGENT: connection #%d to %s:%d", n, host, PORT))
        pcall(afd.send, sock, "MicroNT agent ready\n")
        pcall(serve, sock)
        pcall(function() sock:close() end)
    else
        local msg = tostring(cerr)
        if msg ~= last_err then
            print("AGENT: connect failed: " .. msg)
            last_err = msg
        end
        pcall(function() sock:close() end)
        sleep(RETRY_WAIT)
    end
end
