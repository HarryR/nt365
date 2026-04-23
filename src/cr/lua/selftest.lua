-- MicroNT selftest entry point. Runs every suite under test/ and
-- prints a pass/fail summary. Launch via `make selftest`; the
-- Makefile target sets INIT_ARGS so run.exe points here instead of
-- main.lua.
--
-- Tests run in-process under pcall isolation. When NtCreateThread
-- and NtCreateProcess are bridged we'll extend the harness to run
-- suites in sibling threads or child processes for fault isolation;
-- the t.test() API is designed to stay source-compatible with that.

local ffi   = require('ffi')
local ntdll = require('nt.dll')
local t     = require('test')

ffi.cdef[[
NTSTATUS __stdcall RtlAdjustPrivilege(ULONG Privilege,
                                      unsigned char Enable,
                                      unsigned char Client,
                                      unsigned char *WasEnabled);
NTSTATUS __stdcall NtShutdownSystem(int Action);
]]

print("MicroNT selftest")
print("================")

-- Suite order doesn't matter — each suite is self-contained — but
-- grouping shallow modules first makes debug easier when things
-- explode early.
require('test.str')
require('test.handle')
require('test.ob')
require('test.cm')
require('test.fs')
require('test.mm')
require('test.lpc')
require('test.sys')
require('test.tree')
require('test.thread')

local ok = t.summary()
print("")
if ok then
    print("ALL PASSED — shutting down")
else
    print("FAILURES — shutting down with failure status")
end

-- Clean shutdown. Exit status doesn't propagate out of QEMU in any
-- useful way for this harness, so the summary line is the signal.
local was = ffi.new('unsigned char[1]')
ntdll.RtlAdjustPrivilege(19 --[[ SE_SHUTDOWN_PRIVILEGE ]], 1, 0, was)
ntdll.NtShutdownSystem(2 --[[ ShutdownPowerOff ]])
while true do end
