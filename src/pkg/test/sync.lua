-- Sync primitives: Event (ke) + Mutex / Semaphore / Timer (ex).
-- Exercises both raw Nt* wrappers and the Lua-idiomatic factory
-- objects with their :method() surface.

local t  = require('test')
local ke = require('nt.dll.ke')
local ex = require('nt.dll.ex')
local ps = require('nt.dll.ps')

t.suite("sync")

-- ------------------------------------------------------------------
-- Event
-- ------------------------------------------------------------------

t.test("event: initial state false, signal() makes wait() return true", function()
    local e = ke.event()
    t.eq(e:wait(0), false, "not signaled yet, immediate wait times out")
    e:signal()
    t.eq(e:wait(0), true,  "signaled now, wait returns true")
    e:close()
end)

t.test("event: notify=false auto-resets after single waiter", function()
    local e = ke.event{ notify = false, signaled = true }
    t.eq(e:wait(0), true,  "consumed the initial signal")
    t.eq(e:wait(0), false, "auto-reset left it unsignaled")
    e:close()
end)

t.test("event: timeout returns false, no error", function()
    local e = ke.event()
    t.eq(e:wait(0.05), false, "0.05s timeout on unsignaled event")
    e:close()
end)

t.test("event: reset() clears a signaled notification event", function()
    local e = ke.event{ signaled = true }
    t.eq(e:wait(0), true)
    -- Wait again — still signaled (NotificationEvent = manual reset).
    t.eq(e:wait(0), true)
    e:reset()
    t.eq(e:wait(0), false)
    e:close()
end)

-- ------------------------------------------------------------------
-- Mutex
-- ------------------------------------------------------------------

t.test("mutex: lock/unlock round-trip", function()
    local m = ex.mutex()
    t.ok(m:lock(0), "uncontended acquire succeeds")
    m:unlock()
    t.ok(m:lock(0), "re-acquire after unlock succeeds")
    m:unlock()
    m:close()
end)

t.test("mutex: owned=true makes creator the initial owner", function()
    local m = ex.mutex{ owned = true }
    local info = m:query()
    t.eq(info.owned_by_caller, true)
    m:unlock()
    m:close()
end)

t.test("mutex: recursive lock by same thread", function()
    local m = ex.mutex{ owned = true }
    t.ok(m:lock(0), "recursive acquire from owning thread succeeds")
    t.ok(m:lock(0), "and again")
    m:unlock(); m:unlock(); m:unlock()
    m:close()
end)

-- ------------------------------------------------------------------
-- Semaphore
-- ------------------------------------------------------------------

t.test("semaphore: initial slots drain then refill", function()
    local s = ex.semaphore{ initial = 2, maximum = 5 }
    t.ok(s:acquire(0), "got slot 1/2")
    t.ok(s:acquire(0), "got slot 2/2")
    t.eq(s:acquire(0.01), false, "no more slots — times out")
    s:release(2)
    t.ok(s:acquire(0), "refilled, acquire 1")
    t.ok(s:acquire(0), "refilled, acquire 2")
    s:close()
end)

t.test("semaphore: query reports current / maximum", function()
    local s = ex.semaphore{ initial = 3, maximum = 10 }
    local info = s:query()
    t.eq(info.current, 3)
    t.eq(info.maximum, 10)
    s:acquire(0); s:acquire(0)
    t.eq(s:query().current, 1)
    s:close()
end)

t.test("semaphore.release returns previous count", function()
    local s = ex.semaphore{ initial = 0, maximum = 5 }
    t.eq(s:release(1), 0, "previous = 0 before first release")
    t.eq(s:release(2), 1, "previous = 1 before 2-slot release")
    s:close()
end)

t.test("semaphore: missing maximum raises", function()
    t.raises(function() ex.semaphore{ initial = 1 } end,
             "maximum is required")
end)

-- ------------------------------------------------------------------
-- Timer
-- ------------------------------------------------------------------

t.test("timer: one-shot fires within the due interval", function()
    local tm = ex.timer{}
    tm:set(0.05)   -- fire in 50ms
    t.ok(tm:wait(0.5), "fired within 500ms timeout")
    tm:close()
end)

t.test("timer: cancel before fire, wait times out", function()
    local tm = ex.timer{}
    tm:set(5.0)    -- 5s from now
    tm:cancel()
    t.eq(tm:wait(0.1), false, "no fire after cancel")
    tm:close()
end)

-- NT 3.5's NtSetTimer is one-shot only — there is no periodic-timer
-- syscall (KeSetTimerEx / a Period argument arrived in NT 4.0), so
-- there is no periodic case to test here.

-- ------------------------------------------------------------------
-- EventPair
-- ------------------------------------------------------------------

t.test("event_pair: signal high then wait high completes immediately", function()
    local ep = ex.event_pair()
    ep:signal_high()
    ep:wait_high()   -- consumes the signal
    ep:close()
end)

t.test("event_pair: signal low then wait low completes immediately", function()
    local ep = ex.event_pair()
    ep:signal_low()
    ep:wait_low()
    ep:close()
end)

t.test("event_pair: two threads rendezvous via set-and-wait atomics", function()
    local ntdll = require('nt.dll')
    local ep = ex.event_pair()
    -- Spawned thread's entry is ntdll.NtSetLowWaitHighEventPair itself.
    -- Its signature — NTSTATUS __stdcall fn(HANDLE) — matches the
    -- thread-entry contract exactly, so we hand the ntdll function
    -- straight to create_thread. The NT_HANDLE param is extracted by
    -- the wrapper.
    local th = ps.create_thread(ntdll.NtSetLowWaitHighEventPair,
                                ep:handle())

    -- Main thread: atomic signal-high + wait-low. Rendezvous with the
    -- spawned thread's signal-low + wait-high.
    ep:signal_high_wait_low()

    ke.NtWaitForSingleObject(th, false, ke.timeout(1.0))
    th:close()
    ep:close()
end)

-- IoCompletion has its own suite — see test/iocp.lua (idiomatic +
-- concurrency) and test/fuzz/iocp.lua (raw-ntdll adversarial cases).

-- Cross-thread Event signaling is covered implicitly: Event signal/wait
-- semantics are tested same-thread above, and cross-thread sync is
-- tested via EventPair's rendezvous. No dedicated test needed.
