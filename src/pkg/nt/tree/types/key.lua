-- Key — registry key (NT Configuration Manager object). Drives
-- \Registry and everything beneath it.
--
-- children yields two kinds of Node:
--   - Value nodes  (type_name="Value") — one per value on this key,
--                                        carrying (type, length, data)
--                                        captured at enumeration time
--                                        so no reread is needed.
--   - Key nodes    (type_name="Key")   — one per subkey.
--
-- Values are emitted first so a plain walk prints them before nested
-- subkeys — easier to read when both exist on one key.

local ffi  = require('ffi')
local tree = require('nt.tree')
local cm   = require('nt.dll.cm')
local oa   = require('nt.dll.oa')
local str  = require('nt.dll.str')

-- KEY_BASIC_INFORMATION / KEY_VALUE_FULL_INFORMATION + info-class
-- constants + REG_* type codes all live in nt.dll.cm; cm is already
-- required above, so they're visible here.

local KEY_READ_ACCESS         = 0x9   -- KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS
local STATUS_NO_MORE_ENTRIES  = 0x8000001A
local STATUS_BUFFER_OVERFLOW  = 0x80000005

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

local M = {}

function M.open(node)
    return cm.NtOpenKey(KEY_READ_ACCESS, oa.path(node.path).oa)
end

function M.children(node)
    return coroutine.wrap(function()
        -- Scoped handle (see dir.lua for the rationale — keeps the
        -- walker from accumulating a cached __handle per key).
        local key = M.open(node)

        -- Values first.
        local vbuf = ffi.new('char[4096]')
        local vidx = 0
        while true do
            local len, st = cm.NtEnumerateValueKey(
                key, vidx, cm.KeyValueFullInformation, vbuf, 4096)
            if st == STATUS_NO_MORE_ENTRIES then break end
            if st ~= STATUS_BUFFER_OVERFLOW then
                local info = ffi.cast('KEY_VALUE_FULL_INFORMATION *', vbuf)
                local name = str.from_wchars(info.Name, info.NameLength / 2)
                if name == "" then name = "(default)" end
                local dp = ffi.cast('char *', vbuf) + info.DataOffset
                local v  = tree.Node.new(node, name, join(node.path, name), "Value")
                v.__value_type   = info.Type
                v.__value_length = info.DataLength
                v.__value_data   = ffi.string(dp, info.DataLength)
                coroutine.yield(v)
            end
            vidx = vidx + 1
        end

        -- Subkeys.
        local kbuf = ffi.new('char[4096]')
        local kidx = 0
        while true do
            local len, st = cm.NtEnumerateKey(
                key, kidx, cm.KeyBasicInformation, kbuf, 4096)
            if st == STATUS_NO_MORE_ENTRIES then break end
            if st ~= STATUS_BUFFER_OVERFLOW then
                local info = ffi.cast('KEY_BASIC_INFORMATION *', kbuf)
                local name = str.from_wchars(info.Name, info.NameLength / 2)
                coroutine.yield(
                    tree.Node.new(node, name, join(node.path, name), "Key"))
            end
            kidx = kidx + 1
        end
    end)
end

return M
