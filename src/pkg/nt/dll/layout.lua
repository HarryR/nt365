-- nt.dll.layout — struct-layout self-checks.
--
-- Every cdef of an NT 3.5 OS struct in nt.dll.* should pair with a
-- check_offsets() call so that if a future LuaJIT, MSVC, or
-- compile-flag change shifts a field, the module fails to load with
-- a precise diagnostic naming the type and field rather than
-- producing weird runtime symptoms (NULL handles where there
-- shouldn't be any, garbage pointers, off-by-pack ?????? strings).
--
-- Why this matters: NT 3.5 source uses #pragma pack(2) on the OS
-- structs (PEB, TEB, RTL_USER_PROCESS_PARAMETERS, …).  LuaJIT FFI
-- and mingw-built code default to native alignment, so a missing
-- pack(2) in our cdef silently shifts every field after the first
-- BOOLEAN-followed-by-pointer.  We learned this the hard way reading
-- PEB.ProcessParameters at offset 0x10 instead of 0x0E and getting
-- the upper half of the pointer back.
--
-- check_offsets{
--   PEB_HEAD = {
--     InheritedAddressSpace = 0x00,
--     Mutant                = 0x02,
--     ProcessParameters     = 0x0E,
--     _size                 = 22,    -- optional whole-struct size check
--   },
--   ...
-- }

local ffi = require('ffi')

local M = {}

-- Verify each named field in `specs[type]` is at the expected offset.
-- `_size` (optional) verifies sizeof(type).  Errors at level 2 so
-- the caller's site shows up in the traceback, not this helper.
function M.check_offsets(specs)
    for typ, fields in pairs(specs) do
        for field, expected in pairs(fields) do
            local actual
            if field == '_size' then
                actual = tonumber(ffi.sizeof(typ))
                if actual ~= expected then
                    error(string.format(
                        "%s sizeof mismatch: expected %d, got %d",
                        typ, expected, actual), 2)
                end
            else
                actual = tonumber(ffi.offsetof(typ, field))
                if actual ~= expected then
                    error(string.format(
                        "%s.%s offset mismatch: expected 0x%X, got 0x%X",
                        typ, field, expected, actual), 2)
                end
            end
        end
    end
end

return M
