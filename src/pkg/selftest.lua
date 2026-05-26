-- MicroNT selftest entry point. Runs every suite under test/ and
-- prints a pass/fail summary. Launch via `make selftest`; the
-- Makefile target sets INIT_ARGS so run.exe points here instead of
-- main.lua.
--
-- Tests run in-process under pcall isolation. When NtCreateThread
-- and NtCreateProcess are bridged we'll extend the harness to run
-- suites in sibling threads or child processes for fault isolation;
-- the t.test() API is designed to stay source-compatible with that.

-- package.path + the zip searcher + io/os globals are set by the
-- runtime preamble (\SystemRoot\System32\preamble.lua, run by lua.dll
-- before this entry script).  No per-script path setup needed.

local t   = require('test')
local se  = require('nt.dll.se')
local sys = require('nt.dll.sys')

print("MicroNT selftest")
print("================")

-- Same boot prelude main.lua runs — publishes \NLS\ named sections
-- (for kernel32!nlslib) and \DosDevices\C: (for Win32 toolchain DOS
-- paths in test.ntosbe).  Idempotent so subsequent suites/processes
-- collapse to no-ops.
require('nt.boot').run()

-- Suite order: shallow modules first for easier debug on early
-- explosions.  The dhcp suite runs BEFORE the network-dependent
-- suites (afd, iphard) because the Vionet1 NTE now boots without
-- a static address — afd's UDP-loopback tests still work because
-- they bind 127.0.0.1, but anything touching the vionet interface
-- depends on dhcp.acquire() having configured it first.
require('test.str')
require('test.handle')
require('test.ob')
require('test.cm')
require('test.fs')
require('test.mm')
require('test.lpc')
-- harderr: single-process unit tests deliberately do NOT call
-- NtSetDefaultHardErrorPort (would set ExpReadyForErrors permanently
-- and break harderr_xproc).  The xproc suite owns the one-and-only
-- registration in this boot, so it must come BEFORE anything else
-- that might want to register the default port (nothing else does
-- today, so order between the two is loose).
require('test.harderr')
require('test.harderr_xproc')
require('test.sys')
require('test.tree')
require('test.thread')
require('test.ps')
require('test.sync')
require('test.iocp')
require('test.io')
require('test.os')
require('test.dhcp')
require('test.afd')
require('test.iphard')
require('test.sysenter')
require('test.se')
require('test.fuzz.se')
require('test.fuzz.iocp')
require('test.fuzz.npfs')
require('test.fuzz.lpc')
require('test.fuzz.ob')
require('test.fuzz.io')
require('test.fuzz.ps')
require('test.fuzz.mm')
require('test.fuzz.cm')
require('test.fuzz.ex')
require('test.fuzz.sys')
require('test.nls')
-- test.msvc (spawns the MS toolchain EXEs) and test.ntosbe (in-OS
-- NMAKE self-host probe) need \SystemRoot\src and \SystemRoot\pkg\
-- msvc20\ — they run from the `selfhost` profile / selfhost.lua, which
-- stages those.  The selftest disk omits them so it composes fast.

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
