-- nt.dll.ke — NT kernel executive: synchronisation primitives, wait,
-- thread alerts and delay. Maps to NTOS/KE on the kernel side.
--
-- Bridging convention (uniform across the nt.dll.* tree):
--   - OUT pointer arguments surface as return values.
--   - NTSTATUS < 0 raises a structured error (see nt.errors).
--     Callers inspect err.status directly — no string parsing.
--   - Positive NTSTATUS values (STATUS_TIMEOUT = 0x102, STATUS_ALERTED
--     = 0x101, STATUS_USER_APC = 0xC0, ...) are returned to the caller;
--     they are not errors.
--   - Lua booleans bridge to NT BOOLEAN: true → 1, false/nil → 0.
--
-- Callers still build OBJECT_ATTRIBUTES, LARGE_INTEGER timeouts etc.
-- by hand via ffi.new. For anything not wrapped here, require('nt.dll')
-- for the raw handle.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')

ffi.cdef[[
NTSTATUS __stdcall NtCreateEvent(HANDLE *EventHandle,
                                 ULONG DesiredAccess,
                                 OBJECT_ATTRIBUTES *ObjectAttributes,
                                 int EventType,
                                 unsigned char InitialState);
NTSTATUS __stdcall NtSetEvent      (HANDLE EventHandle, long *PreviousState);
NTSTATUS __stdcall NtResetEvent    (HANDLE EventHandle, long *PreviousState);
NTSTATUS __stdcall NtPulseEvent    (HANDLE EventHandle, long *PreviousState);
NTSTATUS __stdcall NtClearEvent    (HANDLE EventHandle);
NTSTATUS __stdcall NtWaitForSingleObject(HANDLE Object,
                                         unsigned char Alertable,
                                         LARGE_INTEGER *Timeout);
NTSTATUS __stdcall NtWaitForMultipleObjects(ULONG Count,
                                            HANDLE *Handles,
                                            int WaitType,
                                            unsigned char Alertable,
                                            LARGE_INTEGER *Timeout);
NTSTATUS __stdcall NtDelayExecution(unsigned char Alertable,
                                    LARGE_INTEGER *DelayInterval);
NTSTATUS __stdcall NtAlertThread   (HANDLE ThreadHandle);

NTSTATUS __stdcall NtQuerySystemTime(LARGE_INTEGER *SystemTime);
NTSTATUS __stdcall NtQueryPerformanceCounter(LARGE_INTEGER *PerformanceCounter,
                                             LARGE_INTEGER *PerformanceFrequency);
]]

local M = {}

function M.NtCreateEvent(access, oa, event_type, initial_state)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateEvent(h, access, oa, event_type,
                                   initial_state and 1 or 0)
    if err.is_error(st) then err.raise('NtCreateEvent', st) end
    return handle.wrap(h[0])
end

function M.NtSetEvent(h)
    local st = ntdll.NtSetEvent(handle.raw(h), nil)
    if err.is_error(st) then err.raise('NtSetEvent', st) end
end

function M.NtResetEvent(h)
    local st = ntdll.NtResetEvent(handle.raw(h), nil)
    if err.is_error(st) then err.raise('NtResetEvent', st) end
end

function M.NtPulseEvent(h)
    local st = ntdll.NtPulseEvent(handle.raw(h), nil)
    if err.is_error(st) then err.raise('NtPulseEvent', st) end
end

function M.NtClearEvent(h)
    local st = ntdll.NtClearEvent(handle.raw(h))
    if err.is_error(st) then err.raise('NtClearEvent', st) end
end

function M.NtWaitForSingleObject(h, alertable, timeout)
    local st = ntdll.NtWaitForSingleObject(handle.raw(h),
                                           alertable and 1 or 0,
                                           timeout)
    if err.is_error(st) then err.raise('NtWaitForSingleObject', st) end
    return err.normalize(st)
end

-- `handles` is a Lua table of NT_HANDLE wrappers. We extract each
-- into a fresh HANDLE[n] array inside this function — the table stays
-- on the Lua stack during our call, pinning the wrappers (and thus
-- their raw HANDLE values) through to kernel dispatch. A reactor with
-- a persistent wait set should bypass this wrapper and call
-- `ntdll.NtWaitForMultipleObjects` directly against its own array.
function M.NtWaitForMultipleObjects(handles, wait_type, alertable, timeout)
    local n   = #handles
    local arr = ffi.new('HANDLE[?]', n)
    for i = 1, n do
        arr[i-1] = handle.raw(handles[i])
    end
    local st = ntdll.NtWaitForMultipleObjects(n, arr, wait_type,
                                              alertable and 1 or 0,
                                              timeout)
    if err.is_error(st) then err.raise('NtWaitForMultipleObjects', st) end
    return err.normalize(st)
end

