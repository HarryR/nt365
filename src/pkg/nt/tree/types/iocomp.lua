local ex = require('nt.dll.ex')
local oa = require('nt.dll.oa')

local IO_COMPLETION_QUERY_STATE = 0x0001

return {
    open = function(node)
        return ex.NtOpenIoCompletion(IO_COMPLETION_QUERY_STATE, oa.path(node.path).oa)
    end,
}
