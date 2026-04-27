-- nt.dll.errors — structured errors for NT syscalls.
--
-- Every bridged wrapper (across nt.dll.* sub-modules) raises failures
-- via this module's `raise` so callers see a single, uniform error
-- shape:
--
--   {
--     fn     = 'NtCreateFile',       -- the NT symbol that failed
--     status = 0xC0000022,           -- NTSTATUS, normalised to uint32
--   }
--
-- __tostring returns "NtCreateFile: STATUS 0xc0000022" for log lines
-- and uncaught propagation.
--
-- Callers inspect err.status directly against MSDN-style STATUS_*
-- constants (literal hex in Lua parses as the same uint32 number) —
-- no string parsing required.

local M = {}

local err_mt = {
    __tostring = function(self)
        return string.format("%s: STATUS 0x%08x", self.fn, self.status)
    end,
}

-- normalize(status) — signed NTSTATUS from an FFI call (long) to the
-- uint32 form that matches MSDN-style hex constants.
function M.normalize(status)
    return status < 0 and status + 0x100000000 or status
end

-- is_error(status) — true iff the NTSTATUS severity is ERROR
-- (top two bits set, i.e. 0xC0000000..0xFFFFFFFF). Success codes
-- (0x0*), informational (0x4*) and warnings (0x8*) all return false
-- so wrappers pass them through to the caller without raising.
--
-- Accepts either signed-long form (as returned by FFI) or already-
-- normalised uint32.
function M.is_error(status)
    local u = status < 0 and status + 0x100000000 or status
    return u >= 0xC0000000
end

-- raise(fn, status) — throw the structured error. status is normalised
-- internally so err.status always matches MSDN hex constants.
function M.raise(fn, status)
    local u = status < 0 and status + 0x100000000 or status
    error(setmetatable({ fn = fn, status = u }, err_mt))
end

return M
