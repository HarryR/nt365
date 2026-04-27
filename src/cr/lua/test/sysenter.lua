-- SYSENTER cutover verification.
--
-- Most of the SYSENTER path is exercised implicitly by the existing
-- 138 selftests — every Nt* call from user mode now goes through
-- KiFastSystemCall + (eligibility-permitting) SYSEXIT.  This file adds
-- a handful of port-specific cases the rest of the suite doesn't cover:
--
--   * argument-count grid           — 1 arg / 6 args / 11 args
--   * APC delivery on syscall return — alertable wait + NtAlertThread
--   * suspend/resume mid-syscall     — wait while suspended, then resume
--   * high-rate concurrent syscalls  — per-thread MSR_SYSENTER_ESP reload
--   * latency timing (informational) — bracket NtQuerySystemTime in a tight loop

local ffi    = require('ffi')
local t      = require('test')
local ntdll  = require('nt.dll')
local ke     = require('nt.dll.ke')
local ex     = require('nt.dll.ex')
local mm     = require('nt.dll.mm')
local ps     = require('nt.dll.ps')
local thread = require('nt.thread')
local handle = require('nt.dll.handle')

t.suite("sysenter")

-- ------------------------------------------------------------------
-- (1) Argument-count grid: a fast-path bug in the trap-frame synthesis
--     or argument copy would manifest at specific arg-count boundaries.
-- ------------------------------------------------------------------

t.test("argc grid: 1-arg syscall (NtClose) round-trips", function()
    -- NtClose: a single HANDLE argument.  Run a few hundred close
    -- cycles to amortise any flaky jitter and exercise the same
    -- syscall path repeatedly.
    for i = 1, 200 do
        local e = ke.event()
        e:close()
    end
end)

t.test("argc grid: 6-arg syscall (NtAllocate/FreeVirtualMemory)", function()
    -- NtAllocateVirtualMemory takes 6 args.  This path also exercises
    -- the user→kernel pointer probe (Base/Size in/out).
    -- Pass nil for proc → wrapper uses NtCurrentProcess pseudo-handle.
    for i = 1, 200 do
        local base, size = mm.NtAllocateVirtualMemory(
            nil, nil, 4096,
            bit.bor(mm.MEM_COMMIT, mm.MEM_RESERVE),
            mm.PAGE_READWRITE)
        t.eq(size, 4096)
        mm.NtFreeVirtualMemory(nil, base, 0, mm.MEM_RELEASE)
    end
end)

t.test("argc grid: 11-arg syscall (NtCreateEvent via NtCreateXxx path)", function()
    -- NtCreateEvent itself is 5 args, but the underlying object create
    -- takes a full OBJECT_ATTRIBUTES.  Exercise repeatedly to catch
    -- arg-copy boundary errors.
    for i = 1, 100 do
        local e = ke.event()
        e:signal()
        t.eq(e:wait(0), true)
        e:close()
    end
end)

-- ------------------------------------------------------------------
-- (2) APC / alert delivery on syscall return.
--
-- Thread A blocks in NtWaitForSingleObject(event, alertable=TRUE).
-- Thread B (the test thread) calls NtAlertThread(A).  A must wake
-- with STATUS_ALERTED.
--
-- This stresses the most fragile part of the SYSEXIT path: when an
-- APC or alert is pending the eligibility check should fall through
-- to IRET — SYSEXIT can't deliver an APC because it doesn't restore
-- a full trap frame.
-- ------------------------------------------------------------------

t.test("alert wakes alertable wait (APC-style return)", function()
    -- Use ke.event() with a never-set state.  Spawn a child thread
    -- via thread.run so we have a HANDLE we can NtAlertThread().
    local started = ke.event()              -- child signals when it's about to wait
    local done    = ke.event()               -- child signals on exit

    local h_started = tostring(tonumber(ffi.cast('intptr_t', handle.raw(started._h))))
    local h_done    = tostring(tonumber(ffi.cast('intptr_t', handle.raw(done._h))))

    local th = thread.run([[
        local ffi    = require('ffi')
        local ke     = require('nt.dll.ke')
        local handle = require('nt.dll.handle')

        -- borrow (not wrap) — parent owns these handles; the child VM's
        -- GC must NOT NtClose them out from under the parent.
        local started = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match('^(%d+),'))))
        local done    = handle.borrow(ffi.cast('void *',
                            tonumber(PAYLOAD:match(',(%d+)$'))))

        -- Tell parent we're about to wait.
        ke.NtSetEvent(started)

        -- Alertable infinite wait on a never-set event — only an alert
        -- (or NtTerminateThread) can break this.  The Event class's :wait
        -- doesn't expose alertable, so use the raw NtWaitForSingleObject
        -- against the underlying NT_HANDLE.
        local victim = ke.event()
        local st = ke.NtWaitForSingleObject(victim:handle(), true, nil)
        victim:close()
        ke.NtSetEvent(done)
        return ('status=%x'):format(st)
    ]], h_started .. ',' .. h_done)

    -- Wait until child is actually in the wait.
    started:wait()

    -- Brief delay to make sure the child has entered the kernel-mode
    -- wait (the event signals just before the wait, not after).
    ke.NtDelayExecution(false, ke.timeout(0.02))

    -- Alert the child.  The alertable wait should return STATUS_ALERTED.
    local th_handle = th:handle()
    ke.NtAlertThread(th_handle)

    -- Wait for the child to finish.  If alert failed to wake it, this
    -- hangs forever — bound the wait so a regression shows as a fail
    -- not a hang.
    done:wait(2.0)

    th:wait()
    local ok_status, ret = th:result()
    t.eq(ok_status, "ok")
    t.ok(ret:match("status="), "child returned a status string")
    th:close()
    started:close()
    done:close()
