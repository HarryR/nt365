-- main.lua — MicroNT initial user process.
--
-- Walks the unified NT namespace via nt.tree. Directory, Key,
-- SymbolicLink, Value and the rest all show up as Nodes; the walk
-- is one recursion that doesn't care which type it's at — empty
-- iterators make leaves a no-op. Per-type display lives in the
-- render table below; handlers in nt.tree.types.* own syscalls and
-- semantic decoding (Value:decode() for REG_*), main.lua just
-- formats decoded values for print.

-- Phase A reorg: every package lives under \SystemRoot\lua\.  Set
-- package.path before the first require() so `require('nt.tree')` etc.
-- resolves out of the on-disk tree.  Phase B will move this into the
-- C side (run.exe / lua.exe sets LUA_PATH or pushes package.path
-- before the entry script runs); for now the entry scripts do it.
package.path = "\\SystemRoot\\lua\\?.lua;\\SystemRoot\\lua\\?\\init.lua"
package.cpath = ""

local ffi   = require('ffi')
local ntdll = require('nt.dll')
local tree  = require('nt.tree')

-- Shutdown primitives — raw ffi for now, one-off use. Move to nt.dll.ex
-- (Executive) if/when we grow more of that surface.
ffi.cdef[[
NTSTATUS __stdcall RtlAdjustPrivilege(ULONG Privilege,
                                      unsigned char Enable,
                                      unsigned char Client,
                                      unsigned char *WasEnabled);
NTSTATUS __stdcall NtShutdownSystem(int Action);
]]

-- Registry data type codes — needed for render dispatch only; the
-- Value handler owns decoding.
local REG_NONE, REG_SZ, REG_EXPAND_SZ, REG_BINARY, REG_DWORD = 0, 1, 2, 3, 4
local REG_MULTI_SZ                 = 7
local REG_RESOURCE_LIST            = 8
local REG_FULL_RESOURCE_DESCRIPTOR = 9

-- INTERFACE_TYPE enum — bus-name lookup for
-- CM_FULL_RESOURCE_DESCRIPTOR.interface_type. Pure display; lives here.
local INTERFACE_TYPE_NAMES = {
    [-1] = "Undef",    [0]  = "Internal", [1]  = "Isa",
    [2]  = "Eisa",     [3]  = "MChannel", [4]  = "TurboCh",
    [5]  = "PCIBus",   [6]  = "VMEBus",   [7]  = "NuBus",
    [8]  = "PCMCIA",   [9]  = "CBus",     [10] = "MPIBus",
    [11] = "MPSABus",  [12] = "ProcInt",  [13] = "IntPower",
    [14] = "PNPISA",   [15] = "PNPBus",
}

-- ---------------------------------------------------------------------
-- Resource-list rendering. Operates on the decoded Lua table shape
-- produced by value.lua — no cdata here.
-- ---------------------------------------------------------------------

local function render_partial(p)
    if p.kind == "Port" then
        return string.format("Port 0x%x..0x%x", p.start, p.start + p.length - 1)
    elseif p.kind == "Interrupt" then
        return string.format("IRQ %d", p.level)
    elseif p.kind == "Memory" then
        return string.format("Mem 0x%08x..0x%08x", p.start, p.start + p.length - 1)
    elseif p.kind == "Dma" then
        return string.format("DMA ch%d", p.channel)
    elseif p.kind == "DeviceSpecific" then
        return string.format("DevSpec[%d]", p.data_size)
    else
        return string.format("type%s", tostring(p.type or p.kind))
    end
end

local function render_full(fd)
    local iface = INTERFACE_TYPE_NAMES[fd.interface_type]
                  or tostring(fd.interface_type)
    local parts = {}
    for i, p in ipairs(fd.partials) do parts[i] = render_partial(p) end
    return string.format("%s.%d [%s]",
        iface, fd.bus_number, table.concat(parts, ", "))
end

local function render_resource_list(rl)
    if #rl.full == 0 then return "RESLIST empty" end
    local parts = {}
    for i, fd in ipairs(rl.full) do parts[i] = render_full(fd) end
    return "RESLIST " .. table.concat(parts, "; ")
end

