-- ps.create_thread — RtlCreateUserThread via the NT_HANDLE-only
-- contract. The thread entry must be a __stdcall function taking a
-- single HANDLE arg; the Lua-side param is an NT_HANDLE (or nil) and
-- the wrapper does the raw extraction.
--
-- Thread entries here are ntdll exports whose shape already matches
-- (one HANDLE arg, stdcall). We use NtSetHighEventPair as the
-- spawned thread's "done" signaler — it matches the entry ABI
-- directly so no C shim is needed.

local ffi   = require('ffi')
local t     = require('test')
local ntdll = require('nt.dll')
local ps    = require('nt.dll.ps')
local ke    = require('nt.dll.ke')
local ex    = require('nt.dll.ex')

t.suite("thread")

t.test("create_thread spawns a thread and runs entry", function()
    -- Spawned thread signals the high side of an EventPair on entry.
    -- Main thread waits on the high side — if it fires, the thread ran.
    local ep = ex.event_pair()
    local th = ps.create_thread(ntdll.NtSetHighEventPair, ep:handle())
    ep:wait_high()
    ke.NtWaitForSingleObject(th, false, ke.timeout(1.0))
    th:close()
    ep:close()
end)

t.test("create_thread with nil param terminates cleanly", function()
    -- Entry (NtSetHighEventPair) runs with a NULL HANDLE — kernel
    -- returns STATUS_INVALID_HANDLE, thread exits with that status.
    -- We only care the wrapper accepted nil and the thread is joinable.
    local th = ps.create_thread(ntdll.NtSetHighEventPair, nil)
    ke.NtWaitForSingleObject(th, false, ke.timeout(1.0))
    th:close()
end)

t.test("create_thread rejects non-NT_HANDLE param", function()
    t.raises(function()
        ps.create_thread(ntdll.NtSetHighEventPair, 42)
    end, "NT_HANDLE")
    t.raises(function()
        ps.create_thread(ntdll.NtSetHighEventPair, "not a handle")
    end, "NT_HANDLE")
end)

t.test("suspended thread runs to completion after resume", function()
    local ep = ex.event_pair()
    local th = ps.create_thread(ntdll.NtSetHighEventPair,
                                ep:handle(),
                                { suspended = true })
    local prev = ps.NtResumeThread(th)
    t.eq(prev, 1, "thread was suspended once; resume returns 1")
    ep:wait_high()   -- thread's NtSetHighEventPair ran after resume
    ke.NtWaitForSingleObject(th, false, ke.timeout(1.0))
    th:close()
    ep:close()
end)

-- ------------------------------------------------------------------
-- nt.thread.run — Lua-on-its-own-VM threading primitive
-- ------------------------------------------------------------------
--
-- These tests run the chunk in a fresh lua_State on a new OS thread,
-- exercising real cross-thread sync (parent and child contend on a
-- shared kernel object). Same-process handles are usable verbatim in
-- both threads (same handle table); we pass the integer value through
-- PAYLOAD and ffi.cast back on the other side.

local thread = require('nt.thread')
local handle = require('nt.dll.handle')
local ex     = require('nt.dll.ex')

-- Stringify a HANDLE so it can ride through PAYLOAD as decimal text.
local function handle_int(h)
    return tostring(tonumber(ffi.cast('intptr_t', handle.raw(h))))
end

t.test("nt.thread.run: chunk runs and returns a string", function()
    local th = thread.run("return 'hi from child'")
    th:wait()
    local s, v = th:result()
    t.eq(s, "ok")
    t.eq(v, "hi from child")
    th:close()
end)

t.test("nt.thread.run: payload is exposed as PAYLOAD global", function()
    local th = thread.run("return 'got: ' .. PAYLOAD", "the goods")
    th:wait()
    local s, v = th:result()
    t.eq(s, "ok")
    t.eq(v, "got: the goods")
    th:close()
end)

t.test("nt.thread.run: chunk error is reported with status='error'", function()
    local th = thread.run("error('boom from child')")
    th:wait()
    local s, v = th:result()
    t.eq(s, "error")
    t.ok(v:match("boom from child"), "error message preserved, got " .. v)
    th:close()
end)

