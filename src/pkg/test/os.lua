-- os — Lua-side os module over nt.dll (ke + rtl + ps + fs).
--
-- Time helpers use deterministic fixed Unix epochs so the round-trip
-- through NtQuerySystemTime / RtlTimeToTimeFields / RtlTimeFieldsToTime
-- is exact-equality-checkable (no flaky "current time" assertions).
--
-- Lifetime audit while reading these tests:
--   * os.remove and os.rename internally open a handle (NT_HANDLE),
--     issue NtSetInformationFile, then close. Failure paths use pcall
--     inside os.lua so we get nil + errmsg here instead of an unwound
--     structured error.
--   * os.getenv passes UTF-8 through to RtlQueryEnvironmentVariable_U
--     via the str.to_utf16 / str.from_utf16 fused-cdata path. No raw
--     buffers leak across the syscall boundary.

local t  = require('test')
local os = require('os')
local ke = require('nt.dll.ke')

t.suite("os")

local SCRATCH_A = "\\SystemRoot\\__test_os_a.tmp"
local SCRATCH_B = "\\SystemRoot\\__test_os_b.tmp"

local function reset_scratch()
    os.remove(SCRATCH_A)
    os.remove(SCRATCH_B)
end

-- ------------------------------------------------------------------
-- time / date / difftime
-- ------------------------------------------------------------------

t.test("os.time() returns a number; skip post-2026 check if no RTC", function()
    local now = os.time()
    t.eq(type(now), "number")
    -- The RTC isn't wired up in this build — the kernel boots SYSTEM_TIME
    -- at 0 (1601-01-01) and only advances it by uptime ticks. Once an
    -- RTC driver lands and we see post-Unix-epoch values, tighten this
    -- assertion to `now >= 1767225600` (2026-01-01).
    if now < 0 then
        t.skip("system time pre-1970 (no RTC) — got " .. tostring(now))
    end
    t.ok(now >= 1767225600, "now=" .. tostring(now))
end)

t.test("os.time(table) round-trips through os.date('*t')", function()
    -- 2026-04-25 12:34:56 UTC.
    local secs = os.time{year=2026, month=4, day=25,
                         hour=12, min=34, sec=56}
    t.eq(type(secs), "number")
    local tab = os.date("*t", secs)
    t.eq(tab.year,  2026)
    t.eq(tab.month, 4)
    t.eq(tab.day,   25)
    t.eq(tab.hour,  12)
    t.eq(tab.min,   34)
    t.eq(tab.sec,   56)
    t.eq(tab.isdst, false)
    -- 2026-04-25 was a Saturday → Lua wday=7.
    t.eq(tab.wday,  7)
    -- Day of year: Jan(31)+Feb(28)+Mar(31)+Apr 25 = 115. 2026 is not leap.
    t.eq(tab.yday,  115)
end)

t.test("os.time on invalid table fields raises", function()
    t.raises(function()
        os.time{year=2026, month=13, day=1}    -- month=13 is bogus
    end, "fields")
end)

t.test("os.time table missing required fields raises", function()
    t.raises(function() os.time{year=2026} end, "year/month/day")
end)

t.test("os.date format string emits %Y/%m/%d/%H/%M/%S", function()
    local secs = os.time{year=2024, month=2, day=29,         -- leap day
                         hour=23, min=59, sec=58}
    t.eq(os.date("%Y-%m-%d %H:%M:%S", secs), "2024-02-29 23:59:58")
end)

t.test("os.date format string handles %a/%A/%b/%B/%j/%p/%%", function()
    local secs = os.time{year=2024, month=2, day=29,         -- Thursday
                         hour=15, min=0, sec=0}
    t.eq(os.date("%a", secs), "Thu")
    t.eq(os.date("%A", secs), "Thursday")
    t.eq(os.date("%b", secs), "Feb")
    t.eq(os.date("%B", secs), "February")
    t.eq(os.date("%j", secs), "060",   "31 (Jan) + 29 (Feb leap day)")
    t.eq(os.date("%p", secs), "PM")
    t.eq(os.date("%% literal", secs), "% literal")
end)

t.test("os.date('!*t') matches os.date('*t') (UTC-only platform)", function()
    local secs = os.time{year=2026, month=4, day=25, hour=12, min=0, sec=0}
    local utc   = os.date("!*t", secs)
    local local_t = os.date("*t",  secs)
    t.eq(utc.year,  local_t.year)
    t.eq(utc.month, local_t.month)
    t.eq(utc.day,   local_t.day)
    t.eq(utc.hour,  local_t.hour)
    t.eq(utc.min,   local_t.min)
    t.eq(utc.sec,   local_t.sec)
end)

