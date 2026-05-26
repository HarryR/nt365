-- Child chunk for pkg/test/harderr_xproc.lua.
--
-- Spawned as a separate NT process via ps.spawn.  Calls
-- NtRaiseHardError with a known status and exits with the daemon's
-- response as the process exit code.  The parent test reads the
-- exit code to verify the round-trip end-to-end.
--
-- Standalone: must not depend on any pkg/test/ helpers (separate
-- process), and is self-contained at the require-graph level.
--
-- This file runs in a separate process from the daemon, so the
-- HARDERR.C:404-417 recursion guard does NOT fire here -- exactly
-- the property the cross-process design hangs on.

local ex = require('nt.dll.ex')

-- Status the parent expects to see.  STATUS_ASSERTION_FAILURE
-- (0xC0000420 on modern Windows; on NT 3.5 it's reserved but
-- NT_ERROR-shaped, which is what matters for routing).  Any high-bit-
-- set status whose name is not load-bearing for our boot path works.
local STATUS_TEST_HARDERR = 0xC0000420

-- Optional first arg overrides the status (lets the parent test
-- exercise different status codes without re-shipping the child).
local status = STATUS_TEST_HARDERR
if arg and arg[1] then status = tonumber(arg[1]) end

local response = ex.NtRaiseHardError(
    status,
    {},                            -- no parameters
    0,                             -- no unicode-string parameters
    ex.HARDERROR_OPTION.OK         -- daemon may reply OK or RETURN_TO_CALLER
)

os.exit(response)
