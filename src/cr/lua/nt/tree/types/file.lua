-- File / Device — both open via NtOpenFile. Devices in the object
-- manager (\Device\Serial0, \Device\Null, ...) respond to the same
-- fs-layer syscalls; the Device handler is a thin alias.
--
-- Access / share / options chosen for generic read-only browse:
--   FILE_READ_DATA | SYNCHRONIZE → basic read + waitable handle
--   FILE_SHARE_READ | FILE_SHARE_WRITE → don't break other openers
--   FILE_SYNCHRONOUS_IO_NONALERT → blocking reads; matches :read()
--
-- FILE_READ_DATA == FILE_LIST_DIRECTORY (same bit), so the same access
-- works for regular files and directories. NtQueryInformationFile
-- works on any file handle; NtQueryDirectoryFile only works when the
-- underlying object is a directory.

local ffi  = require('ffi')
local fs   = require('nt.dll.fs')
local oa   = require('nt.dll.oa')
local str  = require('nt.dll.str')
local tree = require('nt.tree')

local FILE_READ_DATA               = 0x0001
local FILE_SHARE_READ              = 0x0001
local FILE_SHARE_WRITE             = 0x0002
local SYNCHRONIZE                  = 0x00100000
local FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
local STATUS_NO_MORE_FILES         = 0x80000006

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    if path:sub(-1) == "\\" then return path .. name end
    return path .. "\\" .. name
end

local M = {}

function M.open(node)
    local h = fs.NtOpenFile(
        FILE_READ_DATA + SYNCHRONIZE,
        oa.path(node.path).oa,
        FILE_SHARE_READ + FILE_SHARE_WRITE,
        FILE_SYNCHRONOUS_IO_NONALERT)
    return h
end

-- Attribute queries are cheap on real files; not all Devices support
-- them (Null, Serial0, ...). pcall wraps both the open and the query
-- so a denied path becomes nil rather than an error that cascades up
-- through the walker.
local function safe_open(node)
    local ok, h = pcall(node.open, node)
    return ok and h or nil
end

local function safe_basic(node)
    local h = safe_open(node); if not h then return nil end
    local ok, info = pcall(fs.query_basic, h)
    return ok and info or nil
end

local function safe_standard(node)
    local h = safe_open(node); if not h then return nil end
    local ok, info = pcall(fs.query_standard, h)
    return ok and info or nil
end

M.fields = {
    size = function(n)
        local i = safe_standard(n); return i and i.EndOfFile.QuadPart or nil
    end,
    allocation_size = function(n)
        local i = safe_standard(n); return i and i.AllocationSize.QuadPart or nil
    end,
    links = function(n)
        local i = safe_standard(n); return i and i.NumberOfLinks or nil
    end,
    is_directory = function(n)
        local i = safe_standard(n); return i and (i.Directory ~= 0) or nil
    end,
    attributes = function(n)
        local i = safe_basic(n); return i and i.FileAttributes or nil
    end,
    created = function(n)
        local i = safe_basic(n); return i and i.CreationTime.QuadPart or nil
    end,
    accessed = function(n)
        local i = safe_basic(n); return i and i.LastAccessTime.QuadPart or nil
    end,
    modified = function(n)
        local i = safe_basic(n); return i and i.LastWriteTime.QuadPart or nil
    end,
    changed = function(n)
        local i = safe_basic(n); return i and i.ChangeTime.QuadPart or nil
    end,
}

-- Directory enumeration via NtQueryDirectoryFile. Opens a scoped
-- handle (NOT node:open() — we don't want the walker to accumulate a
-- cached __handle per directory) and reuses it both for the Directory=1
-- probe and for the query loop. When the coroutine ends the local h
-- goes out of scope and NT_HANDLE.__gc closes it immediately.
--
-- Serial and Null devices answer FILE_STANDARD_INFORMATION but with
-- Directory=0, so we short-circuit to an empty iterator without firing
-- a doomed NtQueryDirectoryFile.
--
-- Skip the '.' and '..' pseudo-entries so iteration doesn't infinite-
-- loop via the current Node's own path.
function M.children(node)
    local ok, h = pcall(M.open, node)
    if not ok then return function() return nil end end
    local iok, info = pcall(fs.query_standard, h)
    if not iok or not info or info.Directory == 0 then
        return function() return nil end
    end
    return coroutine.wrap(function()
        local buf = ffi.new('char[4096]')
        local first = true
        while true do
            local ok, len, st = pcall(fs.NtQueryDirectoryFile,
                                      h, buf, 4096, first)
            first = false
            if not ok then return end
            if st == STATUS_NO_MORE_FILES or len == 0 then return end
            local off = 0
            while true do
                local entry = ffi.cast('FILE_DIRECTORY_INFORMATION *',
                                        ffi.cast('char *', buf) + off)
                local nc    = entry.FileNameLength / 2
                local name  = str.from_wchars(entry.FileName, nc)
                if name ~= "." and name ~= ".." then
                    local fn = tree.Node.new(node, name,
                                             join(node.path, name), "File")
                    coroutine.yield(fn)
                end
                if entry.NextEntryOffset == 0 then break end
                off = off + entry.NextEntryOffset
            end
        end
    end)
end

M.methods = {
    read = function(node, length)
        local buf = ffi.new('char[?]', length)
        local n = fs.NtReadFile(node:open(), buf, length, nil)
        return ffi.string(buf, n)
    end,
}

M.descriptions = {
    size            = "Logical size (EndOfFile) in bytes; nil if not supported by the device.",
    allocation_size = "On-disk allocation size in bytes.",
    links           = "Hard-link count (usually 1 on FAT).",
    is_directory    = "true if this file object is a directory (supports children iteration).",
    attributes      = "FILE_ATTRIBUTE_* bitmask.",
    created         = "Creation time (100ns since 1601).",
    accessed        = "Last access time.",
    modified        = "Last write time.",
    changed         = "Last metadata change time.",
    read            = "read(n) → Lua string of up to n bytes from this handle.",
}

return M
