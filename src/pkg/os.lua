-- os — Lua's standard `os` module, reimplemented over nt.dll.
--
-- This is a Lua-side replacement for LuaJIT's built-in lib_os.c, which
-- we compile out (see -DLUAJIT_DISABLE_LIB_OS in src/cr/Makefile). The
-- built-in expected POSIX/CRT primitives (time, gmtime, mktime,
-- strftime, getenv, system, ...); we go straight to NT via nt.dll.
--
-- Coverage (v1):
--   os.time([table])       Wall-clock seconds since Unix epoch (UTC)
--                          via NtQuerySystemTime + epoch-bias math.
--                          Table form converts via RtlTimeFieldsToTime.
--   os.clock()             Elapsed seconds since this module loaded.
--                          NtQueryPerformanceCounter when the HAL
--                          provides a frequency; falls back to
--                          NtQuerySystemTime (100ns ticks) otherwise.
--                          NOT process-CPU time (stock Lua's
--                          definition) — that needs
--                          NtQueryInformationProcess(KernelTime+UserTime),
--                          left for a follow-up. Elapsed is good
--                          enough for the typical "how long did this
--                          take" usage and matches what os.clock did
--                          under our previous libc.
--   os.date([fmt [, t]])   Format a time as a string or table.
--                          UTC only — NT 3.5 has no in-kernel TZ DB
--                          we can drive from user mode, so localtime
--                          == gmtime.
--   os.difftime(a, b)      Trivial wrapper.
--   os.getenv(name)        Wraps rtl.query_env.
--   os.remove(path)        NtOpenFile + FileDispositionInformation.
--   os.rename(old, new)    NtOpenFile + FileRenameInformation.
--   os.exit(code [, close]) NtTerminateProcess. `close` (Lua 5.2+) is
--                          ignored — process death runs no atexit hooks.
--   os.setlocale(...)      Stub returning "C" (no locale support).
--   os.execute, os.popen,  Raise — no shell, no subprocess primitive
--   os.tmpname, os.tmpfile yet, no NT-namespace tmp dir convention.

local ke  = require('nt.dll.ke')
local ps  = require('nt.dll.ps')
local rtl = require('nt.dll.rtl')
local fs  = require('nt.dll.fs')
local oa  = require('nt.dll.oa')

-- Wide-open share mask for delete/rename opens — composite assembled
-- from the bits exposed by nt.dll.fs. NT semantics: even
-- FILE_SHARE_DELETE alone doesn't let us delete-while-busy; we still
-- need DELETE access on the handle.
local FILE_SHARE_ALL = fs.FILE_SHARE_READ + fs.FILE_SHARE_WRITE
                     + fs.FILE_SHARE_DELETE

local M = {}

-- ------------------------------------------------------------------
-- strftime subset
-- ------------------------------------------------------------------
--
-- Implements the specifiers Lua scripts in this codebase actually need.
-- Anything else passes through verbatim (so "%X format" stays "%X format"
-- rather than turning into garbage). Locale is "C" — English names only.
--
-- Operates on the calendar table produced by rtl.unix_to_table — the
-- same shape Lua's os.date "*t" returns. We never see TIME_FIELDS
-- here; rtl owns the cdata boundary.

local SHORT_WEEKDAYS = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}
local LONG_WEEKDAYS  = {"Sunday","Monday","Tuesday","Wednesday",
                            "Thursday","Friday","Saturday"}
local SHORT_MONTHS = {"Jan","Feb","Mar","Apr","May","Jun",
                      "Jul","Aug","Sep","Oct","Nov","Dec"}
local LONG_MONTHS  = {"January","February","March","April","May","June",
                      "July","August","September","October","November","December"}

local function strftime(fmt, t)
    return (fmt:gsub("%%(.)", function(spec)
        if spec == "Y" then return string.format("%04d", t.year)
        elseif spec == "y" then return string.format("%02d", t.year % 100)
        elseif spec == "m" then return string.format("%02d", t.month)
        elseif spec == "d" then return string.format("%02d", t.day)
        elseif spec == "H" then return string.format("%02d", t.hour)
        elseif spec == "M" then return string.format("%02d", t.min)
        elseif spec == "S" then return string.format("%02d", t.sec)
        elseif spec == "p" then return t.hour < 12 and "AM" or "PM"
        elseif spec == "a" then return SHORT_WEEKDAYS[t.wday] or "?"
        elseif spec == "A" then return LONG_WEEKDAYS [t.wday] or "?"
        elseif spec == "b" then return SHORT_MONTHS[t.month] or "?"
        elseif spec == "B" then return LONG_MONTHS [t.month] or "?"
        elseif spec == "j" then return string.format("%03d", t.yday)
        elseif spec == "w" then return tostring(t.wday - 1)   -- POSIX: Sun=0
        elseif spec == "c" then
            return string.format("%s %s %2d %02d:%02d:%02d %d",
                SHORT_WEEKDAYS[t.wday] or "?",
                SHORT_MONTHS[t.month]  or "?",
                t.day, t.hour, t.min, t.sec, t.year)
        elseif spec == "x" then
            return string.format("%02d/%02d/%02d",
                t.month, t.day, t.year % 100)
        elseif spec == "X" then
            return string.format("%02d:%02d:%02d", t.hour, t.min, t.sec)
        elseif spec == "%" then return "%"
        else return "%" .. spec     -- unknown spec: pass through
        end
    end))
