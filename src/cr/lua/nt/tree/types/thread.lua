-- Thread — synthetic Node under \Processes\<pid>\<tid>. Carries the
-- snapshot record on self.__thread (a plain Lua table from
-- sys.copy_process). :open() via NtOpenThread uses the (pid, tid)
-- CLIENT_ID so :info() works against a live handle.

local ffi = require('ffi')
local ps  = require('nt.dll.ps')

local THREAD_QUERY_INFORMATION = 0x0040

-- THREAD_STATE enum (from NT source). Values may vary slightly across
-- versions; these match NT 3.5 ke.h.
local THREAD_STATES = {
    [0] = "Initialized", [1] = "Ready",      [2] = "Running",
    [3] = "Standby",     [4] = "Terminated", [5] = "Wait",
    [6] = "Transition",
}

-- KWAIT_REASON enum.
local WAIT_REASONS = {
    [0]  = "Executive",      [1]  = "FreePage",      [2]  = "PageIn",
    [3]  = "PoolAllocation", [4]  = "DelayExecution",[5]  = "Suspended",
    [6]  = "UserRequest",    [7]  = "WrExecutive",   [8]  = "WrFreePage",
    [9]  = "WrPageIn",       [10] = "WrPoolAllocation",
    [11] = "WrDelayExecution",[12] = "WrSuspended",  [13] = "WrUserRequest",
    [14] = "WrEventPair",    [15] = "WrQueue",       [16] = "WrLpcReceive",
    [17] = "WrLpcReply",     [18] = "WrVirtualMemory",[19] = "WrPageOut",
    [20] = "WrRendezvous",
}

local M = {}

function M.open(node)
    local cid = ffi.new('CLIENT_ID')
    cid.UniqueProcess = ffi.cast('HANDLE', node.__thread.pid)
    cid.UniqueThread  = ffi.cast('HANDLE', node.__thread.tid)
    return ps.NtOpenThread(THREAD_QUERY_INFORMATION, ps.empty_oa(), cid)
end

local function from_thread(key)
    return function(n) return n.__thread[key] end
end

M.fields = {
    tid              = from_thread("tid"),
    pid              = from_thread("pid"),
    start_address    = from_thread("start_address"),
    priority         = from_thread("priority"),
    base_priority    = from_thread("base_priority"),
    context_switches = from_thread("context_switches"),
    wait_time        = from_thread("wait_time"),
    create_time      = from_thread("create_time"),
    user_time        = from_thread("user_time"),
    kernel_time      = from_thread("kernel_time"),
    thread_state     = function(n)
        local v = n.__thread.thread_state
        return THREAD_STATES[v] or v
    end,
    wait_reason      = function(n)
        local v = n.__thread.wait_reason
        return WAIT_REASONS[v] or v
    end,
}

M.descriptions = {
    tid              = "Thread ID.",
    pid              = "Owning process ID.",
    start_address    = "Thread entry-point address.",
    priority         = "Current scheduler priority.",
    base_priority    = "Base priority (set by SetThreadPriority).",
    context_switches = "Cumulative context-switch count.",
    thread_state     = "Scheduler state (Running/Ready/Wait/...).",
    wait_reason      = "Reason for wait when thread_state == Wait.",
    wait_time        = "Time spent in current wait (ticks).",
    create_time      = "Creation time (NT 100ns ticks since 1601-01-01).",
    user_time        = "User-mode CPU time (100ns units).",
    kernel_time      = "Kernel-mode CPU time (100ns units).",
}

return M
