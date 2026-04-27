-- nt.dll.rtl — pure-userland Rtl* helpers exported from ntdll.
--
-- These are not syscalls; the kernel knows nothing about them. They are
-- standalone routines linked into ntdll for everyone (kernel32, user
-- code) to share — calendar arithmetic, privilege adjustment, string
-- decoding, etc. Subsystem-prefixed Nt* wrappers live in their own
-- modules (ke / mm / ob / ...); this is the home for ntdll utilities
-- that don't belong to any subsystem.
--
-- Currently:
--   RtlTimeToTimeFields            LARGE_INTEGER (100ns since 1601, UTC)
--                                  → TIME_FIELDS calendar struct (raw
--                                  cdata interface; rare).
--   RtlTimeFieldsToTime            inverse. Returns nil + reason on
--                                  invalid fields.
--   query_env(name)                lookup an environment variable in
--                                  the current process. Wraps
--                                  RtlQueryEnvironmentVariable_U +
--                                  the buffer-too-small grow loop.
--   li_to_unix(li)                 LARGE_INTEGER (NT 100ns) → Unix
--                                  seconds (Lua number).
--   unix_to_table(secs)            Unix seconds → calendar table with
--                                  Lua-style fields (year, month, day,
--                                  hour, min, sec, wday, yday).
--   table_to_unix(t)               inverse; nil + reason on bad fields.
--
-- The Unix-epoch math (EPOCH_BIAS_SEC = 11_644_473_600 between
-- 1601-01-01 and 1970-01-01) lives here rather than in user code so
-- the same constant doesn't get copy-pasted into every consumer.
--
-- TIME_FIELDS layout matches NTOS/INC/ntoskrnl.h: SHORTs in
-- year/month/day/hour/min/sec/msec/weekday order. The cdef stays
-- internal — Lua code paths use the table API above and never
-- ffi.new('TIME_FIELDS') directly.

local ffi   = require('ffi')
local ntdll = require('nt.dll')
local err   = require('nt.dll.errors')
local str   = require('nt.dll.str')

ffi.cdef[[
typedef struct _TIME_FIELDS {
    short Year;
    short Month;
    short Day;
    short Hour;
    short Minute;
    short Second;
    short Milliseconds;
    short Weekday;
} TIME_FIELDS;

void          __stdcall RtlTimeToTimeFields(LARGE_INTEGER *Time, TIME_FIELDS *TimeFields);
unsigned char __stdcall RtlTimeFieldsToTime(TIME_FIELDS *TimeFields, LARGE_INTEGER *Time);

NTSTATUS __stdcall RtlQueryEnvironmentVariable_U(void *Environment,
                                                 UNICODE_STRING *Name,
                                                 UNICODE_STRING *Value);
]]

local STATUS_BUFFER_TOO_SMALL    = 0xC0000023
local STATUS_VARIABLE_NOT_FOUND  = 0xC0000100

-- NT system time / RTL_USER_PROCESS_PARAMETERS env block work in
-- 100ns ticks and UTF-16 wchars respectively. Name the magic numbers
-- so reading sites don't have to remember why "1e7" or "* 2".
local NT_TICKS_PER_SEC = 1e7
local WCHAR_BYTES      = ffi.sizeof('wchar_t')

local M = {}

-- LARGE_INTEGER (100ns-since-1601) → TIME_FIELDS. Returns a fresh cdata.
-- li must remain reachable for the duration of this call (pass a Lua
-- local), but the returned TIME_FIELDS does not alias it — fully copied.
function M.RtlTimeToTimeFields(li)
    local tf = ffi.new('TIME_FIELDS')
    ntdll.RtlTimeToTimeFields(li, tf)
    return tf
end

-- TIME_FIELDS → LARGE_INTEGER. Returns the LARGE_INTEGER, or nil + reason
-- on invalid input. NT validates the fields as a unit (Feb 30 is rejected
-- even if Year/Month/Day are individually in range).
function M.RtlTimeFieldsToTime(tf)
    local li = ffi.new('LARGE_INTEGER')
    if ntdll.RtlTimeFieldsToTime(tf, li) == 0 then
        return nil, "invalid time fields"
    end
    return li
end

-- ------------------------------------------------------------------
-- Lua-friendly time API. Hides TIME_FIELDS / LARGE_INTEGER from
-- callers — they pass / receive Lua numbers and tables.
-- ------------------------------------------------------------------

-- NT system time is 100ns intervals since 1601-01-01 UTC. Unix epoch
-- is 11_644_473_600 seconds later. Same constant is used by C-side
-- libc_time.c — keep them in sync if it ever changes.
local EPOCH_BIAS_SEC = 11644473600
M.EPOCH_BIAS_SEC   = EPOCH_BIAS_SEC
M.NT_TICKS_PER_SEC = NT_TICKS_PER_SEC

