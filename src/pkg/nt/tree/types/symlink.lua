-- SymbolicLink — NT object-manager symlink. Leaves the tree (no
-- children) but exposes .target as a lazy field.

local ob  = require('nt.dll.ob')
local oa  = require('nt.dll.oa')

local SYMBOLIC_LINK_QUERY = 0x1

local M = {}

function M.open(node)
    return ob.NtOpenSymbolicLinkObject(SYMBOLIC_LINK_QUERY, oa.path(node.path).oa)
end

M.fields = {
    target = function(node)
        return ob.NtQuerySymbolicLinkObject(node:open())
    end,
}

M.descriptions = {
    target = "Absolute NT path this symlink resolves to.",
}

return M
