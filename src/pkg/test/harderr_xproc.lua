-- nt.harderr -- two-process integration test.
--
-- Parent: register a hard-error port, spawn a child that raises a
-- hard error, receive the LPC, reply, verify the child saw our
-- response in NtRaiseHardError's out-param.
--
-- This is the only way to exercise NtRaiseHardError → daemon
-- delivery end-to-end: HARDERR.C:404-417 forbids the registering
-- process from raising (would bugcheck the kernel via
-- ExpSystemErrorHandler(CallShutdown=TRUE)).  The child runs in a
-- separate NT process and so escapes the guard cleanly.
--
-- Synchronization: a named event lets the main thread wait until the
-- daemon cr_thread has registered the port before spawning the child.
-- Without this, the child can raise before the kernel knows where to
-- LPC, which would halt the box via the no-port branch.
--
-- Wraps: lua.exe (= run.exe, src/cr/run.c) + ps.spawn + ps.wait_exit.
-- The preamble.lua hook in lua_main.c sets package.path on every
-- lua.exe invocation, so the child can require('nt.dll.ex') without
-- any extra environment plumbing.

local ffi    = require('ffi')
local t      = require('test')
local lpc    = require('nt.dll.lpc')
local ex     = require('nt.dll.ex')
local ke     = require('nt.dll.ke')
local oa     = require('nt.dll.oa')
local ps     = require('nt.dll.ps')
local thread = require('nt.thread')
local handle = require('nt.dll.handle')
local harderr = require('nt.harderr')

t.suite("harderr_xproc")

-- Path layout assumes the standard MicroNT staging:
--   \SystemRoot\System32\lua.exe (= run.exe, the LuaJIT interpreter)
-- The child script lives in pkg\test.zip; we drive it via lua.exe's
-- -e flag (`require('test.harderr_raise')`) so the zip searcher can
-- resolve it -- direct file-path invocation would miss the zip.
local LUA_EXE = "\\SystemRoot\\System32\\lua.exe"

local STATUS_TEST_HARDERR = 0xC0000420   -- matches harderr_raise.lua default

-- ------------------------------------------------------------------
-- End-to-end: child raises, parent receives + replies, child exits
-- with the response as its exit code.
-- ------------------------------------------------------------------

local DAEMON_CHUNK = [[
local ffi     = require('ffi')
local ex      = require('nt.dll.ex')
local ke      = require('nt.dll.ke')
local oa      = require('nt.dll.oa')
local harderr = require('nt.harderr')

local port_name, ready_event_name = PAYLOAD:match("^([^\n]*)\n(.*)$")

local ok, result = pcall(function()
    -- 1. Register as the default hard-error port.
    local port = harderr.listen(port_name, { default = true })

    -- 2. Signal the parent that we're ready.  The parent opened the
    --    event by name before spawning us; we open it here (also by
    --    name) and set it.
    local EVENT_ALL_ACCESS = 0x1F0003
    local evt = ex.NtOpenEvent(EVENT_ALL_ACCESS,
                               oa.path(ready_event_name).oa)
    ke.NtSetEvent(evt)
    evt:close()

    -- 3. Receive + reply.
    local msg = port:recv()
    port:reply(msg, harderr.RESPONSE.RETURN_TO_CALLER)
    port:close()

    return string.format("status=%x pid=%d nparams=%d",
                         msg.status, msg.pid, #msg.params)
end)

return ok and ("OK:" .. result) or ("ERR:" .. tostring(result))
]]

t.test("child raises -> daemon receives + replies -> child observes", function()
    local PORT_NAME  = "\\MicroNTHardErrXProc"
    local READY_NAME = "\\MicroNTHardErrReady"

    -- Create the "daemon-ready" event on the parent's main thread
    -- BEFORE spawning the daemon thread.  The daemon thread opens it
    -- by name and signals it once NtSetDefaultHardErrorPort has
    -- returned -- ensuring no child can raise before the kernel knows
    -- where to LPC.
    local noa_ready = oa.path(READY_NAME)
    local ready_evt = ke.NtCreateEvent(0x1F0003 --[[ EVENT_ALL_ACCESS ]],
                                       noa_ready.oa,
                                       0 --[[ NotificationEvent ]],
                                       false --[[ not initially signaled ]])

    -- Spawn the daemon cr_thread.
    local daemon = thread.run(DAEMON_CHUNK,
                              PORT_NAME .. "\n" .. READY_NAME)

    -- Wait for the daemon to signal ready.  Bounded timeout so a
    -- broken daemon shows up as a clean FAIL rather than hanging
    -- the selftest.
    local st_ready = ke.NtWaitForSingleObject(ready_evt, false, ke.timeout(8.0))
    t.eq(st_ready, 0, "daemon signaled ready within 8s")
    ready_evt:close()

    -- Spawn the child process.  Inherits stdin/out/err so any errors
    -- it prints surface in the boot log.  -e drives the child chunk
    -- through the zip searcher (test files live in pkg\test.zip --
    -- direct file path wouldn't find harderr_raise inside the zip).
    -- The preamble runs first on every lua.exe invocation, setting
    -- package.path + zip searcher, so require('test.harderr_raise')
    -- resolves cleanly inside the child.
    local proc = ps.spawn{
        exe     = LUA_EXE,
        cmdline = "\"lua.exe\" -e \"require('test.harderr_raise')\"",
    }
    local child_exit = ps.wait_exit(proc)

    -- Watchdog the daemon thread.  By the time the child has exited,
    -- the daemon must have received + replied, so the join should
    -- complete promptly.  8 seconds is generous.
    local daemon_done = daemon:wait(8.0)
    t.ok(daemon_done, "daemon cr_thread finished -- no deadlock")

    -- Validate the child saw RESPONSE.RETURN_TO_CALLER (= 0).
    t.eq(child_exit, ex.HARDERROR_RESPONSE.RETURN_TO_CALLER,
         "child's NtRaiseHardError returned RETURN_TO_CALLER")

    -- Validate the daemon decoded the message correctly.
    if daemon_done then
        local status, value = daemon:result()
        t.eq(status, "ok", "daemon thread ran cleanly")
        local payload = tostring(value):match("^OK:(.*)$")
        t.ok(payload, "daemon: " .. tostring(value))
        if payload then
            local got_status = payload:match("status=(%x+)")
            t.eq(tonumber(got_status, 16), STATUS_TEST_HARDERR,
                 "daemon saw the status the child raised")
            local got_pid = tonumber(payload:match("pid=(%d+)"))
            t.eq(got_pid, tonumber(ffi.cast('uintptr_t', proc.pid)),
                 "daemon saw the child's PID")
        end
    end

    daemon:close()
end)
