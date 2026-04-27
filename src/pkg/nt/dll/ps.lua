-- nt.dll.ps — Process / thread bindings. Maps to NTOS/PS.
--
-- Opens take CLIENT_ID (pid/tid pair) rather than namespace paths.
-- Creation goes through RtlCreateUserThread (Rtl wrapper over raw
-- NtCreateThread — handles stack alloc, CONTEXT and INITIAL_TEB so the
-- entry function receives its PVOID parameter in the standard __stdcall
-- calling convention).
--
-- WARNING — thread entries on native-NT do NOT auto-terminate on return.
-- RtlInitializeContext (rtl/i386/context.c) sets up [esp+4] = parameter
-- and [esp+0] = (uninitialized) return-address slot. If the entry's
-- final `ret` executes, EIP becomes 0 and the thread crashes with
-- STATUS_ACCESS_VIOLATION at NULL. Win32's kernel32 hides this by
-- wrapping entries in BaseThreadStartThunk → ExitThread; ntdll callers
-- get no such trampoline. Every entry must call NtTerminateThread before
-- returning, OR the caller must accept that the thread terminates via
-- unhandled-exception (visible as `UMODE EXC code=c0000005 eip=0` in the
-- kernel log; harmless but noisy).
--
-- Lifecycle:
--   ps.create_thread(entry, param, opts) → (NT_HANDLE, tid)
--   ps.NtTerminateThread(h, exit_status)
--   ps.NtSuspendThread(h) → previous_suspend_count
--   ps.NtResumeThread (h) → previous_suspend_count
--
-- Raw NtCreateThread + CONTEXT / INITIAL_TEB are also bridged for
-- callers that need register-level control; most should stick to
-- create_thread.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')
local handle = require('nt.dll.handle')

ffi.cdef[[
#pragma pack(push, 4)

typedef struct _FLOATING_SAVE_AREA {
    ULONG         ControlWord;
    ULONG         StatusWord;
    ULONG         TagWord;
    ULONG         ErrorOffset;
    ULONG         ErrorSelector;
    ULONG         DataOffset;
    ULONG         DataSelector;
    unsigned char RegisterArea[80];
    ULONG         Cr0NpxState;
} FLOATING_SAVE_AREA;

typedef struct _CONTEXT {
    ULONG ContextFlags;
    ULONG Dr0;
    ULONG Dr1;
    ULONG Dr2;
    ULONG Dr3;
    ULONG Dr6;
    ULONG Dr7;
    FLOATING_SAVE_AREA FloatSave;
    ULONG SegGs;
    ULONG SegFs;
    ULONG SegEs;
    ULONG SegDs;
    ULONG Edi;
    ULONG Esi;
    ULONG Ebx;
    ULONG Edx;
    ULONG Ecx;
    ULONG Eax;
    ULONG Ebp;
    ULONG Eip;
    ULONG SegCs;
    ULONG EFlags;
    ULONG Esp;
    ULONG SegSs;
} CONTEXT;

typedef struct _INITIAL_TEB {
    PVOID StackBase;
    PVOID StackLimit;
} INITIAL_TEB;

#pragma pack(pop)

NTSTATUS __stdcall NtOpenProcess(HANDLE *h, ULONG Access,
                                 OBJECT_ATTRIBUTES *oa, CLIENT_ID *cid);
NTSTATUS __stdcall NtOpenThread (HANDLE *h, ULONG Access,
                                 OBJECT_ATTRIBUTES *oa, CLIENT_ID *cid);

NTSTATUS __stdcall NtCreateThread(HANDLE *h, ULONG Access,
                                  OBJECT_ATTRIBUTES *oa,
                                  HANDLE Process, CLIENT_ID *cid,
                                  CONTEXT *ctx, INITIAL_TEB *teb,
                                  unsigned char CreateSuspended);

NTSTATUS __stdcall NtTerminateProcess(HANDLE h, NTSTATUS ExitStatus);
NTSTATUS __stdcall NtTerminateThread (HANDLE h, NTSTATUS ExitStatus);
NTSTATUS __stdcall NtSuspendThread   (HANDLE h, ULONG *PreviousCount);
NTSTATUS __stdcall NtResumeThread    (HANDLE h, ULONG *PreviousCount);

/* RtlCreateUserThread bundles stack alloc + CONTEXT + INITIAL_TEB +
 * RtlUserThreadStart trampoline. Almost always what you want; raw
 * NtCreateThread is only needed when you're controlling registers
 * directly (e.g. injecting a thread with a manual GDI call site). */
NTSTATUS __stdcall RtlCreateUserThread(
    HANDLE Process,
    void *ThreadSecurityDescriptor,
    unsigned char CreateSuspended,
    ULONG ZeroBits,
    ULONG MaximumStackSize,
    ULONG CommittedStackSize,
    void *StartAddress,
    void *Parameter,
    HANDLE *Thread,
    CLIENT_ID *ClientId);
]]

local M = {}

-- Thread access-mask shortcuts (STANDARD_RIGHTS_REQUIRED bits plus
-- per-object specifics). THREAD_ALL_ACCESS is the common choice from
-- the creator's side.
M.THREAD_TERMINATE              = 0x0001
M.THREAD_SUSPEND_RESUME         = 0x0002
M.THREAD_GET_CONTEXT            = 0x0008
M.THREAD_SET_CONTEXT            = 0x0010
M.THREAD_QUERY_INFORMATION      = 0x0040
M.THREAD_SET_INFORMATION        = 0x0020
M.THREAD_SET_THREAD_TOKEN       = 0x0080
M.THREAD_IMPERSONATE            = 0x0100
M.THREAD_DIRECT_IMPERSONATION   = 0x0200
M.THREAD_ALL_ACCESS             = 0x001F03FF

