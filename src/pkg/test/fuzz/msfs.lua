-- test.fuzz.msfs — adversarial edge-case tests for the mailslot file
-- system (msfs) create surface.
--
-- The bugcheck-resistance counterpart to test.fuzz.npfs (which hardens
-- NtCreateNamedPipeFile). msfs.sys is a fully-privileged kernel driver
-- reachable straight from usermode via NtCreateMailslotFile — a
-- malformed call that faults it is a kernel bug in the same class as the
-- IOCP / npfs findings. See [[project_kernel_coverage_tests]].
--
-- Raw-ntdll surface (cf. test.fuzz.npfs): hands NtCreateMailslotFile
-- deliberately malformed pointers and out-of-range parameters.
-- Invariant: every call returns a clean NTSTATUS and never bugchecks —
-- survival of the in-process runner to t.summary() is itself the
-- assertion.
--
-- Scope: NtCreateMailslotFile, the server-create syscall. Client opens
-- (NtCreateFile on \Device\Mailslot\...) and the mailslot FSCTL/read/
-- write surface are exercised functionally in test.msfs.

local bit   = require('bit')
local ffi   = require('ffi')
local t     = require('test')
local ntdll = require('nt.dll')
local fs    = require('nt.dll.fs')      -- registers the NtCreateMailslotFile cdef
local oa    = require('nt.dll.oa')
local ke    = require('nt.dll.ke')
local err   = require('nt.dll.errors')
require('nt.dll.handle')                -- registers NtClose

local STATUS_SUCCESS = 0x00000000

local function hex(st) return string.format("0x%08x", st) end

t.suite("msfs: hardening (raw NtCreateMailslotFile)")

local DEFAULT_ACCESS  = bit.bor(fs.FILE_GENERIC_READ, fs.SYNCHRONIZE)
-- Immediate read timeout (0) by default — built once, reused.
local DEFAULT_TIMEOUT = ffi.new('LARGE_INTEGER')

local namecount = 0
local function ms_name()
    namecount = namecount + 1
    return "\\Device\\Mailslot\\fuzzms" .. namecount
end
local function ms_oa()
    return oa.path(ms_name())
end

-- Call NtCreateMailslotFile. `p` overrides any field; anything absent
-- uses a sane default. Returns the normalised NTSTATUS; on success (and
-- when we owned the handle slot) the handle is closed, tearing the
-- mailslot down.
local function create(p)
    p = p or {}
    local own_h = (p.FileHandle == nil)
    local h     = p.FileHandle    or ffi.new('HANDLE[1]')
    local iosb  = p.IoStatusBlock or ffi.new('IO_STATUS_BLOCK')
    local st = err.normalize(ntdll.NtCreateMailslotFile(
        h,
        p.DesiredAccess    or DEFAULT_ACCESS,
        p.ObjectAttributes,                       -- nil -> NULL
        iosb,
        p.CreateOptions    or fs.FILE_SYNCHRONOUS_IO_NONALERT,
        p.MailslotQuota    or 0,
        p.MaximumMessageSize or 0,
        p.ReadTimeout      or DEFAULT_TIMEOUT))
    if st == STATUS_SUCCESS and own_h then
        ntdll.NtClose(h[0])
    end
    return st
end

-- Reaching this line means no bugcheck. msfs may accept or reject the
-- value — both are fine; the invariant under test is only that the
-- kernel did not crash.
local function survived(label, st)
    t.ok(true, label .. " -> " .. hex(st) .. " (clean NTSTATUS, no bugcheck)")
end

-- ------------------------------------------------------------------
-- Baseline — a sane create must succeed, so the fuzz below is known to
-- actually reach msfs rather than failing in front of it.
-- ------------------------------------------------------------------

t.test("a valid mailslot creates and closes cleanly", function()
    local h = fs.create_mailslot{ name = ms_name() }
    t.ok(h ~= nil, "fs.create_mailslot returned a handle")
    h:close()
end)

-- ------------------------------------------------------------------
-- Output-pointer faults — IoCreateFile probes FileHandle and
-- IoStatusBlock for write before reaching msfs.
-- ------------------------------------------------------------------

local OOR = {
    { name = "NULL",         make = function(ct) return ffi.cast(ct, 0) end },
    { name = "kernel-range", make = function(ct) return ffi.cast(ct, 0x80000000) end },
}

