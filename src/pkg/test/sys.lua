-- nt.dll.sys — NtQuerySystemInformation iterators. Checks both
-- SystemProcessInformation (each_process) and SystemModuleInformation
-- (each_module) produce sane plain-Lua-table snapshots.

local t   = require('test')
local sys = require('nt.dll.sys')

t.suite("sys")

t.test("each_process finds the System (kernel) process", function()
    local seen_system = false
    local count = 0
    for proc in sys.each_process() do
        count = count + 1
        t.ne(proc.pid, nil)
        t.ne(proc.image, nil)
        t.ne(proc.threads, nil)
        if proc.image == "System" then
            seen_system = true
            t.ok(proc.thread_count > 0, "System has at least one thread")
            t.ok(#proc.threads == proc.thread_count,
                 "threads array length matches thread_count")
        end
    end
    t.ok(count >= 2, "at least idle + System + run.exe")
    t.ok(seen_system, "one of the processes is named 'System'")
end)

t.test("each_process yields plain Lua tables (no cdata in snapshot)", function()
    for proc in sys.each_process() do
        t.eq(type(proc.pid),     "number")
        t.eq(type(proc.image),   "string")
        t.eq(type(proc.threads), "table")
        if #proc.threads > 0 then
            local th = proc.threads[1]
            t.eq(type(th.tid),      "number")
            t.eq(type(th.priority), "number")
        end
        break   -- one is enough
    end
end)

t.test("each_process finds run.exe (our own process)", function()
    local found = false
    for proc in sys.each_process() do
        if proc.image == "run.exe" then
            found = true
            t.ok(proc.pid > 0)
            t.ok(proc.thread_count >= 1)
        end
    end
    t.ok(found, "our own run.exe is in the process list")
end)

t.test("each_module finds ntoskrnl.exe + hal.dll", function()
    local seen_ntos, seen_hal = false, false
    for mod in sys.each_module() do
        t.ne(mod.basename, nil)
        t.ne(mod.image_path, nil)
        t.ok(mod.image_size > 0)
        if mod.basename == "ntoskrnl.exe" then seen_ntos = true end
        if mod.basename == "hal.dll"      then seen_hal  = true end
    end
    t.ok(seen_ntos, "ntoskrnl.exe present in module list")
    t.ok(seen_hal,  "hal.dll present in module list")
end)

t.test("each_module yields plain Lua tables", function()
    for mod in sys.each_module() do
        t.eq(type(mod.basename),    "string")
        t.eq(type(mod.image_path),  "string")
        t.eq(type(mod.image_base),  "number")
        t.eq(type(mod.image_size),  "number")
        break
    end
end)