end

-- ------------------------------------------------------------------
-- Public API
-- ------------------------------------------------------------------

function M.time(t)
    if t == nil then
        return math.floor(rtl.li_to_unix(ke.NtQuerySystemTime()))
    end
    local secs, why = rtl.table_to_unix(t)
    if not secs then error("os.time: " .. why, 2) end
    return math.floor(secs)
end

-- os.clock — elapsed seconds since this module loaded. Captured at
-- require-time by the prologue below. Two backends:
--   1. NtQueryPerformanceCounter when the HAL gives us a non-zero
--      frequency. Sub-microsecond resolution.
--   2. NtQuerySystemTime (100ns ticks) otherwise — what MicroNT's
--      custom HAL forces us into today.
local _clock_start_qpc, _clock_qpc_freq
do
    local c, f = ke.NtQueryPerformanceCounter()
    if f and f > 0 then
        _clock_start_qpc, _clock_qpc_freq = c, f
    end
end
-- Convert immediately so the LARGE_INTEGER cdata isn't retained as an
-- upvalue (we only need the scalar tick count for the elapsed delta).
local _clock_start_ticks_n = tonumber(ke.NtQuerySystemTime().QuadPart)

function M.clock()
    if _clock_qpc_freq then
        local c             = ke.NtQueryPerformanceCounter()
        local elapsed_ticks = (c - _clock_start_qpc)
        return elapsed_ticks / _clock_qpc_freq
    end
    local now           = ke.NtQuerySystemTime()
    local elapsed_ticks = (tonumber(now.QuadPart) - _clock_start_ticks_n)
    return elapsed_ticks / rtl.NT_TICKS_PER_SEC
end

function M.date(fmt, t)
    fmt = fmt or "%c"
    local secs = t or M.time()
    -- Stock Lua: leading "!" forces UTC. We always do UTC so the prefix
    -- is just stripped — same outcome either way on this platform.
    if fmt:sub(1, 1) == "!" then fmt = fmt:sub(2) end
    local tab = rtl.unix_to_table(secs)
    if fmt == "*t" then
        tab.isdst = false                     -- not modelled on this platform
        return tab
    end
    return strftime(fmt, tab)
end

function M.difftime(a, b)
    return a - b
end

function M.getenv(name)
    if type(name) ~= "string" then
        error("bad argument #1 to 'getenv' (string expected)", 2)
    end
    return rtl.query_env(name)
end

-- File-system mutators. Both go through pcall so the stock Lua
-- "nil + errmsg" contract is preserved instead of unwinding through
-- the structured-error mechanism in nt.errors.

function M.remove(path)
    if type(path) ~= "string" then
        error("bad argument #1 to 'remove' (string expected)", 2)
    end
    local ok, why = pcall(function()
        local noa = oa.path(path)
        local h   = fs.NtOpenFile(fs.DELETE + fs.SYNCHRONIZE, noa.oa,
                                  FILE_SHARE_ALL,
                                  fs.FILE_SYNCHRONOUS_IO_NONALERT)
        fs.set_disposition(h, true)
        h:close()                              -- last close → kernel deletes
    end)
    if not ok then return nil, tostring(why) end
    return true
end

function M.rename(old, new)
    if type(old) ~= "string" or type(new) ~= "string" then
        error("os.rename: both arguments must be strings", 2)
    end
    local ok, why = pcall(function()
        local noa = oa.path(old)
        -- DELETE access is the rename-source requirement on NT;
        -- SYNCHRONIZE so we can pass FILE_SYNCHRONOUS_IO_NONALERT.
        local h = fs.NtOpenFile(fs.DELETE + fs.SYNCHRONIZE, noa.oa,
                                FILE_SHARE_ALL,
                                fs.FILE_SYNCHRONOUS_IO_NONALERT)
        fs.set_rename(h, new, false)           -- replace=false: stock Lua
        h:close()
    end)
    if not ok then return nil, tostring(why) end
    return true
end

function M.exit(code, _close)
    if code == nil or code == true then code = 0
    elseif code == false then code = 1
    elseif type(code) ~= "number" then
        error("bad argument #1 to 'exit' (number/boolean expected)", 2)
    end
    -- _close is Lua 5.2's "close lua_State first" hook; we have nothing
    -- meaningful to close (no atexit, no buffered stdio at this layer).
    -- NtTerminateProcess on the current process pseudo-handle does not
    -- return.
    ps.NtTerminateProcess(nil, code)
end

function M.setlocale(_loc, _category)
    -- Always "C" locale; stock Lua returns the new locale name on
    -- success or nil on failure. Returning "C" mirrors what stock Lua
    -- does on a system without locale data.
    return "C"
end

local function not_supported(name, why)
    return function()
        error("os." .. name .. ": " .. why, 2)
    end
end

M.execute = not_supported("execute",
    "no shell on micront — use ps.create_thread / future process bridge")
M.popen   = not_supported("popen",
    "needs anonymous-pipe + process creation (follow-up work)")
M.tmpname = not_supported("tmpname",
    "no tmp dir convention on NT 3.5")
M.tmpfile = not_supported("tmpfile",
    "no tmp dir convention on NT 3.5")

return M
