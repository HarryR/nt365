-- nt.dll.ps spawn smoke — load unmodified MS NT 3.5 Win32 EXEs against
-- our kernel32.dll, observe their behaviour.  Both ML /? and MC -?
-- are pure usage-help paths and exit cleanly with status 0; anything
-- else means kernel32 loaded but something downstream (stdio plumbing,
-- argv parsing, NLS lookup) misbehaved.
--
-- Targets:
--   ML.EXE — MASM assembler, kernel32-only, no CRT.
--   MC.EXE — message compiler, same shape.
--
-- Both staged under \SystemRoot\pkg\msvc20\ by ide.lua.

local bit    = require('bit')
local ffi    = require('ffi')
local t      = require('test')
local fs     = require('nt.dll.fs')
local oa     = require('nt.dll.oa')
local ps     = require('nt.dll.ps')
local ke     = require('nt.dll.ke')
local handle = require('nt.dll.handle')

t.suite("msvc")

local function nt_path_exists(nt_path)
    local noa = oa.path(nt_path)
    local ok, h = pcall(fs.NtOpenFile,
        bit.bor(fs.FILE_GENERIC_READ, fs.SYNCHRONIZE),
        noa.oa,
        fs.FILE_SHARE_READ,
        fs.FILE_SYNCHRONOUS_IO_NONALERT)
    if ok then h:close() end
    return ok
end

t.test("ML.EXE present in image", function()
    t.ok(nt_path_exists("\\SystemRoot\\pkg\\msvc20\\ML.EXE"),
         "ide.lua manifest staged ML.EXE under \\SystemRoot\\pkg\\msvc20\\")
end)

t.test("MC.EXE present in image", function()
    t.ok(nt_path_exists("\\SystemRoot\\pkg\\msvc20\\MC.EXE"))
end)

-- The parent's PEB std handles are what the child inherits.  All three
-- must be non-zero or RtlCreateUserProcess (RTLEXEC.C:949-995) skips
-- the dup-into-child step and the child writes WriteFile to NULL.
local function handle_value(h)
    return tonumber(ffi.cast('intptr_t', handle.raw(h)))
end

t.test("parent PEB stdio handles are non-NULL", function()
    local stdin_b, stdout_b, stderr_b = ps.parent_stdio()
    -- parent_stdio returns borrowed NT_HANDLEs (no close, no GC).
    t.ok(handle_value(stdin_b)  ~= 0, "parent has stdin")
    t.ok(handle_value(stdout_b) ~= 0, "parent has stdout")
    t.ok(handle_value(stderr_b) ~= 0, "parent has stderr")
end)

local function spawn_and_wait(spawn_opts)
    local label = spawn_opts.cmdline or spawn_opts.exe
    print("  spawning " .. label .. " ...")
    local proc = ps.spawn(spawn_opts)
    print(string.format("  pid=%d tid=%d entry=%s",
        proc.pid, proc.tid, tostring(proc.image.entry)))

    ps.NtResumeThread(proc.thread)
    ke.NtWaitForSingleObject(proc.process, false, nil)

    local info = ps.NtQueryInformationProcess_Basic(proc.process)
    print(string.format("  %s exit_status = 0x%08x  pid=%d  ppid=%d",
        label, info.exit_status, info.pid, info.ppid))

    proc.thread:close()
    proc.process:close()
    return info.exit_status
end

t.test("ML.EXE /? exits cleanly", function()
    -- /? forces usage output — disambiguates "stdio plumbing broken"
    -- from "ML had nothing to say with no-arg invocation."
    local status = spawn_and_wait{
        exe     = "\\SystemRoot\\pkg\\msvc20\\ML.EXE",
        cmdline = "ML.EXE /?",
    }
    t.eq(status, 0, "ML.EXE /? should exit with status 0")
end)

t.test("MC.EXE -? exits cleanly", function()
    local status = spawn_and_wait{
        exe     = "\\SystemRoot\\pkg\\msvc20\\MC.EXE",
        cmdline = "MC.EXE -?",
    }
    t.eq(status, 0, "MC.EXE -? should exit with status 0")
end)
