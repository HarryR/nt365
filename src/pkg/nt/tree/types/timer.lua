local ex = require('nt.dll.ex')
local oa = require('nt.dll.oa')

local TIMER_QUERY_STATE = 0x0001

local M = {}

function M.open(node)
    return ex.NtOpenTimer(TIMER_QUERY_STATE, oa.path(node.path).oa)
end

local function query(node) return ex.NtQueryTimer(node:open()) end

M.fields = {
    remaining_time = function(n) return query(n).RemainingTime.QuadPart end,
    signaled       = function(n) return query(n).TimerState ~= 0        end,
}

M.descriptions = {
    remaining_time = "Remaining interval until next fire (100ns units, negative = absolute time).",
    signaled       = "true if the timer has expired and not yet been reset.",
}

return M
