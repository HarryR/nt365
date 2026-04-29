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
local layout = require('nt.dll.layout')
local oa     = require('nt.dll.oa')
local str    = require('nt.dll.str')

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

/* Process spawn — RtlCreateUserProcess wraps the section + NtCreateProcess
 * + initial-thread setup ladder.  Caller passes an already-built
 * RTL_USER_PROCESS_PARAMETERS block (built by RtlCreateProcessParameters).
 * The block layout has CURDIR / RTL_DRIVE_LETTER_CURDIR / STRING fields we
 * don't need to expose, but we DO need to poke std-handle fields after
 * the create-params call (RtlCreateProcessParameters copies them from the
 * caller's PEB; we then mark them inheritable so the kernel-side process
 * create dups them into the child's handle table).  Layout up through
 * StandardError is enough — anything past that we treat as opaque. */
typedef struct _RTL_USER_PROCESS_PARAMETERS_HEADER {
    ULONG  MaximumLength;
    ULONG  Length;
    ULONG  Flags;
    ULONG  DebugFlags;
    HANDLE ConsoleHandle;
    ULONG  ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;
    /* CURDIR / DllPath / ImagePathName / ... follow but we don't expose. */
} RTL_USER_PROCESS_PARAMETERS_HEADER;

/* Opaque full-struct alias for the ntdll API surface — callers pass
 * pointers, never deref directly (use the _HEADER cast above). */
typedef struct _RTL_USER_PROCESS_PARAMETERS RTL_USER_PROCESS_PARAMETERS;

typedef struct _OBJECT_HANDLE_FLAG_INFORMATION {
    unsigned char Inherit;
    unsigned char ProtectFromClose;
} OBJECT_HANDLE_FLAG_INFORMATION;

NTSTATUS __stdcall NtSetInformationObject(
    HANDLE Handle,
    int    ObjectInformationClass,
    void  *ObjectInformation,
    ULONG  ObjectInformationLength);

typedef struct _SECTION_IMAGE_INFORMATION {
    PVOID  TransferAddress;
    ULONG  ZeroBits;
    ULONG  MaximumStackSize;
    ULONG  CommittedStackSize;
    ULONG  SubSystemType;
    USHORT SubSystemMinorVersion;
    USHORT SubSystemMajorVersion;
    ULONG  GpValue;
    USHORT ImageCharacteristics;
    USHORT DllCharacteristics;
    USHORT Machine;
    unsigned char ImageContainsCode;
    unsigned char Spare1;
    ULONG  LoaderFlags;
    ULONG  Reserved[2];
} SECTION_IMAGE_INFORMATION;

typedef struct _RTL_USER_PROCESS_INFORMATION {
    ULONG     Length;
    HANDLE    Process;
    HANDLE    Thread;
    CLIENT_ID ClientId;
    SECTION_IMAGE_INFORMATION ImageInformation;
} RTL_USER_PROCESS_INFORMATION;

NTSTATUS __stdcall RtlCreateProcessParameters(
    RTL_USER_PROCESS_PARAMETERS **ProcessParameters,
    UNICODE_STRING *ImagePathName,
    UNICODE_STRING *DllPath,
    UNICODE_STRING *CurrentDirectory,
    UNICODE_STRING *CommandLine,
    PVOID Environment,
    UNICODE_STRING *WindowTitle,
    UNICODE_STRING *DesktopInfo,
    UNICODE_STRING *ShellInfo,
    UNICODE_STRING *RuntimeData);

NTSTATUS __stdcall RtlDestroyProcessParameters(
    RTL_USER_PROCESS_PARAMETERS *ProcessParameters);

NTSTATUS __stdcall RtlCreateUserProcess(
    UNICODE_STRING *NtImagePathName,
    ULONG Attributes,
    RTL_USER_PROCESS_PARAMETERS *ProcessParameters,
    PVOID ProcessSecurityDescriptor,
    PVOID ThreadSecurityDescriptor,
    HANDLE ParentProcess,
    unsigned char InheritHandles,
    HANDLE DebugPort,
    HANDLE ExceptionPort,
    RTL_USER_PROCESS_INFORMATION *ProcessInformation);

