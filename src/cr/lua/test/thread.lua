-- nt.dll.ps thread lifecycle. Drives rt_thread_flag_set and
-- rt_thread_signal_event from rt/test_thread.c to confirm
-- RtlCreateUserThread actually spawns and runs a new OS thread that
-- invokes our native entry with the parameter we supplied.

local ffi = require('ffi')
local t   = require('test')
local ps  = require('nt.dll.ps')
local ke  = require('nt.dll.ke')
local mm  = require('nt.dll.mm')

ffi.cdef[[
unsigned long __stdcall rt_thread_flag_set(void *param);
unsigned long __stdcall rt_thread_signal_event(void *param);
]]

t.suite("thread")

t.test("RtlCreateUserThread spawns a thread that runs our entry", function()
    -- Allocate a page of RW memory for the thread to scribble a magic
    -- value into; allocating in shared VM keeps the flag visible from
    -- both threads without special plumbing.
    local flag_base, _ = mm.NtAllocateVirtualMemory(nil, nil, 4096,
        mm.MEM_COMMIT + mm.MEM_RESERVE, mm.PAGE_READWRITE)
    ffi.cast('uint32_t *', flag_base)[0] = 0

    local th, tid = ps.create_thread(ffi.C.rt_thread_flag_set, flag_base)
    t.ne(th, nil)
    t.ok(tid > 0, "got a real tid")

    -- Wait for the thread to exit — thread handle is signaled when the
    -- thread terminates, so a single wait with a sane timeout is
    -- enough. 1 second in NT 100ns units is -10_000_000.
    local timeout = ffi.new('LARGE_INTEGER')
    timeout.QuadPart = -10000000LL
    ke.NtWaitForSingleObject(th, false, timeout)

    t.eq(ffi.cast('uint32_t *', flag_base)[0], 0x12345678,
         "entry ran and stored magic through the param pointer")

    th:close()
    mm.NtFreeVirtualMemory(nil, flag_base, 0, mm.MEM_RELEASE)
end)

t.test("thread signals an event on exit, parent waits on it", function()
    -- Same idea but the thread notifies via an event instead of a
    -- memory poke. Exercises the NtSetEvent-from-another-thread path.
    local ev = ke.NtCreateEvent(0x1F0003 --[[ EVENT_ALL_ACCESS ]],
                                nil, 1 --[[ NotificationEvent ]],
                                false)
    -- Pass the raw event HANDLE as the thread parameter. Must detach
    -- would be wrong here: the parent still owns the handle and will
    -- close it. The child only reads through the raw value.
    local raw_ev = ffi.cast('void *', require('nt.dll.handle').raw(ev))
    local th, tid = ps.create_thread(ffi.C.rt_thread_signal_event, raw_ev)

    -- Wait on the event (set by the thread) rather than on the thread
    -- handle itself. Tests that cross-thread signalling works.
    local timeout = ffi.new('LARGE_INTEGER')
    timeout.QuadPart = -10000000LL
    local st = ke.NtWaitForSingleObject(ev, false, timeout)
    t.eq(st, 0 --[[ STATUS_SUCCESS ]], "event was signaled within timeout")

    -- Drain the thread too so NT_HANDLE:close doesn't race the exit.
    ke.NtWaitForSingleObject(th, false, timeout)

    th:close()
    ev:close()
end)

t.test("thread created suspended, resumed runs to completion", function()
    local flag_base, _ = mm.NtAllocateVirtualMemory(nil, nil, 4096,
        mm.MEM_COMMIT + mm.MEM_RESERVE, mm.PAGE_READWRITE)
    ffi.cast('uint32_t *', flag_base)[0] = 0

    local th, _ = ps.create_thread(ffi.C.rt_thread_flag_set, flag_base,
                                   { suspended = true })
    -- Nothing should have run yet. Cheap sanity check: flag untouched.
    t.eq(ffi.cast('uint32_t *', flag_base)[0], 0)

    local prev = ps.NtResumeThread(th)
    t.eq(prev, 1, "thread was suspended once, resume returns 1")

    local timeout = ffi.new('LARGE_INTEGER')
    timeout.QuadPart = -10000000LL
    ke.NtWaitForSingleObject(th, false, timeout)
    t.eq(ffi.cast('uint32_t *', flag_base)[0], 0x12345678)

    th:close()
    mm.NtFreeVirtualMemory(nil, flag_base, 0, mm.MEM_RELEASE)
end)