for _, bad in ipairs(OOR) do
    t.test("rejects FileHandle = " .. bad.name .. " pointer", function()
        local noa = ms_oa()
        local st  = create{ FileHandle = bad.make('HANDLE *'),
                            ObjectAttributes = noa.oa }
        t.ne(st, STATUS_SUCCESS, "bad FileHandle must not succeed")
        t.ok(st >= 0xC0000000, "expected an error NTSTATUS, got " .. hex(st))
    end)

    t.test("rejects IoStatusBlock = " .. bad.name .. " pointer", function()
        local noa = ms_oa()
        local st  = create{ IoStatusBlock = bad.make('IO_STATUS_BLOCK *'),
                            ObjectAttributes = noa.oa }
        t.ne(st, STATUS_SUCCESS, "bad IoStatusBlock must not succeed")
        t.ok(st >= 0xC0000000, "expected an error NTSTATUS, got " .. hex(st))
    end)
end

-- ------------------------------------------------------------------
-- ReadTimeout fault — the optional ReadTimeout LARGE_INTEGER is probed
-- for read. NULL is valid (= wait forever); a non-NULL bad pointer must
-- fault cleanly.
-- ------------------------------------------------------------------

local rt_pad = ffi.new('char[64]')
local RT_BAD = {
    { name = "kernel-range",
      ptr  = ffi.cast('LARGE_INTEGER *', 0x80000000) },
    { name = "unaligned",
      ptr  = ffi.cast('LARGE_INTEGER *', ffi.cast('char *', rt_pad) + 1) },
}

for _, bad in ipairs(RT_BAD) do
    t.test("rejects ReadTimeout = " .. bad.name .. " pointer", function()
        local noa = ms_oa()
        local st  = create{ ObjectAttributes = noa.oa, ReadTimeout = bad.ptr }
        t.ne(st, STATUS_SUCCESS, "bad ReadTimeout must not succeed")
        t.ok(st >= 0x80000000, "expected a probe rejection, got " .. hex(st))
    end)
end

-- ------------------------------------------------------------------
-- ObjectAttributes / mailslot name.
-- ------------------------------------------------------------------

t.test("rejects a NULL ObjectAttributes", function()
    local st = create{}                       -- no ObjectAttributes -> NULL
    t.ne(st, STATUS_SUCCESS, "NULL OA must not succeed")
    t.ok(st >= 0xC0000000, "expected an error NTSTATUS, got " .. hex(st))
end)

t.test("rejects the device root with no mailslot name", function()
    local noa = oa.path("\\Device\\Mailslot")
    local st  = create{ ObjectAttributes = noa.oa }
    t.ne(st, STATUS_SUCCESS, "device root with no name must not succeed")
    t.ok(st >= 0x80000000, "expected an error NTSTATUS, got " .. hex(st))
end)

t.test("survives a 4 KB mailslot name", function()
    local noa = oa.path("\\Device\\Mailslot\\" .. string.rep("A", 4096))
    survived("4 KB name", create{ ObjectAttributes = noa.oa })
end)

-- ------------------------------------------------------------------
-- Out-of-range size scalars — MailslotQuota / MaximumMessageSize size
-- pool reservations; a hostile 4 GB request must fail gracefully
-- (STATUS_INSUFFICIENT_RESOURCES, or capped), never bugcheck or actually
-- exhaust the pool.
-- ------------------------------------------------------------------

for _, v in ipairs({ 0, 0xFFFFFFFF }) do
    t.test(string.format("survives MailslotQuota = 0x%x", v), function()
        local noa = ms_oa()
        survived("MailslotQuota=" .. hex(v),
                 create{ ObjectAttributes = noa.oa, MailslotQuota = v })
    end)
    t.test(string.format("survives MaximumMessageSize = 0x%x", v), function()
        local noa = ms_oa()
        survived("MaximumMessageSize=" .. hex(v),
                 create{ ObjectAttributes = noa.oa, MaximumMessageSize = v })
    end)
end

-- ------------------------------------------------------------------
-- Out-of-range CreateOptions — a directory-file create option on a
-- mailslot is nonsensical; msfs must reject or ignore, not bugcheck.
-- ------------------------------------------------------------------

t.test("survives CreateOptions = FILE_DIRECTORY_FILE", function()
    local noa = ms_oa()
    survived("CreateOptions=FILE_DIRECTORY_FILE",
             create{ ObjectAttributes = noa.oa,
                     CreateOptions = fs.FILE_DIRECTORY_FILE })
end)

t.test("survives CreateOptions = 0xFFFFFFFF", function()
    local noa = ms_oa()
    survived("CreateOptions=0xffffffff",
             create{ ObjectAttributes = noa.oa, CreateOptions = 0xFFFFFFFF })
end)
