local ex = require('nt.dll.ex')
local oa = require('nt.dll.oa')

local MUTANT_QUERY_STATE = 0x0001

local M = {}

function M.open(node)
    return ex.NtOpenMutant(MUTANT_QUERY_STATE, oa.path(node.path).oa)
end

local function query(node) return ex.NtQueryMutant(node:open()) end

M.fields = {
    current_count   = function(n) return query(n).CurrentCount         end,
    owned_by_caller = function(n) return query(n).OwnedByCaller  ~= 0  end,
    abandoned       = function(n) return query(n).AbandonedState ~= 0  end,
}

M.descriptions = {
    current_count   = "Recursion count if held (>0 when available, ≤0 when owned).",
    owned_by_caller = "true if the current thread holds this mutant.",
    abandoned       = "true if previous owner exited without releasing.",
}

return M
