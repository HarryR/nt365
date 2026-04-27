-- nt.dll.sys — NtQuerySystemInformation. System-wide introspection:
-- process list, handle list, module list, performance counters.
--
-- For now we only bridge SystemProcessInformation (class 5) — the
-- canonical path taskmgr / perfmon / PSAPI take under the hood.
--
-- SystemProcessInformation returns a linked list in one flat buffer:
-- each SYSTEM_PROCESS_INFORMATION is variable-length (the fixed header
-- is followed by SYSTEM_THREAD_INFORMATION[NumberOfThreads], and the
-- UNICODE_STRING ImageName's Buffer points into the same buffer).
-- Walk via NextEntryOffset; 0 marks the last entry.
--
-- Size-query: we don't know the full size up-front (process count can
-- race between calls). Start at 32KB, double on STATUS_INFO_LENGTH_MISMATCH
-- until it fits or we exceed a safety cap.
--
-- NT 3.5's SYSTEM_PROCESS_INFORMATION is smaller than modern Windows's
-- (no SessionId, no IoCounters, no VM counters) — we stop at HandleCount
-- which is the last field guaranteed present. Later Windows versions
-- just leave the extra fields we don't read at the end of the record,
-- still reachable via NextEntryOffset.

local ffi    = require('ffi')
local ntdll  = require('nt.dll')
local err    = require('nt.dll.errors')

-- Layout exact-match for NT 3.5 (from NT/PUBLIC/SDK/INC/NTEXAPI.H). No
-- HandleCount field — the one in later Windows versions doesn't exist
-- yet in 3.5; use NtQueryObject on a Process handle for live handle
-- counts. NumberOfThreads is followed by SYSTEM_THREAD_INFORMATION
-- repeated NumberOfThreads times, then padding to the next process at
-- offset NextEntryOffset from the record start.
-- Pack(4): NT 3.5 was built with MSC 8.00, which treated `long long`
-- as a struct-of-two-longs with 4-byte alignment — so LARGE_INTEGER is
-- 4-aligned in NT 3.5 binaries even though the kernel passed /Zp8.
-- Modern compilers (LuaJIT's FFI default on x86 is already /Zp4, but
-- a later LuaJIT or x64 build would be /Zp8) would 8-align it and tail-
-- pad SYSTEM_THREAD_INFORMATION from 60 to 64 bytes, causing thread-
-- array stride drift after thread[0]. Force pack(4) to match NT 3.5's
-- actual layout.
ffi.cdef[[
#pragma pack(push, 4)
typedef long KPRIORITY;

typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG          NextEntryOffset;
    ULONG          NumberOfThreads;
    LARGE_INTEGER  ReadTransferCount;
    LARGE_INTEGER  WriteTransferCount;
    LARGE_INTEGER  OtherTransferCount;
    LARGE_INTEGER  CreateTime;
    LARGE_INTEGER  UserTime;
    LARGE_INTEGER  KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY      BasePriority;
    HANDLE         UniqueProcessId;
    HANDLE         InheritedFromUniqueProcessId;
    ULONG          ReadOperationCount;
    ULONG          WriteOperationCount;
    ULONG          OtherOperationCount;
    ULONG          PeakVirtualSize;
    ULONG          VirtualSize;
    ULONG          PageFaultCount;
    ULONG          PeakWorkingSetSize;
    ULONG          WorkingSetSize;
    ULONG          QuotaPeakPagedPoolUsage;
    ULONG          QuotaPagedPoolUsage;
    ULONG          QuotaPeakNonPagedPoolUsage;
    ULONG          QuotaNonPagedPoolUsage;
    ULONG          PagefileUsage;
    ULONG          PeakPagefileUsage;
    ULONG          PrivatePageCount;
} SYSTEM_PROCESS_INFORMATION;

typedef struct _SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG         WaitTime;
    void *        StartAddress;
    CLIENT_ID     ClientId;
    KPRIORITY     Priority;
    long          BasePriority;
    ULONG         ContextSwitches;
    ULONG         ThreadState;
    ULONG         WaitReason;
} SYSTEM_THREAD_INFORMATION;

typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE         Section;
    void *         MappedBase;
    void *         ImageBase;
    ULONG          ImageSize;
    ULONG          Flags;
    USHORT         LoadOrderIndex;
    USHORT         InitOrderIndex;
    USHORT         LoadCount;
    USHORT         OffsetToFileName;
    unsigned char  FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES;
#pragma pack(pop)

NTSTATUS __stdcall NtQuerySystemInformation(
    int SystemInformationClass,
    void *SystemInformation,
    ULONG SystemInformationLength,
    ULONG *ReturnLength);
]]

local SystemProcessInformation    = 5
local SystemModuleInformation     = 11
local STATUS_INFO_LENGTH_MISMATCH = 0xC0000004

local M = {}

-- Generic size-query wrapper. Returns a char[?] buffer sized to hold
-- whatever the given info class produced; caller keeps the buffer
-- alive while pointer-walking into it.
local function query_buffer(info_class, initial_size)
    local size = initial_size
    local buf  = ffi.new('char[?]', size)
    local ret  = ffi.new('ULONG[1]')
    for _ = 1, 10 do
        local st = ntdll.NtQuerySystemInformation(info_class, buf, size, ret)
        local stu = err.normalize(st)
        if stu == STATUS_INFO_LENGTH_MISMATCH then
            -- Grow to max(2*size, reported-required). Reported size can
            -- still be short of the next attempt's need (list races) so
            -- doubling gives slack.
            local needed = ret[0]
            size = needed > size and needed * 2 or size * 2
            buf  = ffi.new('char[?]', size)
        elseif err.is_error(st) then
            err.raise('NtQuerySystemInformation', st)
        else
            return buf, ret[0]
        end
    end
    error('NtQuerySystemInformation: buffer did not converge after 10 grows')
