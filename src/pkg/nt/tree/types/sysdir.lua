-- SystemDir — synthetic container for system-wide kernel info views.
-- Its children are themselves synthetic sub-trees (Modules, later
-- Handles / Perf / Info) backed by NtQuerySystemInformation classes.

local tree = require('nt.tree')

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

local CHILDREN = {
    { name = "Modules", type_name = "ModuleList" },
}

local M = {}

function M.children(node)
    return coroutine.wrap(function()
        for _, spec in ipairs(CHILDREN) do
            coroutine.yield(tree.Node.new(node, spec.name,
                                          join(node.path, spec.name),
                                          spec.type_name))
        end
    end)
end

return M