-- Render a Value node's decoded data. Calls :decode() for the Lua-
-- native form, then type-dispatches on formatting.
local function render_value(n)
    local typ = n.type
    local len = n.length
    if typ == REG_NONE then return "NONE" end

    local v = n:decode()

    if typ == REG_DWORD then
        return string.format("DWORD  0x%08x", v)
    elseif typ == REG_SZ then
        return string.format("SZ     %q", v)
    elseif typ == REG_EXPAND_SZ then
        return string.format("EXPAND %q", v)
    elseif typ == REG_MULTI_SZ then
        local strs = {}
        for i, s in ipairs(v) do strs[i] = string.format("%q", s) end
        return string.format("MULTI  [%s]", table.concat(strs, ", "))
    elseif typ == REG_BINARY then
        local parts = {}
        local preview = len < 16 and len or 16
        for j = 1, preview do
            parts[j] = string.format("%02x", string.byte(v, j))
        end
        return string.format("BIN[%d] %s%s",
            len, table.concat(parts, " "), len > 16 and " ..." or "")
    elseif typ == REG_RESOURCE_LIST then
        return render_resource_list(v)
    elseif typ == REG_FULL_RESOURCE_DESCRIPTOR then
        return "FULLDESC " .. render_full(v)
    else
        return string.format("TYPE=%d LEN=%d", typ, len)
    end
end

-- ---------------------------------------------------------------------
-- Per-type display. render(node) returns one line; the walker adds
-- indent and prints. Unknown TypeNames fall through to _default.
-- ---------------------------------------------------------------------

local render = {}

function render._default(n)
    return string.format("%-24s <%s>", n.name, n.type_name or "?")
end

function render.SymbolicLink(n)
    local ok, target = pcall(function() return n.target end)
    if ok then
        return string.format("%-24s <SymbolicLink>  → %s", n.name, target)
    end
    return string.format("%-24s <SymbolicLink>  (query failed: %s)",
                         n.name, tostring(target))
end

function render.Value(n)
    return string.format("  = %-20s %s", n.name, render_value(n))
end

function render.Process(n)
    return string.format("%-6s <Process>     %-20s  threads=%-3d  rss=%dK  virt=%dK  faults=%d",
        n.name, n.image or "(?)", n.threads or 0,
        math.floor((n.working_set  or 0) / 1024),
        math.floor((n.virtual_size or 0) / 1024),
        n.page_faults or 0)
end

function render.Thread(n)
    return string.format("%-6s <Thread>      state=%-11s  reason=%-14s  prio=%d  ctx=%d",
        n.name, tostring(n.thread_state), tostring(n.wait_reason),
        n.priority or 0, n.context_switches or 0)
end

function render.Module(n)
    return string.format("%-24s <Module>  base=0x%08x  size=%dK  %s",
        n.name, n.image_base or 0,
        math.floor((n.image_size or 0) / 1024),
        n.image_path or "")
end

function render.Event(n)
    local ok, sig = pcall(function() return n.signaled end)
    if not ok then return string.format("%-24s <Event>", n.name) end
    return string.format("%-24s <Event>  %s  (%s)", n.name,
        sig and "SIGNALED" or "not signaled",
        tostring((ok and n.event_type) or ""))
end

function render.Section(n)
    local ok, size = pcall(function() return n.maximum_size end)
    if not ok then return string.format("%-24s <Section>", n.name) end
    -- size is int64_t (LARGE_INTEGER.QuadPart) cdata; coerce to Lua number.
    return string.format("%-24s <Section>  size=%dK  attrs=0x%x",
        n.name, math.floor(tonumber(size) / 1024),
        n.allocation_attributes or 0)
end

local function render_node(n)
    local r = render[n.type_name] or render._default
    return r(n)
end

-- ---------------------------------------------------------------------
-- Single unified walk. Recurses into every node that has children;
-- leaves (SymbolicLink, Value, Device, File) return an empty iterator
-- and the recursion no-ops. Tracks TypeNames we couldn't handle so we
-- can report coverage at the end.
-- ---------------------------------------------------------------------

local unhandled = {}

local function mark_unhandled(n)
    local h = n:handler()
    -- Handled = the handler exposes at least one surface element.
    -- A pure-default fallback (empty table) counts as unhandled. Legit
    -- leaves like Type/Driver/Port live here — reporting them surfaces
    -- how much of the object manager still has no user-space verbs.
    local handled = h and (h.open or h.children or h.fields or h.methods)
    if not handled then
        unhandled[n.type_name or "?"] =
            (unhandled[n.type_name or "?"] or 0) + 1
    end
end

local function walk(node, depth)
    local indent = string.rep("  ", depth)
    local ok, err = pcall(function()
        for child in node:iter() do
            mark_unhandled(child)
            print(indent .. render_node(child))
            walk(child, depth + 1)
        end
    end)
    if not ok then
        print(string.format("%s  (walk failed: %s)", indent, tostring(err)))
    end
end

-- ---------------------------------------------------------------------
-- Main
-- ---------------------------------------------------------------------