/* NtQueryInformationProcess(ProcessBasicInformation = 0). The PROCESS_BASIC_INFORMATION
 * fields are: ExitStatus, PebBaseAddress, AffinityMask, BasePriority,
 * UniqueProcessId, InheritedFromUniqueProcessId. */
typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PVOID    PebBaseAddress;
    ULONG    AffinityMask;
    long     BasePriority;
    ULONG    UniqueProcessId;
    ULONG    InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION;

NTSTATUS __stdcall NtQueryInformationProcess(
    HANDLE ProcessHandle,
    int    ProcessInformationClass,
    void  *ProcessInformation,
    ULONG  ProcessInformationLength,
    ULONG *ReturnLength);
]]

-- NT 3.5 OS structs use #pragma pack(2): the leading BOOLEAN
-- (UCHAR-sized) is followed by a HANDLE that lands at offset 2, not
-- 4.  Without pack(2), reading PEB.ProcessParameters at the natural
-- offset (0x10) gets the upper half of the pointer plus the low half
-- of SubSystemData — silent garbage.  Layout self-check below catches
-- any future drift.
ffi.cdef[[
#pragma pack(push, 2)
typedef struct _PEB_HEAD {
    UCHAR  InheritedAddressSpace;
    HANDLE Mutant;
    PVOID  ImageBaseAddress;
    PVOID  Ldr;
    void  *ProcessParameters;
} PEB_HEAD;
#pragma pack(pop)
]]

layout.check_offsets {
    PEB_HEAD = {
        InheritedAddressSpace = 0x00,
        Mutant                = 0x02,
        ImageBaseAddress      = 0x06,
        Ldr                   = 0x0A,
        ProcessParameters     = 0x0E,
    },
    -- All-4-byte fields, natural alignment matches pack(2), but we
    -- still assert in case a future cdef tweak shifts something.
    RTL_USER_PROCESS_PARAMETERS_HEADER = {
        StandardInput  = 0x18,
        StandardOutput = 0x1C,
        StandardError  = 0x20,
    },
    PROCESS_BASIC_INFORMATION = {
        ExitStatus     = 0x00,
        PebBaseAddress = 0x04,
        _size          = 24,
    },
}

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

-- ------------------------------------------------------------------
-- Process spawn (RtlCreateUserProcess)
-- ------------------------------------------------------------------
--
-- Higher-level spawn: image_path + command_line in UTF-8 → handles
-- to a new process whose initial thread is created suspended.  Caller
-- must NtResumeThread to start it running, then NtWaitForSingleObject
-- on the process handle to wait for exit.
--
-- Std handles + environment + DllPath inherit from the calling process
-- by default.  Explicit overrides via opts will land here when we need
-- pipe-based stdio capture; for the smoke-test pass we just inherit.
--
-- The RTL_USER_PROCESS_PARAMETERS block built by RtlCreateProcessParameters
-- is callee-allocated; we have to pair every successful create with a
-- matching RtlDestroyProcessParameters or leak heap.  pcall the
-- second call so a mid-flight error in CreateUser doesn't strand it.

local str = require('nt.dll.str')

-- ------------------------------------------------------------------
-- OBJECT_INFORMATION_CLASS values used here.  Promote to module-level
-- exports if a second consumer arrives.
-- ------------------------------------------------------------------
local ObjectHandleFlagInformation = 4

-- Mark a raw HANDLE inheritable so InheritHandles=TRUE on the
-- subsequent RtlCreateUserProcess actually duplicates it into the
-- child's handle table.  Tolerates failure (some pseudo-handles
-- like INVALID_HANDLE_VALUE / -1 reject the call) — best-effort.
local function mark_inheritable(raw_handle)
    if raw_handle == nil then return end
    local hi = ffi.new('OBJECT_HANDLE_FLAG_INFORMATION')
    hi.Inherit          = 1
    hi.ProtectFromClose = 0
    ntdll.NtSetInformationObject(
        raw_handle, ObjectHandleFlagInformation,
        hi, ffi.sizeof('OBJECT_HANDLE_FLAG_INFORMATION'))
end

