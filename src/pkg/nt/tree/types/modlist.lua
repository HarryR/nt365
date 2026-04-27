-- ModuleList — synthetic at \System\Modules. Yields one Module Node
-- per entry from sys.each_module(), which hands back plain Lua tables
-- (no cdata lifetime concerns).

local tree = require('nt.tree')
local sys  = require('nt.dll.sys')

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

local M = {}

function M.children(node)
    return coroutine.wrap(function()
        for mod in sys.each_module() do
            local mn = tree.Node.new(node, mod.basename,
                                     join(node.path, mod.basename), "Module")
            mn.__mod = mod
            coroutine.yield(mn)
        end
    end)
end

return M