function M.NtDelayExecution(alertable, delay_interval)
    local st = ntdll.NtDelayExecution(alertable and 1 or 0, delay_interval)
    if err.is_error(st) then err.raise('NtDelayExecution', st) end
end

function M.NtAlertThread(thread_handle)
    local st = ntdll.NtAlertThread(handle.raw(thread_handle))
    if err.is_error(st) then err.raise('NtAlertThread', st) end
end

-- Wall-clock time in NT's native units (100ns intervals since 1601-01-01,
-- UTC). Returns a fresh LARGE_INTEGER cdata; the caller may keep it across
-- other calls. For a Unix-epoch number, divide QuadPart by 1e7 and
-- subtract 11_644_473_600 — see os.lua's time helpers.
function M.NtQuerySystemTime()
    local t  = ffi.new('LARGE_INTEGER')
    local st = ntdll.NtQuerySystemTime(t)
    if err.is_error(st) then err.raise('NtQuerySystemTime', st) end
    return t
end

-- Monotonic high-resolution counter, returned as (counter, frequency)
-- Lua numbers (each is the QuadPart of a LARGE_INTEGER). NT 3.5 with
-- MicroNT's custom HAL returns frequency=0 because the HAL doesn't
-- implement the perf counter primitive — callers should treat that as
-- "not available" and fall back to NtQuerySystemTime for elapsed time.
function M.NtQueryPerformanceCounter()
    local c  = ffi.new('LARGE_INTEGER')
    local f  = ffi.new('LARGE_INTEGER')
    local st = ntdll.NtQueryPerformanceCounter(c, f)
    if err.is_error(st) then err.raise('NtQueryPerformanceCounter', st) end
    return tonumber(c.QuadPart), tonumber(f.QuadPart)
end

-- ------------------------------------------------------------------
-- Timeouts
-- ------------------------------------------------------------------

-- Convert a Lua seconds value to a LARGE_INTEGER suitable for any NT
-- wait / sleep / timer API. nil → nil (caller passes nil for infinite).
-- Positive seconds → negative 100ns count, i.e. relative delay (the
-- almost-always-wanted form). For an absolute deadline (rare) build
-- the LARGE_INTEGER yourself and set a positive QuadPart.
--
-- Held as a cdata local by the caller so it's alive across the syscall:
--     local t = ke.timeout(5.0)
--     ke.NtWaitForSingleObject(h, false, t)
function M.timeout(seconds)
    if seconds == nil then return nil end
    local li = ffi.new('LARGE_INTEGER')
    li.QuadPart = -math.floor(seconds * 1e7)
    return li
end

-- ------------------------------------------------------------------
-- Lua-idiomatic Event wrapper
-- ------------------------------------------------------------------
--
-- Wraps NtCreateEvent + the NtSet/Reset/Pulse/Clear/Wait family in an
-- object with method access and GC-backed cleanup. The returned table
-- holds an NT_HANDLE in self._h — dropping all references to the
-- wrapper lets the handle's own __gc fire NtClose.
--
-- Constructor:
--   ke.event{
--       notify    = true|false,   -- true (default) = NotificationEvent
--                                    (manual reset), false = SynchronizationEvent
--                                    (auto-reset on single waiter release)
--       signaled  = true|false,   -- initial state, default false
--       access    = EVENT_ALL_ACCESS by default
--       oa        = OBJECT_ATTRIBUTES for a named event
--   } → Event
--
-- Methods: :signal() :reset() :pulse() :clear()
--          :wait(seconds)   -- returns true if signaled, false on timeout
--          :handle()        -- underlying NT_HANDLE (for cross-object waits)
--          :close()

local Event = {}
Event.__index = Event

function Event:signal() return M.NtSetEvent(self._h)   end
function Event:reset()  return M.NtResetEvent(self._h) end
function Event:pulse()  return M.NtPulseEvent(self._h) end
function Event:clear()  return M.NtClearEvent(self._h) end

function Event:wait(seconds)
    local t = M.timeout(seconds)
    local st = M.NtWaitForSingleObject(self._h, false, t)
    return st == 0   -- 0 = STATUS_SUCCESS (signaled); 0x102 = timeout
end

function Event:handle() return self._h end

function Event:close()
    if self._h then self._h:close(); self._h = nil end
end

local EVENT_ALL_ACCESS = 0x1F0003

function M.event(opts)
    opts = opts or {}
    local event_type = 0   -- NotificationEvent
    if opts.notify == false then event_type = 1 end   -- SynchronizationEvent
    local h = M.NtCreateEvent(
        opts.access or EVENT_ALL_ACCESS,
        opts.oa,
        event_type,
        opts.signaled or false)
    return setmetatable({ _h = h }, Event)
end

M.Event = Event   -- exposed so callers can extend the metatable if needed

return M