-- Pseudo-handles (NtCurrentProcess / NtCurrentThread). Kernel
-- recognizes these as "operate on the caller" — never leave the
-- process boundary.
local CURRENT_PROCESS = ffi.cast('HANDLE', ffi.cast('intptr_t', -1))
local CURRENT_THREAD  = ffi.cast('HANDLE', ffi.cast('intptr_t', -2))
M.NtCurrentProcess = function() return CURRENT_PROCESS end
M.NtCurrentThread  = function() return CURRENT_THREAD  end

local function proc_or_current(h)
    if h == nil then return CURRENT_PROCESS end
    return handle.raw(h)
end

function M.NtOpenProcess(access, oa, cid)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenProcess(h, access, oa, cid)
    if err.is_error(st) then err.raise('NtOpenProcess', st) end
    return handle.wrap(h[0])
end

function M.NtOpenThread(access, oa, cid)
    local h = ffi.new('HANDLE[1]')
    local st = ntdll.NtOpenThread(h, access, oa, cid)
    if err.is_error(st) then err.raise('NtOpenThread', st) end
    return handle.wrap(h[0])
end

-- Build a minimal OBJECT_ATTRIBUTES with only Length populated — what
-- CLIENT_ID-based opens require.
function M.empty_oa()
    local oa = ffi.new('OBJECT_ATTRIBUTES')
    oa.Length = ffi.sizeof('OBJECT_ATTRIBUTES')
    return oa
end

-- ------------------------------------------------------------------
-- Thread lifecycle
-- ------------------------------------------------------------------

-- High-level thread creator. Returns (NT_HANDLE, tid). `entry` is a
-- native function pointer with signature `ULONG __stdcall fn(HANDLE)` —
-- typically either an ffi.C export from rt/ or an ntdll export
-- (accessed via require('nt.dll').NtFoo) whose shape matches.
--
-- `handle_param` is an NT_HANDLE the thread operates on (the signal-
-- target for an Event thread, the EventPair the thread rendezvouses
-- on, etc.) — or nil if the entry doesn't need a handle. Raw pointer
-- params are NOT supported: the thread-entry ABI here is "operate on
-- an NT object", which is always an NT_HANDLE. Passing anything else
-- raises.
--
-- `opts` table (all optional):
--   .stack_max      = reserved stack size (default 64K)
--   .stack_commit   = initial committed stack (default 8K)
--   .suspended      = create suspended (caller ResumeThread later)
--   .access         = desired access mask (default THREAD_ALL_ACCESS)
--   .process        = target process handle (default current process)
--
-- On return, the new thread is either already running (suspended=false,
-- the default) or waiting at its entry point until ps.NtResumeThread.
-- The entry MUST call NtTerminateThread before returning (see WARNING
-- at the top of this file) — direct ntdll thread creation has no
-- ExitThread wrapper. Letting the entry fall through `ret` produces a
-- harmless-but-noisy STATUS_ACCESS_VIOLATION at EIP=0 in the log.
function M.create_thread(entry, handle_param, opts)
    opts = opts or {}
    local raw_param
    if handle_param == nil then
        raw_param = nil
    elseif ffi.istype('NT_HANDLE', handle_param) then
        raw_param = ffi.cast('void *', handle.raw(handle_param))
    else
        error("create_thread: handle_param must be nil or NT_HANDLE, got "
              .. tostring(handle_param), 2)
    end
    local h   = ffi.new('HANDLE[1]')
    local cid = ffi.new('CLIENT_ID')
    local st = ntdll.RtlCreateUserThread(
        proc_or_current(opts.process),
        nil,                                       -- no security descriptor
        opts.suspended and 1 or 0,
        0,                                         -- ZeroBits
        opts.stack_max    or 0x10000,              -- 64K reserved
        opts.stack_commit or 0x2000,               -- 8K committed
        entry,
        raw_param,
        h,
        cid)
    if err.is_error(st) then err.raise('RtlCreateUserThread', st) end
    local tid = tonumber(ffi.cast('intptr_t', cid.UniqueThread))
    return handle.wrap(h[0]), tid
end

function M.NtTerminateThread(h, exit_status)
    local raw = (h == nil) and CURRENT_THREAD or handle.raw(h)
    local st = ntdll.NtTerminateThread(raw, exit_status or 0)
    if err.is_error(st) then err.raise('NtTerminateThread', st) end
end

-- Terminate a process. nil h means the current process (-1 pseudo-handle);
-- the syscall does not return in that case. exit_status defaults to 0.
function M.NtTerminateProcess(h, exit_status)
    local raw = (h == nil) and CURRENT_PROCESS or handle.raw(h)
    local st = ntdll.NtTerminateProcess(raw, exit_status or 0)
    if err.is_error(st) then err.raise('NtTerminateProcess', st) end
end

-- Returns the thread's previous suspend count.
function M.NtSuspendThread(h)
    local prev = ffi.new('ULONG[1]')
    local st = ntdll.NtSuspendThread(handle.raw(h), prev)
    if err.is_error(st) then err.raise('NtSuspendThread', st) end
    return prev[0]
end

function M.NtResumeThread(h)
    local prev = ffi.new('ULONG[1]')
    local st = ntdll.NtResumeThread(handle.raw(h), prev)
    if err.is_error(st) then err.raise('NtResumeThread', st) end
    return prev[0]
end

return M
