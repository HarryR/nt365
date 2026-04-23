-- ProcessList — synthetic virtual at \Processes. Yields one Process
-- Node per entry from sys.each_process(), which already hands back
-- plain Lua tables (no cdata lifetime concerns).

local tree = require('nt.tree')
local sys  = require('nt.dll.sys')

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

local M = {}

function M.children(node)
    return coroutine.wrap(function()
        for proc in sys.each_process() do
            local name = tostring(proc.pid)
            local n = tree.Node.new(node, name, join(node.path, name), "Process")
            -- Copy the whole snapshot onto the Node's __proc table so
            -- the Process handler's lazy fields can read any of them
            -- without another syscall. threads is the trailing array.
            n.__proc    = proc
            n.__threads = proc.threads
            coroutine.yield(n)
        end
    end)
end

return M
