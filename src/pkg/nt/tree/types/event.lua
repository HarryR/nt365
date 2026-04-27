local ex = require('nt.dll.ex')
local oa = require('nt.dll.oa')

local EVENT_QUERY_STATE = 0x0001

local EVENT_TYPE_NAMES = {
    [0] = "Notification",
    [1] = "Synchronization",
}

local M = {}

function M.open(node)
    return ex.NtOpenEvent(EVENT_QUERY_STATE, oa.path(node.path).oa)
end

local function query(node) return ex.NtQueryEvent(node:open()) end

M.fields = {
    event_type = function(n) return EVENT_TYPE_NAMES[query(n).EventType] end,
    signaled   = function(n) return query(n).EventState ~= 0 end,
    state      = function(n) return query(n).EventState end,
}

M.descriptions = {
    event_type = "Event type: 'Notification' (manual reset) or 'Synchronization' (auto reset).",
    signaled   = "true if the event is currently signaled.",
    state      = "Raw EventState integer (0 = not signaled, 1 = signaled).",
}

return M