-- Read NtCurrentPeb()->ProcessParameters->Standard{Input,Output,Error}
-- as raw HANDLEs.  Internal use — defaulted into spawn's params block.
-- NT 3.5's RtlCreateProcessParameters does NOT auto-copy stdio (it
-- copies DllPath / CurrentDirectory / Environment / CommandLine, but
-- leaves stdio fields zero), and RtlCreateUserProcess's dup loop
-- (RTLEXEC.C:949-995) only dups when those fields are non-zero — so
-- if we want stdio inheritance we must fill them ourselves.
local function read_parent_stdio_raw()
    local pbi = ffi.new('PROCESS_BASIC_INFORMATION')
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryInformationProcess(
        CURRENT_PROCESS, M.ProcessBasicInformation, pbi,
        ffi.sizeof('PROCESS_BASIC_INFORMATION'), ret)
    if err.is_error(st) then
        err.raise('NtQueryInformationProcess(self)', st)
    end
    local peb = ffi.cast('PEB_HEAD *', pbi.PebBaseAddress)
    local pp  = ffi.cast('RTL_USER_PROCESS_PARAMETERS_HEADER *',
                         peb.ProcessParameters)
    if pp == nil then return nil, nil, nil end
    return pp.StandardInput, pp.StandardOutput, pp.StandardError
end

-- Public: parent's stdio as borrowed NT_HANDLEs.  :close() and __gc
-- are no-ops on borrowed handles — we don't own these, the parent
-- (boot-efi loader → init process) does.  Useful for diagnostics
-- and any caller wanting symmetric NT_HANDLE handling.
function M.parent_stdio()
    local raw_in, raw_out, raw_err = read_parent_stdio_raw()
    return handle.borrow(raw_in),
           handle.borrow(raw_out),
           handle.borrow(raw_err)
end