t.test("os.date with no fmt defaults to %c", function()
    -- Just check it returns a string with the year in it; the exact
    -- formatting of %c is non-essential to test.
    local s = os.date(nil, os.time{year=2026, month=1, day=1,
                                    hour=0, min=0, sec=0})
    t.ok(s:match("2026"), "%c output contains the year: " .. s)
end)

t.test("os.difftime", function()
    t.eq(os.difftime(100, 40), 60)
    t.eq(os.difftime(0, 1), -1)
end)

-- ------------------------------------------------------------------
-- clock — monotonic
-- ------------------------------------------------------------------

t.test("os.clock is monotonic across a sleep", function()
    local a = os.clock()
    -- 50ms — long enough to advance any reasonable clock backend.
    ke.NtDelayExecution(false, ke.timeout(0.05))
    local b = os.clock()
    t.ok(b >= a, string.format("a=%g b=%g", a, b))
    t.ok(b - a >= 0.04,
         string.format("expected >= 40ms elapsed, got %g", b - a))
end)

-- ------------------------------------------------------------------
-- getenv
-- ------------------------------------------------------------------

t.test("os.getenv on missing variable returns nil", function()
    t.eq(os.getenv("__definitely_not_set__"), nil)
end)

t.test("os.getenv returns a string when the var is set", function()
    -- We can't predict what's in the env on this VM; SystemRoot / Path
    -- are written to the registry but propagation to per-process env
    -- depends on smss/init wiring. Check whichever is actually there;
    -- skip if neither is populated.
    local sr = os.getenv("SystemRoot")
    local pa = os.getenv("Path")
    if sr == nil and pa == nil then
        t.skip("no env vars propagated to this process")
    end
    if sr ~= nil then t.eq(type(sr), "string") end
    if pa ~= nil then t.eq(type(pa), "string") end
end)

t.test("os.getenv rejects non-string argument", function()
    t.raises(function() os.getenv(42) end, "string")
end)

-- ------------------------------------------------------------------
-- remove / rename
-- ------------------------------------------------------------------

t.test("os.remove on nonexistent file returns nil + errmsg", function()
    reset_scratch()
    local ok, errmsg = os.remove("\\SystemRoot\\__nope_" ..
                                  tostring(os.time()) .. ".tmp")
    t.eq(ok, nil)
    t.ok(errmsg and #errmsg > 0)
end)

t.test("os.remove deletes an existing file", function()
    reset_scratch()
    local io = require('io')
    local f = io.open(SCRATCH_A, "wb"); f:write("scratch"); f:close()
    -- Confirm it exists by reopening for read.
    local r = io.open(SCRATCH_A, "rb")
    t.ne(r, nil)
    r:close()

    t.eq(os.remove(SCRATCH_A), true)

    -- Now opening it should fail.
    local r2, _err = io.open(SCRATCH_A, "rb")
    t.eq(r2, nil, "file is gone after os.remove")
end)


t.test("os.rename moves an existing file", function()
    reset_scratch()
    local io = require('io')
    local f = io.open(SCRATCH_A, "wb"); f:write("payload"); f:close()

    local ok, errmsg = os.rename(SCRATCH_A, SCRATCH_B)
    t.eq(ok, true, "rename failed: " .. tostring(errmsg))

    -- A is gone, B has the payload.
    local r_a = io.open(SCRATCH_A, "rb")
    t.eq(r_a, nil)
    local r_b = io.open(SCRATCH_B, "rb")
    t.ne(r_b, nil)
    t.eq(r_b:read("*a"), "payload")
    r_b:close()
    os.remove(SCRATCH_B)
end)

t.test("os.rename on nonexistent source returns nil + errmsg", function()
    reset_scratch()
    local ok, errmsg = os.rename(
        "\\SystemRoot\\__not_there_" .. tostring(os.time()) .. ".tmp",
        SCRATCH_B)
    t.eq(ok, nil)
    t.ok(errmsg and #errmsg > 0)
end)

-- ------------------------------------------------------------------
-- Stubbed / unsupported surface
-- ------------------------------------------------------------------

t.test("os.setlocale returns 'C'", function()
    t.eq(os.setlocale(),     "C")
    t.eq(os.setlocale("en_US"), "C")
end)

t.test("os.execute / popen / tmpname / tmpfile raise", function()
    t.raises(os.execute, "execute")
    t.raises(os.popen,   "popen")
    t.raises(os.tmpname, "tmpname")
    t.raises(os.tmpfile, "tmpfile")
end)

-- os.exit is intentionally not tested — it terminates the process and
-- would take the test runner with it. The wrapper is exercised at
-- selftest end via the existing NtShutdownSystem path.
