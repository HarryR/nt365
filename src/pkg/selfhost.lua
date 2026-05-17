-- MicroNT selfhost entry point. Runs the in-OS build suites — the MS
-- toolchain probe (test.msvc) and the NMAKE self-host probe
-- (test.ntosbe) — that selftest.lua omits because they need
-- \SystemRoot\src and \SystemRoot\pkg\msvc20\ staged.  Launch via
-- `make selfhost`, which selects the `selfhost` ntosbe profile (full
-- disk: source tree + toolchain) and points Init\Args here.
--
-- Mirrors selftest.lua's prelude + shutdown epilogue; only the suite
-- body differs.

-- Phase A reorg: every package lives under \SystemRoot\lua\.  See
-- main.lua for the broader rationale; set this before any require().
package.path = "\\SystemRoot\\lua\\?.lua;\\SystemRoot\\lua\\?\\init.lua"
package.cpath = ""

local t   = require('test')
local se  = require('nt.dll.se')
local sys = require('nt.dll.sys')

print("MicroNT selfhost")
print("================")

-- Same boot prelude main.lua / selftest.lua run — publishes \NLS\
-- named sections (for kernel32!nlslib) and \DosDevices\C: (for Win32
-- toolchain DOS paths in test.ntosbe).  Idempotent.
require('nt.boot').run()

-- test.msvc spawns the MS toolchain EXEs (ML/MC/…); test.ntosbe is the
-- in-OS NMAKE self-host probe that drives a rebuild against
-- \SystemRoot\src.
require('test.msvc')
require('test.ntosbe')

local ok = t.summary()
print("")
if ok then
    print("ALL PASSED — shutting down")
else
    print("FAILURES — shutting down with failure status")
end

-- Clean shutdown. Exit status doesn't propagate out of QEMU in any
-- useful way for this harness, so the summary line is the signal.
--
-- Defensively un-impersonate first: if some test failed mid-impersonate
-- without reverting, the privilege adjust + shutdown calls below would
-- access-check against our impersonation token and get
-- STATUS_BAD_IMPERSONATION_LEVEL — both calls would silently fail and
-- we'd hang forever in the spin loop. revert_to_self is idempotent so
-- the no-leak case is a no-op.
pcall(se.revert_to_self)

local sd_ok, sd_err = pcall(function()
    local tok = se.open_process_token{
        access = se.TOKEN_QUERY + se.TOKEN_ADJUST_PRIVILEGES,
    }
    se.enable_privileges(tok, {"SeShutdownPrivilege"})
    sys.NtShutdownSystem('power_off')
    tok:close()
end)
if not sd_ok then
    print("shutdown failed: " .. tostring(sd_err))
    print("(spinning — kill QEMU manually)")
end
while true do end
