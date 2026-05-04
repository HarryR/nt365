-- test — tiny selftest harness.
--
-- Usage from each suite file:
--   local t = require('test')
--   local mm = require('nt.dll.mm')
--   t.suite("mm")
--   t.test("alloc/free round-trip", function()
--       local base, size = mm.NtAllocateVirtualMemory(...)
--       t.eq(size, 4096)
--       mm.NtFreeVirtualMemory(...)
--   end)
--
-- The entry script (selftest.lua) requires every suite module and
-- then calls t.summary() which prints counts and returns pass/fail.
--
-- Isolation: right now every test runs in-process under pcall — fine
-- for Lua errors, but a cdata fault (wild pointer deref, access
-- violation) will take down the whole runner. When NtCreateThread
-- and NtCreateProcess are bridged we'll add thread- or process-level
-- isolation behind an opt-in flag; the t.test() API stays stable so
-- suites don't need to change.

local M = {}

local ok_count   = 0
local fail_count = 0
local skip_count = 0
local failures   = {}
local skips      = {}

-- Distinct sentinel so t.skip() is distinguishable from a real error.
-- Using error level 0 keeps line-info noise out of the message.
local SKIP_PREFIX = "__TEST_SKIP__:"

function M.suite(name)
    print("")
    print("--- " .. name .. " ---")
end

function M.test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        ok_count = ok_count + 1
        print(string.format("  PASS  %s", name))
    elseif type(err) == "string" and err:sub(1, #SKIP_PREFIX) == SKIP_PREFIX then
        local reason = err:sub(#SKIP_PREFIX + 1)
        skip_count = skip_count + 1
        skips[#skips+1] = { name = name, reason = reason }
        print(string.format("  SKIP  %s  (%s)", name, reason))
    else
        fail_count = fail_count + 1
        failures[#failures+1] = { name = name, err = tostring(err) }
        print(string.format("  FAIL  %s", name))
        print(string.format("        %s", tostring(err)))
    end
end

-- Skip the current test. Safe to call anywhere inside the test fn;
-- stops execution and records as skipped, not failed.
function M.skip(reason)
    error(SKIP_PREFIX .. (reason or ""), 0)
end

-- ------------------------------------------------------------------
-- Assertions. All throw from the test fn on failure; test() catches
-- and records. `ctx` is an optional human string for disambiguation
-- when the same assertion fires on multiple values.
-- ------------------------------------------------------------------

local function tag(ctx) return ctx and (" (" .. ctx .. ")") or "" end

function M.eq(a, b, ctx)
    if a ~= b then
        error(string.format("eq%s: expected %s, got %s",
            tag(ctx), tostring(b), tostring(a)), 2)
    end
end

function M.ne(a, b, ctx)
    if a == b then
        error(string.format("ne%s: both were %s", tag(ctx), tostring(a)), 2)
    end
end

-- Truthy check. `t.ok(x)` accepts anything non-false, non-nil.
function M.ok(v, ctx)
    if not v then
        error(string.format("ok%s: got %s", tag(ctx), tostring(v)), 2)
    end
end

-- Assert fn raises. Optional pattern matches the error message.
function M.raises(fn, pattern)
    local ok, err = pcall(fn)
    if ok then
        error("expected error, got success", 2)
    end
    if pattern and not tostring(err):match(pattern) then
        error(string.format("expected error matching %q, got: %s",
            pattern, tostring(err)), 2)
    end
end

-- ------------------------------------------------------------------
-- Reporting
-- ------------------------------------------------------------------

-- Prints the final summary and returns true iff no test failed.
-- Skipped tests don't count as failures.
function M.summary()
    print("")
    print(string.format("== %d passed, %d failed, %d skipped ==",
        ok_count, fail_count, skip_count))
    if fail_count > 0 then
        print("")
        print("failures:")
        for _, f in ipairs(failures) do
            print(string.format("  %s", f.name))
            print(string.format("    %s", f.err))
        end
    end
    if skip_count > 0 then
        print("")
        print("skipped:")
        for _, s in ipairs(skips) do
            local reason = s.reason ~= "" and s.reason or "(no reason given)"
            print(string.format("  %s — %s", s.name, reason))
        end
    end
    return fail_count == 0
end

return M
