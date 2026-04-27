-- Directory — NT Object Manager directory object. Drives \, \Device,
-- \BaseNamedObjects, \Driver, \FileSystem, ...
--
-- open     → NtOpenDirectoryObject(DIRECTORY_QUERY | DIRECTORY_TRAVERSE)
-- children → coroutine over NtQueryDirectoryObject, one entry per yield.
--            Each yielded Node carries its true TypeName, so the next
--            handler-dispatch (:iter() again, or :open()) picks the
--            right type module without a re-stat.

local ffi  = require('ffi')
local tree = require('nt.tree')
local ob   = require('nt.dll.ob')
local oa   = require('nt.dll.oa')
local str  = require('nt.dll.str')

-- OBJECT_DIRECTORY_INFORMATION cdef lives in nt.dll.ob; ob is already
-- required above, so the type is visible here without re-declaration.

local DIR_ACCESS             = 0x3        -- DIRECTORY_QUERY | DIRECTORY_TRAVERSE
local STATUS_NO_MORE_ENTRIES = 0x8000001A

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

local M = {}

function M.open(node)
    return ob.NtOpenDirectoryObject(DIR_ACCESS, oa.path(node.path).oa)
end

function M.children(node)
    return coroutine.wrap(function()
        -- Scoped handle: open a fresh one just for this iteration and
        -- let it die with the coroutine. Do NOT use node:open() —
        -- that caches on Node.__handle and turns every walked directory
        -- into a retained kernel handle.
        local dir   = M.open(node)
        local buf   = ffi.new('char[4096]')
        local ctx   = ffi.new('ULONG[1]')
        local first = true
        while true do
            local len, st = ob.NtQueryDirectoryObject(
                dir, buf, 4096, true, first, ctx)
            first = false
            if st == STATUS_NO_MORE_ENTRIES or len == 0 then break end
            local info = ffi.cast('OBJECT_DIRECTORY_INFORMATION *', buf)
            local name = str.from_utf16(info.Name)
            local tn   = str.from_utf16(info.TypeName)
            coroutine.yield(tree.Node.new(node, name, join(node.path, name), tn))
        end
    end)
end

return M