print("MicroNT: unified namespace walk from \\")
print("---")
walk(tree.root(), 0)
print("--- end of walk ---")

print("")
print("coverage: TypeNames with no open()/children() handler")
print("---")
local any = false
for tn, n in pairs(unhandled) do
    print(string.format("  %-16s  %d", tn, n))
    any = true
end
if not any then print("  (none — every type has a handler)") end

-- (Smoke tests for LPC / MM / etc. previously inlined here have
-- moved into test/<module>.lua — run via `make selftest`. main.lua
-- is now just the namespace walk + introspection demos.)

-- ---------------------------------------------------------------------
-- Process list demo — walks the synthetic \Processes virtual. For each
-- Process Node, opens a live handle and queries it via NtQueryObject
-- (through :info()). Confirms both snapshot fields and live-handle
-- queries work for PID-addressed objects.
-- ---------------------------------------------------------------------

print("")
print("demo: \\Processes")
print("---")
local ok, plist = pcall(tree.resolve, "\\Processes")
if not ok then
    print("  resolve failed: " .. tostring(plist))
else
    print(string.format("  %-5s %-5s %-20s %-8s %-10s %-10s %-8s",
                        "PID", "PPID", "Image", "Threads",
                        "VirtSize", "WorkingSet", "Faults"))
    for p in plist:iter() do
        print(string.format("  %-5d %-5d %-20s %-8d %-10d %-10d %-8d",
            p.pid, p.parent_pid, p.image, p.threads,
            p.virtual_size, p.working_set, p.page_faults))
    end
end

-- Filesystem enumeration via NtQueryDirectoryFile. \SystemRoot is a
-- SymbolicLink; opening "\SystemRoot\" (trailing backslash) routes the
-- name resolver through the link AND through the FS driver, returning
-- a directory handle on the mounted volume. Without the trailing
-- backslash the resolver stops at the raw partition device, which
-- NtQueryDirectoryFile doesn't accept.
local function walk_fs(path, depth)
    local indent = string.rep("  ", depth)
    local node = tree.Node.new(nil, "", path, "File")
    for f in node:iter() do
        local is_dir = f.is_directory
        print(string.format("%s%-32s %s  %d bytes",
            indent, f.name, is_dir and "<DIR>" or "     ", f.size or 0))
        if is_dir and depth < 2 then
            walk_fs(path .. f.name .. "\\", depth + 1)
        end
    end
end

print("")
print("demo: \\SystemRoot\\ (filesystem enumeration)")
print("---")
walk_fs("\\SystemRoot\\", 0)

-- Open the current process (run.exe) via the synthetic path. PID 0 is
-- the idle pseudo-process — NtOpenProcess rejects it with
-- STATUS_INVALID_PARAMETER, so skip it.
print("")
print("demo: open first real process")
print("---")
local ok2, plist2 = pcall(tree.resolve, "\\Processes")
if ok2 then
    for p in plist2:iter() do
        if p.pid ~= 0 then
            print(string.format("  resolved: %s  pid=%d  image=%s",
                                tostring(p), p.pid, p.image))
            local iok, info = pcall(function() return p:info() end)
            if iok then
                print(string.format("    info.type=%s  handles=%d  pointers=%d",
                    tostring(info.type), info.handle_count or -1,
                    info.pointer_count or -1))
            else
                print("    info failed: " .. tostring(info))
            end
            break
        end
    end
end

-- Hold the system up for a window so we can exercise virtio-input
-- (kernel-debug DbgPrint trace from vioinput.sys is the consumer for
-- now — keys + mouse motion in QEMU's VGA window land in the boot log).
-- Remove once a real input-driven Lua loop replaces this stand-in.
local ke = require('nt.dll.ke')
print("")
print("vioinput test window: 30s — interact with QEMU VGA window now")
ke.NtDelayExecution(false, ke.timeout(30))

-- Shut down cleanly. NT 3.5 requires SeShutdownPrivilege (value 19
-- per ntseapi.h); our init process runs under the kernel's token so
-- RtlAdjustPrivilege will enable it. ShutdownPowerOff routes through
-- HalReturnToFirmware; without ACPI, NT 3.5 falls back to halting.
print("")
print("Shutting down...")
local was = ffi.new('unsigned char[1]')
ntdll.RtlAdjustPrivilege(19 --[[ SE_SHUTDOWN_PRIVILEGE ]], 1, 0, was)
local st = ntdll.NtShutdownSystem(2 --[[ ShutdownPowerOff ]])

-- Only reached if shutdown rejects us.
print(string.format("NtShutdownSystem returned 0x%08x", st < 0 and st + 0x100000000 or st))
while true do end
