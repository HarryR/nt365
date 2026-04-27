local ffi = require('ffi')
local ex  = require('nt.dll.ex')
local oa  = require('nt.dll.oa')

local SECTION_QUERY = 0x0001

local M = {}

function M.open(node)
    return ex.NtOpenSection(SECTION_QUERY, oa.path(node.path).oa)
end

local function query(node) return ex.NtQuerySection(node:open()) end

M.fields = {
    base_address = function(n)
        return tonumber(ffi.cast('uintptr_t', query(n).BaseAddress))
    end,
    allocation_attributes = function(n) return query(n).AllocationAttributes end,
    maximum_size          = function(n) return query(n).MaximumSize.QuadPart end,
}

M.descriptions = {
    base_address          = "Address the section was created to map at (0 if any).",
    allocation_attributes = "SEC_* flags (IMAGE=0x1000000, RESERVE=0x4000000, COMMIT=0x8000000, ...).",
    maximum_size          = "Maximum size of the section in bytes.",
}

return M