-- ------------------------------------------------------------------
-- RtlCreateUserProcess — low-level wrapper.  Mirrors the NT API
-- one-to-one but with Lua-friendly defaults: parent's stdio is
-- copied into the params block (overridable), CommandLine is never
-- inherited from parent (always derived from explicit arg or
-- image_path), DllPath / CurrentDirectory / Environment fall back
-- to ntdll's "copy from parent's PEB" defaults.
--
-- For most callers, use the higher-level `spawn` instead.  This
-- function exists so tests can exercise the raw shape of the
-- ntdll API without ps.spawn's defaulting in the way.
-- ------------------------------------------------------------------
function M.RtlCreateUserProcess(image_path, command_line, opts)
    opts = opts or {}
    local ipath = str.to_utf16(image_path)
    -- CommandLine is *never* defaulted from parent's PEB — would leak
    -- our parent's argv into the child.  Always explicit-or-image_path.
    local cline = str.to_utf16(command_line or image_path)

    local pp_out = ffi.new('RTL_USER_PROCESS_PARAMETERS *[1]')
    local st = ntdll.RtlCreateProcessParameters(
        pp_out,
        ipath.us,           -- ImagePathName
        nil,                -- DllPath          (nil → ntdll default)
        nil,                -- CurrentDirectory (nil → parent's PEB)
        cline.us,           -- CommandLine
        nil,                -- Environment      (nil → parent's PEB)
        nil, nil, nil, nil) -- WindowTitle / Desktop / Shell / Runtime
    if err.is_error(st) then err.raise('RtlCreateProcessParameters', st) end

    -- finally-guard: whatever happens between here and the destroy
    -- call, RtlDestroyProcessParameters runs.  Mirrors the
    -- se.with_privileges pattern.
    local ok, ret = pcall(function()
        local p_in, p_out, p_err = read_parent_stdio_raw()
        local hdr = ffi.cast('RTL_USER_PROCESS_PARAMETERS_HEADER *', pp_out[0])
        hdr.StandardInput  = opts.stdin  and handle.raw(opts.stdin)  or p_in
        hdr.StandardOutput = opts.stdout and handle.raw(opts.stdout) or p_out
        hdr.StandardError  = opts.stderr and handle.raw(opts.stderr) or p_err
        mark_inheritable(hdr.StandardInput)
        mark_inheritable(hdr.StandardOutput)
        mark_inheritable(hdr.StandardError)

        local info = ffi.new('RTL_USER_PROCESS_INFORMATION')
        info.Length = ffi.sizeof('RTL_USER_PROCESS_INFORMATION')

        local inherit = opts.inherit_handles ~= false   -- default true
        local create_st = ntdll.RtlCreateUserProcess(
            ipath.us,
            oa.OBJ_CASE_INSENSITIVE,
            pp_out[0],
            nil,                          -- ProcessSecurityDescriptor
            nil,                          -- ThreadSecurityDescriptor
            CURRENT_PROCESS,              -- ParentProcess (self pseudo-handle)
            inherit and 1 or 0,
            nil,                          -- DebugPort
            nil,                          -- ExceptionPort
            info)
        if err.is_error(create_st) then
            err.raise('RtlCreateUserProcess', create_st)
        end

        return {
            process = handle.wrap(info.Process),
            thread  = handle.wrap(info.Thread),
            pid     = tonumber(ffi.cast('intptr_t', info.ClientId.UniqueProcess)),
            tid     = tonumber(ffi.cast('intptr_t', info.ClientId.UniqueThread)),
            image   = {
                entry            = info.ImageInformation.TransferAddress,
                stack_max        = info.ImageInformation.MaximumStackSize,
                stack_committed  = info.ImageInformation.CommittedStackSize,
                subsystem        = info.ImageInformation.SubSystemType,
                machine          = info.ImageInformation.Machine,
            },
        }
    end)

    -- Always destroy.  pcall it so a destroy-side error doesn't
    -- mask the body's error (defensive — RtlDestroy shouldn't raise
    -- under normal circumstances).
    pcall(ntdll.RtlDestroyProcessParameters, pp_out[0])

    if not ok then error(ret, 0) end
    return ret
end

-- ------------------------------------------------------------------
-- spawn — Lua-idiomatic wrapper over RtlCreateUserProcess.
-- ------------------------------------------------------------------
--
-- Two call shapes:
--
--   ps.spawn("\\SystemRoot\\pkg\\msvc20\\ML.EXE")          -- shorthand
--   ps.spawn{ exe = "...", cmdline = "ML.EXE /?" }         -- full opts
--
-- Required:
--   exe       NT path to the image (UTF-8 Lua string).
--
-- Optional:
--   cmdline   Command line as a single string.  Defaults to `exe`
--             so argv[0] is the EXE name (NEVER inherits parent's
--             cmdline — that would leak our argv into the child).
--   stdin/    Borrowed or owned NT_HANDLE.  Defaults to parent's
--   stdout/     PEB std handle (read via parent_stdio).  Pass an
--   stderr      explicit handle (e.g. a pipe write-end) to redirect.
--   inherit_handles  Bool, default true.  Whether the kernel
--                    duplicates parent's inheritable handles into
--                    the child during NtCreateProcess.
--
-- Returns the same table RtlCreateUserProcess returns:
--   { process, thread, pid, tid, image = { entry, ... } }
-- The thread starts SUSPENDED — caller must NtResumeThread to run it.
function M.spawn(opts)
    if type(opts) == 'string' then opts = { exe = opts } end
    if type(opts) ~= 'table' or not opts.exe then
        error("ps.spawn: opts.exe (NT path to image) is required", 2)
    end
    return M.RtlCreateUserProcess(opts.exe, opts.cmdline, opts)
end

-- Read PROCESS_BASIC_INFORMATION (info class 0).  Returns a Lua table
-- with the fields decoded.  Most-used field is `exit_status`, which
-- after NtWaitForSingleObject(process) holds the process's exit code
-- (or STATUS_PENDING if the process is still running).
M.ProcessBasicInformation = 0

function M.NtQueryInformationProcess_Basic(h)
    local pbi = ffi.new('PROCESS_BASIC_INFORMATION')
    local ret = ffi.new('ULONG[1]')
    local st = ntdll.NtQueryInformationProcess(
        handle.raw(h), M.ProcessBasicInformation,
        pbi, ffi.sizeof('PROCESS_BASIC_INFORMATION'), ret)
    if err.is_error(st) then err.raise('NtQueryInformationProcess', st) end
    return {
        exit_status      = pbi.ExitStatus,
        peb              = pbi.PebBaseAddress,
        affinity_mask    = pbi.AffinityMask,
        base_priority    = pbi.BasePriority,
        pid              = pbi.UniqueProcessId,
        ppid             = pbi.InheritedFromUniqueProcessId,
    }
end

return M
