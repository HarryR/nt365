-- Process — synthetic Node under \Processes. Carries the process
-- snapshot as a plain Lua table on self.__proc (filled by proclist.lua
-- from sys.each_process). :open() via NtOpenProcess(CLIENT_ID) gives a
-- live handle for :info()-style introspection. :children() yields
-- Thread Nodes from the frozen thread list.

local ffi  = require('ffi')
local ps   = require('nt.dll.ps')
local tree = require('nt.tree')

local PROCESS_QUERY_INFORMATION = 0x0400

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

local M = {}

function M.open(node)
    local cid = ffi.new('CLIENT_ID')
    cid.UniqueProcess = ffi.cast('HANDLE', node.__proc.pid)
    cid.UniqueThread  = nil
    return ps.NtOpenProcess(PROCESS_QUERY_INFORMATION, ps.empty_oa(), cid)
end

-- Yield one Thread Node per entry in the frozen thread snapshot. The
-- thread's __thread record is a plain Lua table (copied by sys), so
-- thread fields and open-by-CLIENT_ID have no cdata dependency.
function M.children(node)
    return coroutine.wrap(function()
        for _, t in ipairs(node.__threads or {}) do
            local name = tostring(t.tid)
            local tn = tree.Node.new(node, name, join(node.path, name), "Thread")
            tn.__thread = t
            coroutine.yield(tn)
        end
    end)
end

-- All fields forward to the __proc snapshot. Names match sys.copy_process.
local function from_proc(key)
    return function(n) return n.__proc[key] end
end

M.fields = {
    pid              = from_proc("pid"),
    parent_pid       = from_proc("parent_pid"),
    image            = from_proc("image"),
    threads          = from_proc("thread_count"),
    priority         = from_proc("priority"),
    create_time      = from_proc("create_time"),
    user_time        = from_proc("user_time"),
    kernel_time      = from_proc("kernel_time"),
    virtual_size     = from_proc("virtual_size"),
    peak_virtual     = from_proc("peak_virtual"),
    working_set      = from_proc("working_set"),
    peak_working_set = from_proc("peak_working_set"),
    page_faults      = from_proc("page_faults"),
    paged_pool       = from_proc("paged_pool"),
    non_paged_pool   = from_proc("non_paged_pool"),
    pagefile         = from_proc("pagefile"),
    peak_pagefile    = from_proc("peak_pagefile"),
    private_pages    = from_proc("private_pages"),
    io_read_ops      = from_proc("io_read_ops"),
    io_write_ops     = from_proc("io_write_ops"),
    io_other_ops     = from_proc("io_other_ops"),
    io_read_bytes    = from_proc("io_read_bytes"),
    io_write_bytes   = from_proc("io_write_bytes"),
    io_other_bytes   = from_proc("io_other_bytes"),
}

M.descriptions = {
    pid              = "Process ID.",
    parent_pid       = "PID of the process that created this one (0 if parent has exited).",
    image            = "Image file name.",
    threads          = "Thread count at snapshot time.",
    priority         = "Base priority (KPRIORITY).",
    create_time      = "Creation time (NT 100ns ticks since 1601-01-01).",
    user_time        = "Total user-mode CPU time (100ns units).",
    kernel_time      = "Total kernel-mode CPU time (100ns units).",
    virtual_size     = "Committed virtual memory (bytes).",
    peak_virtual     = "Peak committed virtual memory (bytes).",
    working_set      = "Resident set size (bytes).",
    peak_working_set = "Peak resident set size (bytes).",
    page_faults      = "Cumulative page-fault count.",
    paged_pool       = "Paged-pool quota usage (bytes).",
    non_paged_pool   = "Non-paged-pool quota usage (bytes).",
    pagefile         = "Pagefile usage (bytes).",
    peak_pagefile    = "Peak pagefile usage (bytes).",
    private_pages    = "Private committed pages.",
    io_read_ops      = "Read I/O operation count.",
    io_write_ops     = "Write I/O operation count.",
    io_other_ops     = "Other I/O operation count.",
    io_read_bytes    = "Bytes read via file/device I/O.",
    io_write_bytes   = "Bytes written via file/device I/O.",
    io_other_bytes   = "Other I/O bytes transferred.",
}

return M