t.test("nt.thread.run: child waits on parent-signaled event", function()
    -- Parent creates an auto-reset event; child blocks in
    -- NtWaitForSingleObject; parent signals; child returns.
    local e = ke.event{ notify = false }   -- SynchronizationEvent
    local th = thread.run([[
        local ffi = require('ffi')
        local ntdll = require('nt.dll')
        require('nt.dll.ke')                  -- pulls in NtWaitForSingleObject cdef
        local h = ffi.cast('HANDLE', tonumber(PAYLOAD))
        local st = ntdll.NtWaitForSingleObject(h, 0, nil)
        return string.format("st=0x%x",
            st < 0 and st + 0x100000000 or st)
    ]], handle_int(e:handle()))

    -- The child needs a moment to land in NtWaitForSingleObject. We
    -- can't observe that directly, so signal after a short delay; the
    -- event's auto-reset means signal-after-wait and signal-before-wait
    -- both unblock the child correctly.
    ke.NtDelayExecution(false, ke.timeout(0.05))
    e:signal()

    th:wait(2.0)
    local s, v = th:result()
    t.eq(s, "ok", "child returned: " .. v)
    t.eq(v, "st=0x0", "child saw STATUS_SUCCESS")
    th:close()
    e:close()
end)

t.test("nt.thread.run: mutant contention — parent holds, child blocks "
       .. "until release", function()
    local m = ex.mutex{ owned = true }     -- parent owns it
    local th = thread.run([[
        local ffi = require('ffi')
        local ntdll = require('nt.dll')
        require('nt.dll.ke')                  -- pulls in NtWaitForSingleObject cdef
        local h = ffi.cast('HANDLE', tonumber(PAYLOAD))
        local st = ntdll.NtWaitForSingleObject(h, 0, nil)
        if st < 0 then st = st + 0x100000000 end
        return string.format("acquired st=0x%x", st)
    ]], handle_int(m:handle()))

    -- Hold the mutant briefly so the child has a chance to block on
    -- it, then release. The wait result encodes whether the child got
    -- the mutant uncontested (STATUS_SUCCESS = 0) or after waiting
    -- (also 0; STATUS_ABANDONED would indicate we crashed holding it,
    -- which we didn't).
    ke.NtDelayExecution(false, ke.timeout(0.05))
    m:unlock()

    th:wait(2.0)
    local s, v = th:result()
    t.eq(s, "ok", "child returned: " .. v)
    t.eq(v, "acquired st=0x0")
    th:close()
    m:close()
end)

t.test("nt.thread.run: thread that crashes (NtTerminateThread inside "
       .. "chunk) is reported as status='crash'", function()
    -- Simulate an uncaught native crash: chunk takes the thread down
    -- before _entry's exit path can run. status was initialised to
    -- STATUS_CRASH (3) at spawn; _copy_result never overwrites it; the
    -- parent reads "crash". This also exercises the close-side claim
    -- of the dead thread's ref so ctx (and any TLS the chunk leaked)
    -- gets reclaimed instead of leaking until process exit.
    local th = thread.run([[
        local ffi   = require('ffi')
        local ntdll = require('nt.dll')
        require('nt.dll.ps')                  -- NtTerminateThread cdef
        ntdll.NtTerminateThread(ffi.cast('HANDLE', -2), 0xC0000005)
        return "should be unreachable"
    ]])
    th:wait(2.0)
    local s, v = th:result()
    t.eq(s, "crash", "expected crash, got status=" .. s .. " value=" .. v)
    t.eq(v, "", "no result written when thread crashed")
    th:close()
end)

t.test("nt.thread.run: :close() on a still-running thread returns "
       .. "immediately (cooperative cancellation, no terminate)", function()
    -- Spawn a thread that blocks on an event. :close() must NOT call
    -- NtTerminateThread (which would risk leaving loader/heap critical
    -- sections locked forever); it just drops the parent's refcount.
    -- The thread is then "leaked" until it exits naturally — here we
    -- give it the cooperative path by signaling the event afterward.
    local e = ke.event{ notify = false }    -- auto-reset; signal-then-wake
    local th = thread.run([[
        local ffi = require('ffi')
        local ntdll = require('nt.dll')
        require('nt.dll.ke')
        local h = ffi.cast('HANDLE', tonumber(PAYLOAD))
        ntdll.NtWaitForSingleObject(h, 0, nil)
        return "woken"
    ]], handle_int(e:handle()))

    -- Confirm the thread is actually running.
    if th:wait(0.05) then
        local s, v = th:result()
        error("thread finished too early — status=" .. s .. " value=" .. v)
    end

    -- Drop parent's ref; must return immediately. Thread is still
    -- alive somewhere holding the other ref — we have no way to read
    -- its result anymore (we've abandoned the Thread wrapper).
    th:close()

    -- Cooperative cleanup: signal the event so the leaked thread's
    -- wait completes, the chunk returns, _entry calls _release, and
    -- the now-zero-refcount ctx is freed by the thread itself. The
    -- event handle stays alive through the wait via kernel object
    -- ref counting; safe to close after signaling.
    e:signal()
    e:close()
end)