end

local str = require('nt.dll.str')

local function handle_to_int(h) return tonumber(ffi.cast('intptr_t', h)) end

-- Copy one SYSTEM_PROCESS_INFORMATION + its trailing
-- SYSTEM_THREAD_INFORMATION[] into plain Lua tables. After this
-- returns the kernel buffer can be freed at any time — no cdata
-- references leak out.
local function copy_process(info_ptr, threads_ptr)
    -- str.from_utf16 is null-pointer tolerant (returns "") so this
    -- just falls through to a "(System)" label for the kernel idle /
    -- System pseudo-processes which have no image path.
    local image = str.from_utf16(info_ptr.ImageName)
    if image == "" then image = "(System)" end
    local p = {
        pid              = handle_to_int(info_ptr.UniqueProcessId),
        parent_pid       = handle_to_int(info_ptr.InheritedFromUniqueProcessId),
        image            = image,
        priority         = info_ptr.BasePriority,
        thread_count     = info_ptr.NumberOfThreads,
        create_time      = info_ptr.CreateTime.QuadPart,
        user_time        = info_ptr.UserTime.QuadPart,
        kernel_time      = info_ptr.KernelTime.QuadPart,
        virtual_size     = info_ptr.VirtualSize,
        peak_virtual     = info_ptr.PeakVirtualSize,
        working_set      = info_ptr.WorkingSetSize,
        peak_working_set = info_ptr.PeakWorkingSetSize,
        page_faults      = info_ptr.PageFaultCount,
        paged_pool       = info_ptr.QuotaPagedPoolUsage,
        non_paged_pool   = info_ptr.QuotaNonPagedPoolUsage,
        pagefile         = info_ptr.PagefileUsage,
        peak_pagefile    = info_ptr.PeakPagefileUsage,
        private_pages    = info_ptr.PrivatePageCount,
        io_read_ops      = info_ptr.ReadOperationCount,
        io_write_ops     = info_ptr.WriteOperationCount,
        io_other_ops     = info_ptr.OtherOperationCount,
        io_read_bytes    = info_ptr.ReadTransferCount.QuadPart,
        io_write_bytes   = info_ptr.WriteTransferCount.QuadPart,
        io_other_bytes   = info_ptr.OtherTransferCount.QuadPart,
        threads          = {},
    }
    for i = 0, p.thread_count - 1 do
        local t = threads_ptr[i]
        p.threads[i+1] = {
            tid              = handle_to_int(t.ClientId.UniqueThread),
            pid              = handle_to_int(t.ClientId.UniqueProcess),
            start_address    = tonumber(ffi.cast('uintptr_t', t.StartAddress)),
            priority         = t.Priority,
            base_priority    = t.BasePriority,
            context_switches = t.ContextSwitches,
            thread_state     = t.ThreadState,
            wait_reason      = t.WaitReason,
            wait_time        = t.WaitTime,
            kernel_time      = t.KernelTime.QuadPart,
            user_time        = t.UserTime.QuadPart,
            create_time      = t.CreateTime.QuadPart,
        }
    end
    return p
end

-- Iterate all processes. Yields a Lua table per process — all fields
-- and threads copied out of the kernel buffer at yield time so the
-- caller may retain yielded values indefinitely without any cdata
-- lifetime concerns.
function M.each_process()
    local buf = query_buffer(SystemProcessInformation, 32768)
    local proc_size = ffi.sizeof('SYSTEM_PROCESS_INFORMATION')
    return coroutine.wrap(function()
        local ptr = ffi.cast('char *', buf)
        while true do
            local info    = ffi.cast('SYSTEM_PROCESS_INFORMATION *', ptr)
            local threads = ffi.cast('SYSTEM_THREAD_INFORMATION *',
                                      ptr + proc_size)
            coroutine.yield(copy_process(info, threads))
            if info.NextEntryOffset == 0 then return end
            ptr = ptr + info.NextEntryOffset
        end
    end)
end

-- Copy one RTL_PROCESS_MODULE_INFORMATION into a Lua table. FullPathName
-- is an in-struct char[256] (ANSI), so ffi.string on its address reads
-- up to the first NUL regardless of whether the buffer outlives.
local function copy_module(m)
    local full_ptr = ffi.cast('char *', m.FullPathName)
    return {
        image_path  = ffi.string(full_ptr),
        basename    = ffi.string(full_ptr + m.OffsetToFileName),
        mapped_base = tonumber(ffi.cast('uintptr_t', m.MappedBase)),
        image_base  = tonumber(ffi.cast('uintptr_t', m.ImageBase)),
        image_size  = m.ImageSize,
        flags       = m.Flags,
        load_order  = m.LoadOrderIndex,
        init_order  = m.InitOrderIndex,
        load_count  = m.LoadCount,
    }
end

-- Iterate loaded kernel modules. Yields a Lua table per module — no
-- cdata lifetime concerns for callers.
function M.each_module()
    local buf  = query_buffer(SystemModuleInformation, 8192)
    local list = ffi.cast('RTL_PROCESS_MODULES *', buf)
    return coroutine.wrap(function()
        -- Touch buf inside the coroutine so the closure anchors it;
        -- list is a cast (Shape 7), wouldn't keep buf alive on its own.
        local _ = buf
        for i = 0, list.NumberOfModules - 1 do
            coroutine.yield(copy_module(list.Modules[i]))
        end
    end)
end

return M