local DAYS_IN_MONTH = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}

local function is_leap(year)
    return year % 4 == 0 and (year % 100 ~= 0 or year % 400 == 0)
end

local function day_of_year(month, day, year)
    local doy = day
    for i = 1, month - 1 do
        local d = DAYS_IN_MONTH[i]
        if i == 2 and is_leap(year) then d = 29 end
        doy = doy + d
    end
    return doy
end

-- LARGE_INTEGER (NT 100ns ticks since 1601) → Unix seconds. Pure math;
-- the cdata stays alive for the QuadPart access only — caller may drop
-- it immediately after this returns.
function M.li_to_unix(li)
    local secs_since_1601 = (tonumber(li.QuadPart) / NT_TICKS_PER_SEC)
    return secs_since_1601 - EPOCH_BIAS_SEC
end

-- Unix seconds → fresh LARGE_INTEGER cdata. Caller owns the result.
-- Raises if the resulting NT time would be pre-1601 — NT's tick range
-- doesn't go negative.
function M.unix_to_li(secs)
    local secs_since_1601 = (secs + EPOCH_BIAS_SEC)
    local nt_ticks        = (secs_since_1601 * NT_TICKS_PER_SEC)
    if nt_ticks < 0 then
        error("rtl.unix_to_li: time before 1601-01-01 not representable", 2)
    end
    local li = ffi.new('LARGE_INTEGER')
    li.QuadPart = nt_ticks
    return li
end

-- Unix seconds → calendar table. Keys match Lua's os.date "*t" form
-- (year, month, day, hour, min, sec, wday=1..7, yday=1..366) so the
-- consumer doesn't have to translate. The TIME_FIELDS cdata is purely
-- internal — never escapes this function.
function M.unix_to_table(secs)
    local li = M.unix_to_li(secs)
    local tf = M.RtlTimeToTimeFields(li)
    return {
        year  = tf.Year,
        month = tf.Month,
        day   = tf.Day,
        hour  = tf.Hour,
        min   = tf.Minute,
        sec   = tf.Second,
        wday  = tf.Weekday + 1,                   -- Lua: 1=Sun..7=Sat
        yday  = day_of_year(tf.Month, tf.Day, tf.Year),
    }
end

-- Calendar table → Unix seconds. `year`, `month`, `day` are required;
-- `hour` (default 12), `min` (0), `sec` (0) are optional. Returns nil
-- + reason on missing/invalid fields. Lua-style key names match
-- unix_to_table's output so the round-trip composes cleanly.
function M.table_to_unix(t)
    if type(t) ~= "table" then
        return nil, "table expected"
    end
    if not t.year or not t.month or not t.day then
        return nil, "table missing year/month/day"
    end
    local tf = ffi.new('TIME_FIELDS')
    tf.Year         = t.year
    tf.Month        = t.month
    tf.Day          = t.day
    tf.Hour         = t.hour or 12
    tf.Minute       = t.min  or 0
    tf.Second       = t.sec  or 0
    tf.Milliseconds = 0
    tf.Weekday      = 0                           -- ignored on input
    local li, why = M.RtlTimeFieldsToTime(tf)
    if not li then return nil, why end
    return M.li_to_unix(li)
end

-- ------------------------------------------------------------------
-- Environment
-- ------------------------------------------------------------------

-- Look up an environment variable in the current process. Takes a UTF-8
-- name; returns a UTF-8 value, or nil if the name isn't set. Other
-- syscall errors raise. NULL Environment = current-process env block.
--
-- ns_name and ns_value are kept as Lua locals through the syscall so
-- the inline UNICODE_STRING (.us) and the inline data buffer stay
-- reachable until the call returns; both are fused single-cdata via
-- nt.dll.str's NT_STRING type.
function M.query_env(name)
    local ns_name = str.to_utf16(name)
    local capacity = 256                      -- wchars; enough for typical values
    while true do
        local ns_value = str.new_utf16(capacity)
        local st  = ntdll.RtlQueryEnvironmentVariable_U(nil, ns_name.us, ns_value.us)
        local stu = err.normalize(st)
        if stu == STATUS_VARIABLE_NOT_FOUND then
            return nil
        elseif stu == STATUS_BUFFER_TOO_SMALL then
            -- ns_value.Length now holds the required size in bytes (no NUL).
            local needed_wchars = math.floor(ns_value.us.Length / WCHAR_BYTES)
            -- Grow with slack so a racing env update doesn't loop us forever.
            capacity = (needed_wchars * 2)
            if capacity > 32768 then
                error("query_env: value too large (>32K wchars) for "
                      .. tostring(name), 2)
            end
        elseif err.is_error(st) then
            err.raise('RtlQueryEnvironmentVariable_U', st)
        else
            return str.from_utf16(ns_value.us)
        end
    end
end

return M
