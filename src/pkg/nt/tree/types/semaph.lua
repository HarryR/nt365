local ex = require('nt.dll.ex')
local oa = require('nt.dll.oa')

local SEMAPHORE_QUERY_STATE = 0x0001

local M = {}

function M.open(node)
    return ex.NtOpenSemaphore(SEMAPHORE_QUERY_STATE, oa.path(node.path).oa)
end

local function query(node) return ex.NtQuerySemaphore(node:open()) end

M.fields = {
    current = function(n) return query(n).CurrentCount end,
    maximum = function(n) return query(n).MaximumCount end,
}

M.descriptions = {
    current = "Available semaphore slots (0 = fully held).",
    maximum = "Maximum slot count at creation.",
}

return M