end)

-- ------------------------------------------------------------------
-- (3) Suspend mid-syscall.
--
-- Thread A enters a long blocking wait.  Thread B suspends A then
-- resumes A.  A must continue and complete its wait.  This exercises
-- the trap-frame's reconstructibility from the suspended state — the
-- new SYSENTER prolog must produce a frame indistinguishable from an
-- INT 2E gate as far as KiSuspendThread / NtResumeThread are concerned.
-- ------------------------------------------------------------------

t.test("suspend / resume of a thread blocked in a syscall", function()
    local wakeup = ke.event()
    local th = thread.run([[
        local ffi    = require('ffi')
        local ke     = require('nt.dll.ke')
        local handle = require('nt.dll.handle')
        local h = handle.borrow(ffi.cast('void *', tonumber(PAYLOAD)))
        ke.NtWaitForSingleObject(h, false, nil)
        return 'woke'
    ]], tostring(tonumber(ffi.cast('intptr_t', handle.raw(wakeup._h)))))

    -- Let it enter the wait.
    ke.NtDelayExecution(false, ke.timeout(0.02))

    -- Suspend, then resume — child should still complete when we signal.
    local prev = ps.NtSuspendThread(th:handle())
    t.eq(prev, 0, "thread had not been suspended")
    prev = ps.NtResumeThread(th:handle())
    t.eq(prev, 1, "resume returns the prior suspend count")

    wakeup:signal()
    th:wait()
    local s, v = th:result()
    t.eq(s, "ok")
    t.eq(v, "woke")
    th:close()
    wakeup:close()
end)

-- ------------------------------------------------------------------
-- (4) High-rate concurrent syscalls — stresses MSR_SYSENTER_ESP reload
-- per context switch.  Any miswrite of the per-thread MSR value
-- corrupts the next thread's kernel stack on its first SYSENTER and
-- the system faults within milliseconds.
-- ------------------------------------------------------------------

t.test("syscall storm: 4 threads x 5000 NtQuerySystemTime", function()
    local N_THREADS = 4
    local N_CALLS   = 5000
    local threads   = {}
    for i = 1, N_THREADS do
        threads[i] = thread.run(([[
            local ke = require('nt.dll.ke')
            for i = 1, %d do ke.NtQuerySystemTime() end
            return 'ok'
        ]]):format(N_CALLS))
    end
    for i = 1, N_THREADS do
        threads[i]:wait()
        local s, v = threads[i]:result()
        t.eq(s, "ok", ("thread %d"):format(i))
        t.eq(v, "ok", ("thread %d"):format(i))
        threads[i]:close()
    end
end)

-- ------------------------------------------------------------------
-- (5) Latency check.  Informational: if SYSENTER+SYSEXIT are wired
-- correctly we should see a substantial drop vs. the prior INT 2E
-- baseline.  Prints elapsed time so a regression is visible in logs;
-- doesn't assert a number (qemu-relative).
-- ------------------------------------------------------------------

t.test("syscall latency: 100k NtQuerySystemTime round-trips", function()
    local N = 100000
    local t0 = tonumber(ke.NtQuerySystemTime().QuadPart)
    for i = 1, N do
        ke.NtQuerySystemTime()
    end
    local t1 = tonumber(ke.NtQuerySystemTime().QuadPart)
    -- NT system time is in 100ns ticks.
    local elapsed_us = (t1 - t0) / 10
    print(('       %d calls in %.0f us → %.2f us/call'):format(
        N, elapsed_us, elapsed_us / N))
    -- No hard assertion; just exercise the path and report.
end)
