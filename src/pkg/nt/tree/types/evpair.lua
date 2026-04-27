local ex = require('nt.dll.ex')
local oa = require('nt.dll.oa')

-- EventPair doesn't have a query-state access like the other sync
-- primitives; STANDARD_RIGHTS_READ (0x20000) is the minimum for
-- NtQueryObject.
local EVENT_PAIR_READ = 0x20000

return {
    open = function(node)
        return ex.NtOpenEventPair(EVENT_PAIR_READ, oa.path(node.path).oa)
    end,
}
