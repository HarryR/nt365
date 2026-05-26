-- nt.dll.ex — Executive primitives (sync objects, memory sections).
-- Maps to NTOS/EX on the kernel side. Everything here is shaped
-- identically: NtOpenX(HANDLE *out, ACCESS_MASK, OBJECT_ATTRIBUTES *).
--
-- Nothing here sets values, signals, or waits — those live on the
-- opened handle via Nt{Wait,Set,Release,Reset,Pulse}*. Add as real
-- callers show up; for now the object-manager tree only needs the
-- openers so introspection via NtQueryObject works.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')

-- Pack(4) to match NT 3.5's LARGE_INTEGER layout (see nt.dll.sys for
-- the reasoning — MSC 8.00 4-aligned long long). Affects TIMER_* and
-- SECTION_BASIC_INFORMATION here.
ffi.cdef[[
#pragma pack(push, 4)

typedef struct _EVENT_BASIC_INFORMATION {
    int  EventType;      /* 0 = Notification, 1 = Synchronization */
    long EventState;     /* 0 = NotSignaled, 1 = Signaled */
} EVENT_BASIC_INFORMATION;

/* pack(2) here specifically: NT 3.5 kernel compiles this struct in
 * MUTANT.C without tail padding (total 6 bytes), and NtQueryMutant
 * does a strict != sizeof() length check. pack(4) default would give
 * 8 bytes (tail-padded to LONG alignment) and the syscall rejects it
 * with STATUS_INFO_LENGTH_MISMATCH. */
#pragma pack(push, 2)
typedef struct _MUTANT_BASIC_INFORMATION {
    long          CurrentCount;
    unsigned char OwnedByCaller;
    unsigned char AbandonedState;
} MUTANT_BASIC_INFORMATION;
#pragma pack(pop)

typedef struct _SEMAPHORE_BASIC_INFORMATION {
    long CurrentCount;
    long MaximumCount;
} SEMAPHORE_BASIC_INFORMATION;

typedef struct _TIMER_BASIC_INFORMATION {
    LARGE_INTEGER RemainingTime;
    unsigned char TimerState;
} TIMER_BASIC_INFORMATION;

typedef struct _SECTION_BASIC_INFORMATION {
    void *        BaseAddress;
    ULONG         AllocationAttributes;
    LARGE_INTEGER MaximumSize;
} SECTION_BASIC_INFORMATION;

#pragma pack(pop)

NTSTATUS __stdcall NtOpenEvent       (HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtOpenEventPair   (HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtOpenSection     (HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtOpenMutant      (HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtOpenSemaphore   (HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtOpenTimer       (HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtOpenIoCompletion(HANDLE *h, ULONG Access, OBJECT_ATTRIBUTES *oa);

NTSTATUS __stdcall NtCreateMutant(HANDLE *h, ULONG Access,
                                  OBJECT_ATTRIBUTES *oa,
                                  unsigned char InitialOwner);
NTSTATUS __stdcall NtReleaseMutant(HANDLE h, long *PreviousCount);

NTSTATUS __stdcall NtCreateSemaphore(HANDLE *h, ULONG Access,
                                     OBJECT_ATTRIBUTES *oa,
                                     long InitialCount,
                                     long MaximumCount);
NTSTATUS __stdcall NtReleaseSemaphore(HANDLE h,
                                      long ReleaseCount,
                                      long *PreviousCount);

NTSTATUS __stdcall NtCreateTimer(HANDLE *h, ULONG Access,
                                 OBJECT_ATTRIBUTES *oa,
                                 int TimerType);
/* NT 3.5 NtSetTimer is 5-arg and one-shot: the ResumeTimer / Period
 * parameters (and periodic timers) did not arrive until NT 4.0. */
NTSTATUS __stdcall NtSetTimer(HANDLE h,
                              LARGE_INTEGER *DueTime,
                              void *TimerApcRoutine,
                              void *TimerContext,
                              unsigned char *PreviousState);
NTSTATUS __stdcall NtCancelTimer(HANDLE h, unsigned char *CurrentState);

NTSTATUS __stdcall NtCreateEventPair(HANDLE *h, ULONG Access,
                                     OBJECT_ATTRIBUTES *oa);
NTSTATUS __stdcall NtSetHighEventPair       (HANDLE h);
NTSTATUS __stdcall NtSetLowEventPair        (HANDLE h);
NTSTATUS __stdcall NtWaitHighEventPair      (HANDLE h);
NTSTATUS __stdcall NtWaitLowEventPair       (HANDLE h);
NTSTATUS __stdcall NtSetHighWaitLowEventPair(HANDLE h);
NTSTATUS __stdcall NtSetLowWaitHighEventPair(HANDLE h);

/* IO_COMPLETION_BASIC_INFORMATION — single LONG. Flat, pack-agnostic. */
typedef struct _IO_COMPLETION_BASIC_INFORMATION {
    long Depth;
} IO_COMPLETION_BASIC_INFORMATION;

NTSTATUS __stdcall NtCreateIoCompletion(HANDLE *h, ULONG Access,
                                        OBJECT_ATTRIBUTES *oa,
                                        ULONG ConcurrentThreads);
NTSTATUS __stdcall NtRemoveIoCompletion(HANDLE h,
                                        void **KeyContext,
                                        void **ApcContext,
                                        IO_STATUS_BLOCK *IoStatusBlock,
                                        LARGE_INTEGER *Timeout);
NTSTATUS __stdcall NtQueryIoCompletion(HANDLE h,
                                       int InformationClass,
                                       void *Information,
                                       ULONG Length,
                                       ULONG *ReturnLength);

NTSTATUS __stdcall NtQueryEvent    (HANDLE h, int cls, void *info, ULONG len, ULONG *ret);
NTSTATUS __stdcall NtQueryMutant   (HANDLE h, int cls, void *info, ULONG len, ULONG *ret);
NTSTATUS __stdcall NtQuerySemaphore(HANDLE h, int cls, void *info, ULONG len, ULONG *ret);
NTSTATUS __stdcall NtQueryTimer    (HANDLE h, int cls, void *info, ULONG len, ULONG *ret);
NTSTATUS __stdcall NtQuerySection  (HANDLE h, int cls, void *info, ULONG len, ULONG *ret);

/* HARDERR.C -- hard-error routing */
NTSTATUS __stdcall NtSetDefaultHardErrorPort(HANDLE DefaultHardErrorPort);
NTSTATUS __stdcall NtRaiseHardError(NTSTATUS ErrorStatus,
                                    ULONG NumberOfParameters,
                                    ULONG UnicodeStringParameterMask,
                                    ULONG *Parameters,
                                    ULONG ValidResponseOptions,
                                    ULONG *Response);

/* LUID.C -- locally-unique-id allocator.  LUID is cdef'd in nt.dll.se;
 * we leave the parameter opaque to avoid pulling se.lua into ex's cdef
 * (would create a load-order coupling).  The wrapper lazy-requires se
 * and casts. */
NTSTATUS __stdcall NtAllocateLocallyUniqueId(void *Luid);

/* DELAY.C / EX init -- direct write to HalDisplayString */
NTSTATUS __stdcall NtDisplayString(UNICODE_STRING *String);

/* NTXCAPI -- user-mode exception raise (lives next to EX in spirit).
 * Takes EXCEPTION_RECORD + CONTEXT pointers; left opaque here so this
 * module doesn't have to drag in the full CONTEXT cdef (architecture-
 * sensitive, ~700 bytes on x86 with floating-point state).  Callers
 * cast their cdata of the appropriate type to void * at the call site. */
NTSTATUS __stdcall NtRaiseException(void *ExceptionRecord,
                                    void *ContextRecord,
                                    unsigned char FirstChance);
]]

local M = {}

-- All six are the same shape; write one local maker, export six bindings
-- so callers keep distinct NtOpenX names (matches nt.dll.* convention
-- that sub-modules bind real ntdll exports, not abstractions).
local function make_opener(name)
    return function(access, oa)
        local h  = ffi.new('HANDLE[1]')
        local st = ntdll[name](h, access, oa)
        if err.is_error(st) then err.raise(name, st) end
        return handle.wrap(h[0])
    end
end

M.NtOpenEvent        = make_opener('NtOpenEvent')
M.NtOpenEventPair    = make_opener('NtOpenEventPair')
M.NtOpenSection      = make_opener('NtOpenSection')
M.NtOpenMutant       = make_opener('NtOpenMutant')
M.NtOpenSemaphore    = make_opener('NtOpenSemaphore')
M.NtOpenTimer        = make_opener('NtOpenTimer')
M.NtOpenIoCompletion = make_opener('NtOpenIoCompletion')

-- Query wrappers. Each basic-info class has a fixed struct; caller
-- supplies a pre-allocated cdata of the right type. NT 3.5's queries
-- expect Length to exactly match sizeof(struct).
local function make_query(name, struct_name)
    local struct_size = ffi.sizeof(struct_name)
    return function(h)
        local info = ffi.new(struct_name)
        local ret  = ffi.new('ULONG[1]')
        local st = ntdll[name](handle.raw(h), 0 --[[ BasicInformation ]],
                               info, struct_size, ret)
        if err.is_error(st) then err.raise(name, st) end
        return info
    end
end

M.NtQueryEvent     = make_query('NtQueryEvent',     'EVENT_BASIC_INFORMATION')
M.NtQueryMutant    = make_query('NtQueryMutant',    'MUTANT_BASIC_INFORMATION')
M.NtQuerySemaphore = make_query('NtQuerySemaphore', 'SEMAPHORE_BASIC_INFORMATION')
M.NtQueryTimer     = make_query('NtQueryTimer',     'TIMER_BASIC_INFORMATION')
M.NtQuerySection   = make_query('NtQuerySection',   'SECTION_BASIC_INFORMATION')

-- ------------------------------------------------------------------
-- Create / Release raw wrappers
-- ------------------------------------------------------------------

function M.NtCreateMutant(access, oa, initial_owner)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateMutant(h, access, oa,
                                    initial_owner and 1 or 0)
    if err.is_error(st) then err.raise('NtCreateMutant', st) end
    return handle.wrap(h[0])
end

-- Returns previous recursion count (useful for leak-checking).
function M.NtReleaseMutant(h)
    local prev = ffi.new('long[1]')
    local st = ntdll.NtReleaseMutant(handle.raw(h), prev)
    if err.is_error(st) then err.raise('NtReleaseMutant', st) end
    return prev[0]
end

function M.NtCreateSemaphore(access, oa, initial, maximum)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateSemaphore(h, access, oa, initial, maximum)
    if err.is_error(st) then err.raise('NtCreateSemaphore', st) end
    return handle.wrap(h[0])
end

-- Release `count` slots. Returns previous count.
function M.NtReleaseSemaphore(h, count)
    local prev = ffi.new('long[1]')
    local st = ntdll.NtReleaseSemaphore(handle.raw(h), count, prev)
    if err.is_error(st) then err.raise('NtReleaseSemaphore', st) end
    return prev[0]
end

function M.NtCreateTimer(access, oa, timer_type)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateTimer(h, access, oa, timer_type or 0)
    if err.is_error(st) then err.raise('NtCreateTimer', st) end
    return handle.wrap(h[0])
end

-- Set a timer's due time. `due_time` is a LARGE_INTEGER; build via
-- ke.timeout for relative or stamp QuadPart positive for absolute.
-- `apc_routine`/`apc_context` are nil unless the caller is wiring a
-- real APC — ffi.callback into Lua here is treacherous on
-- cross-thread delivery, and no-op for most compositor-style uses.
-- NT 3.5's NtSetTimer is one-shot only; there is no period parameter
-- (periodic timers arrived in NT 4.0).
--
-- Returns the previous signaled-state flag.
function M.NtSetTimer(h, due_time, apc_routine, apc_context)
    local prev = ffi.new('unsigned char[1]')
    local st = ntdll.NtSetTimer(handle.raw(h), due_time,
                                apc_routine, apc_context, prev)
    if err.is_error(st) then err.raise('NtSetTimer', st) end
    return prev[0] ~= 0
end

-- Returns the current signaled-state flag at cancel time.
function M.NtCancelTimer(h)
    local cur = ffi.new('unsigned char[1]')
    local st = ntdll.NtCancelTimer(handle.raw(h), cur)
    if err.is_error(st) then err.raise('NtCancelTimer', st) end
    return cur[0] ~= 0
end

-- ------------------------------------------------------------------
-- Lua-idiomatic wrappers
-- ------------------------------------------------------------------

-- ke is loaded lazily to avoid a module-load cycle (ke pulls ex in
-- via the Event wrapper if we're not careful). Only the timeout
-- helper is needed here; keep the fetch scoped to each method.
local function ke()
    return require('nt.dll.ke')
end

local MUTANT_ALL_ACCESS    = 0x1F0001
local SEMAPHORE_ALL_ACCESS = 0x1F0003
local TIMER_ALL_ACCESS     = 0x1F0003

-- -- Mutex --

local Mutex = {}
Mutex.__index = Mutex

-- Acquire the mutex. Returns true if acquired, false on timeout.
-- Seconds may be nil (infinite).
function Mutex:lock(seconds)
    local t = ke().timeout(seconds)
    local st = ke().NtWaitForSingleObject(self._h, false, t)
    return st == 0   -- STATUS_SUCCESS. STATUS_TIMEOUT = 0x102 → false.
end

function Mutex:unlock()  return M.NtReleaseMutant(self._h) end
function Mutex:handle()  return self._h end
function Mutex:close()
    if self._h then self._h:close(); self._h = nil end
end

-- Query state (via NtQueryMutant). Returns table with current_count,
-- owned_by_caller, abandoned.
function Mutex:query()
    local info = M.NtQueryMutant(self._h)
    return {
        current_count   = info.CurrentCount,
        owned_by_caller = info.OwnedByCaller  ~= 0,
        abandoned       = info.AbandonedState ~= 0,
    }
end

-- Factory. opts.owned makes the creator the initial owner.
function M.mutex(opts)
    opts = opts or {}
    local h = M.NtCreateMutant(opts.access or MUTANT_ALL_ACCESS,
                               opts.oa,
                               opts.owned or false)
    return setmetatable({ _h = h }, Mutex)
end

-- -- Semaphore --

local Semaphore = {}
Semaphore.__index = Semaphore

-- Acquire one slot. Returns true if acquired, false on timeout.
function Semaphore:acquire(seconds)
    local t = ke().timeout(seconds)
    local st = ke().NtWaitForSingleObject(self._h, false, t)
    return st == 0
end

-- Release `count` slots (default 1). Returns previous count.
function Semaphore:release(count)
    return M.NtReleaseSemaphore(self._h, count or 1)
end

function Semaphore:query()
    local info = M.NtQuerySemaphore(self._h)
    return { current = info.CurrentCount, maximum = info.MaximumCount }
end

function Semaphore:handle() return self._h end
function Semaphore:close()
    if self._h then self._h:close(); self._h = nil end
end

-- Factory. opts.initial = initial count (default 0), opts.maximum
-- = max count (required — there's no sensible default).
function M.semaphore(opts)
    opts = opts or {}
    if not opts.maximum then
        error("ex.semaphore: opts.maximum is required", 2)
    end
    local h = M.NtCreateSemaphore(opts.access or SEMAPHORE_ALL_ACCESS,
                                  opts.oa,
                                  opts.initial or 0,
                                  opts.maximum)
    return setmetatable({ _h = h }, Semaphore)
end

-- -- Timer --

local Timer = {}
Timer.__index = Timer

-- Set the timer. `due_seconds` = relative delay in seconds (or nil
-- for an infinite delay). NT 3.5's NtSetTimer is one-shot only.
function Timer:set(due_seconds)
    local due = ke().timeout(due_seconds)
    return M.NtSetTimer(self._h, due)
end

-- Returns true if the timer was signaled at cancel time.
function Timer:cancel() return M.NtCancelTimer(self._h) end

-- Wait for the timer to fire. Returns true if signaled, false on timeout.
function Timer:wait(seconds)
    local t = ke().timeout(seconds)
    local st = ke().NtWaitForSingleObject(self._h, false, t)
    return st == 0
end

function Timer:query()
    local info = M.NtQueryTimer(self._h)
    return {
        remaining_time = info.RemainingTime.QuadPart,
        signaled       = info.TimerState ~= 0,
    }
end

function Timer:handle() return self._h end
function Timer:close()
    if self._h then self._h:close(); self._h = nil end
end

-- Factory. opts.notify true → NotificationTimer (stays signaled);
-- false → SynchronizationTimer (auto-reset on wake).
function M.timer(opts)
    opts = opts or {}
    local timer_type = 0   -- NotificationTimer
    if opts.notify == false then timer_type = 1 end   -- SynchronizationTimer
    local h = M.NtCreateTimer(opts.access or TIMER_ALL_ACCESS,
                              opts.oa, timer_type)
    return setmetatable({ _h = h }, Timer)
end

M.Mutex     = Mutex
M.Semaphore = Semaphore
M.Timer     = Timer

-- ------------------------------------------------------------------
-- EventPair raw wrappers
-- ------------------------------------------------------------------

function M.NtCreateEventPair(access, oa)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateEventPair(h, access, oa)
    if err.is_error(st) then err.raise('NtCreateEventPair', st) end
    return handle.wrap(h[0])
end

-- Each of these is a single-HANDLE no-arg syscall; factor the common
-- raise-on-error shape behind a tiny maker.
local function event_pair_op(name)
    return function(h)
        local st = ntdll[name](handle.raw(h))
        if err.is_error(st) then err.raise(name, st) end
    end
end

M.NtSetHighEventPair        = event_pair_op('NtSetHighEventPair')
M.NtSetLowEventPair         = event_pair_op('NtSetLowEventPair')
M.NtWaitHighEventPair       = event_pair_op('NtWaitHighEventPair')
M.NtWaitLowEventPair        = event_pair_op('NtWaitLowEventPair')
M.NtSetHighWaitLowEventPair = event_pair_op('NtSetHighWaitLowEventPair')
M.NtSetLowWaitHighEventPair = event_pair_op('NtSetLowWaitHighEventPair')

-- -- EventPair object --
-- EventPair is a synchronization primitive with two internal events
-- ("high" and "low"). Classic use: two threads rendezvousing. Thread
-- A signals high and waits on low; thread B waits on high and signals
-- low. The Set{High,Low}Wait{Low,High}EventPair combined calls are
-- atomic — they prevent the wake-before-sleep race when the other
-- thread signals the side you're about to wait on.
--
-- Wait ops have no timeout parameter (kernel only exposes indefinite
-- waits). For timed waits, treat the underlying handle directly via
-- ke.NtWaitForSingleObject.

local EventPair = {}
EventPair.__index = EventPair

function EventPair:signal_high() M.NtSetHighEventPair(self._h) end
function EventPair:signal_low()  M.NtSetLowEventPair (self._h) end
function EventPair:wait_high()   M.NtWaitHighEventPair(self._h) end
function EventPair:wait_low()    M.NtWaitLowEventPair (self._h) end
function EventPair:signal_high_wait_low() M.NtSetHighWaitLowEventPair(self._h) end
function EventPair:signal_low_wait_high() M.NtSetLowWaitHighEventPair(self._h) end
function EventPair:handle() return self._h end
function EventPair:close()
    if self._h then self._h:close(); self._h = nil end
end

-- EventPair has its own access-mask constant in winnt.h — 0x1F0003 is
-- STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|3, the "all access" flavor.
local EVENT_PAIR_ALL_ACCESS = 0x1F0003

function M.event_pair(opts)
    opts = opts or {}
    local h = M.NtCreateEventPair(opts.access or EVENT_PAIR_ALL_ACCESS, opts.oa)
    return setmetatable({ _h = h }, EventPair)
end

M.EventPair = EventPair

-- ------------------------------------------------------------------
-- HARDERR.C -- hard-error routing.
--
-- NtRaiseHardError ships a HARDERROR_MSG (defined in nt.dll.lpc next
-- to PORT_MESSAGE) via LpcRequestWaitReplyPort to whatever process
-- registered the default port.  The high-level daemon plumbing lives
-- in nt.harderr; the raw Nt* surface lives here.
--
-- IMPORTANT: NtRaiseHardError must NEVER be called from the process
-- that called NtSetDefaultHardErrorPort with NT_ERROR(status) -- the
-- kernel's recursion guard (HARDERR.C:404-417) takes the
-- ExpSystemErrorHandler(CallShutdown=TRUE) path in that case and
-- halts the box via the qemu debug-exit port.  Cross-process is fine.
-- ------------------------------------------------------------------

-- Values from NTEXAPI.H typedef enum _HARDERROR_RESPONSE.
M.HARDERROR_RESPONSE = {
    RETURN_TO_CALLER = 0,
    NOT_HANDLED      = 1,
    ABORT            = 2,
    CANCEL           = 3,
    IGNORE           = 4,
    NO               = 5,
    OK               = 6,
    RETRY            = 7,
    YES              = 8,
}

-- Values from NTEXAPI.H typedef enum _HARDERROR_RESPONSE_OPTION.
M.HARDERROR_OPTION = {
    ABORT_RETRY_IGNORE = 0,
    OK                 = 1,
    OK_CANCEL          = 2,
    RETRY_CANCEL       = 3,
    YES_NO             = 4,
    YES_NO_CANCEL      = 5,
    SHUTDOWN_SYSTEM    = 6,
}

-- MAXIMUM_HARDERROR_PARAMETERS (NTEXAPI.H).
M.HARDERROR_MAX_PARAMETERS = 4

-- Register `port` (a handle from lpc.NtCreatePort) as the system-wide
-- default hard-error port.  After this, any process that calls
-- NtRaiseHardError without its own ExceptionPort lands here.
--
-- Requires SeTcbPrivilege (HARDERR.C:807).  Only one port may be set
-- per system; ExpReadyForErrors flips TRUE permanently.  See the note
-- above about the recursion guard.
function M.NtSetDefaultHardErrorPort(port)
    local st = ntdll.NtSetDefaultHardErrorPort(handle.raw(port))
    if err.is_error(st) then err.raise('NtSetDefaultHardErrorPort', st) end
end

-- Raise a hard error against the current process.  `status` is the
-- NTSTATUS to report; `parameters` is a Lua array of ULONGs (each may
-- be a scalar or a pointer to a UNICODE_STRING -- bit i of
-- `unicode_mask` flags Parameters[i] as a string pointer).
-- `valid_options` selects the HARDERROR_OPTION choices the receiver
-- may pick from.  Returns the HARDERROR_RESPONSE the receiver replied.
--
-- Caveat: from the daemon's own process with NT_ERROR(status) this
-- bugchecks the kernel (see module header).  Always exercise from a
-- spawned child process (see pkg/test/harderr_xproc.lua).
function M.NtRaiseHardError(status, parameters, unicode_mask, valid_options)
    local n = parameters and #parameters or 0
    if n > M.HARDERROR_MAX_PARAMETERS then
        error("NtRaiseHardError: too many parameters (" .. n .. " > "
              .. M.HARDERROR_MAX_PARAMETERS .. ")", 2)
    end
    local p = nil
    if n > 0 then
        p = ffi.new('ULONG[?]', n)
        for i = 1, n do p[i-1] = parameters[i] end
    end
    local response = ffi.new('ULONG[1]')
    local st = ntdll.NtRaiseHardError(status, n, unicode_mask or 0,
                                      p, valid_options or M.HARDERROR_OPTION.OK,
                                      response)
    if err.is_error(st) then err.raise('NtRaiseHardError', st) end
    return tonumber(response[0])
end

-- ------------------------------------------------------------------
-- LUID.C -- locally-unique-id allocator.
-- ------------------------------------------------------------------

-- Returns a freshly-allocated LUID (a cdata of type 'LUID' from
-- nt.dll.se).  Each call returns a monotonically-increasing 64-bit
-- value -- useful for unique handle IDs, cookies, log line numbers.
function M.NtAllocateLocallyUniqueId()
    local se = require('nt.dll.se')   -- lazy: LUID lives here
    local luid = ffi.new('LUID')
    local st = ntdll.NtAllocateLocallyUniqueId(luid)
    if err.is_error(st) then err.raise('NtAllocateLocallyUniqueId', st) end
    return luid
end

-- ------------------------------------------------------------------
-- DELAY.C / EX init -- direct write to HalDisplayString.
-- ------------------------------------------------------------------

-- Write `text` straight to the kernel's HAL display string -- the
-- same path KdpStub uses for boot-log output.  Unlike printf+stdio
-- this bypasses CRT buffering and the parent process's handle
-- redirection, so it's load-bearing for diagnostic output from
-- daemons that don't own a console.
function M.NtDisplayString(text)
    local ns = require('nt.dll.str').to_utf16(text)
    local st = ntdll.NtDisplayString(ffi.cast('UNICODE_STRING *', ns))
    if err.is_error(st) then err.raise('NtDisplayString', st) end
end

-- ------------------------------------------------------------------
-- NTXCAPI -- user-mode exception raise.
-- ------------------------------------------------------------------

-- Minimal raw binding.  Caller supplies cdata pointers to its own
-- EXCEPTION_RECORD / CONTEXT structures (cast to void * by the FFI).
-- Use this to exercise the KiDispatchException path from Lua; full
-- structured-exception wrappers can land in nt.dll.ke later if a
-- caller actually drives RaiseException for production.
function M.NtRaiseException(exception_record, context_record, first_chance)
    local st = ntdll.NtRaiseException(exception_record, context_record,
                                      first_chance and 1 or 0)
    if err.is_error(st) then err.raise('NtRaiseException', st) end
end

-- ------------------------------------------------------------------
-- IoCompletion raw wrappers
-- ------------------------------------------------------------------

function M.NtCreateIoCompletion(access, oa, concurrent_threads)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtCreateIoCompletion(h, access, oa,
                                          concurrent_threads or 0)
    if err.is_error(st) then err.raise('NtCreateIoCompletion', st) end
    return handle.wrap(h[0])
end

-- Dequeue a completion packet. Returns (key, apc, status, information),
-- or nil on timeout (STATUS_TIMEOUT passes through without raising).
-- `timeout` is a LARGE_INTEGER* (use ke.timeout(seconds) for relative
-- timeouts; nil blocks indefinitely).
function M.NtRemoveIoCompletion(h, timeout)
    local key = ffi.new('void *[1]')
    local apc = ffi.new('void *[1]')
    local iosb = ffi.new('IO_STATUS_BLOCK')
    local st = ntdll.NtRemoveIoCompletion(handle.raw(h), key, apc, iosb, timeout)
    local stu = err.normalize(st)
    if stu == 0x102 --[[ STATUS_TIMEOUT ]] then return nil end
    if err.is_error(st) then err.raise('NtRemoveIoCompletion', st) end
    return key[0], apc[0], iosb.Status, iosb.Information
end

-- Query the depth of the completion queue (pending packets). Only
-- IoCompletionBasicInformation exists on NT 3.5.
function M.NtQueryIoCompletion(h)
    local info = ffi.new('IO_COMPLETION_BASIC_INFORMATION')
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryIoCompletion(handle.raw(h),
                                         0 --[[ BasicInformation ]],
                                         info,
                                         ffi.sizeof('IO_COMPLETION_BASIC_INFORMATION'),
                                         ret)
    if err.is_error(st) then err.raise('NtQueryIoCompletion', st) end
    return info.Depth
end

-- -- IoCompletion object --
-- NT 3.5 can only receive completions via file-handle association
-- (NtSetInformationFile/FileCompletionInformation). The port itself
-- has no NtSetIoCompletion on this version — that's a NT 4.0 addition.
-- So the Lua surface here is Create / Query-depth / Remove-with-timeout.

local IoCompletion = {}
IoCompletion.__index = IoCompletion

-- Returns queue depth (LONG pending-packets count).
function IoCompletion:depth() return M.NtQueryIoCompletion(self._h) end

-- Dequeue one packet (with optional timeout in seconds; nil = infinite).
-- Returns (key, apc_context, status, information) or nil on timeout.
function IoCompletion:remove(seconds)
    local ke = require('nt.dll.ke')
    return M.NtRemoveIoCompletion(self._h, ke.timeout(seconds))
end

function IoCompletion:handle() return self._h end
function IoCompletion:close()
    if self._h then self._h:close(); self._h = nil end
end

local IO_COMPLETION_ALL_ACCESS = 0x1F0003

function M.iocompletion(opts)
    opts = opts or {}
    local h = M.NtCreateIoCompletion(
        opts.access or IO_COMPLETION_ALL_ACCESS,
        opts.oa,
        opts.concurrent_threads or 0)
    return setmetatable({ _h = h }, IoCompletion)
end

M.IoCompletion = IoCompletion

return M
