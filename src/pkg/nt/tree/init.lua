-- nt.tree — unified, lazy traversal over the NT object namespace and
-- the registry. Every entry in \ (Directory, SymbolicLink, Device,
-- File, Key, ...) shows up as a Node; per-type behavior lives in
-- nt.tree.types.<lowercased typename>, auto-loaded on first access.
--
-- Design rules:
--
--   - Nodes are cheap: {parent, name, path, type_name}. Constructing
--     a Node does not open a handle. That happens lazily in :open()
--     or the first time a handler needs one, and the handle is cached
--     on self.__handle — the NT_HANDLE's own __gc still owns cleanup.
--
--   - Handlers own the nt.dll.* calls and structured data, not
--     rendering. An iterator yields Nodes (directory/key children)
--     or plain records (key values) — the caller decides how to
--     display them.
--
--   - Iteration is coroutine-driven. `for child in node:iter() do`
--     walks one syscall-worth at a time; `for k,v in pairs(node) do`
--     does the same via __pairs (requires LUAJIT_ENABLE_LUA52COMPAT).
--
--   - REPL discovery via :completions() — returns the field/method
--     names the current type exposes, for tab completion.
--
-- Handler contract (nt.tree.types.<name>):
--
--   M.open         = function(node) → NT_HANDLE            -- optional
--   M.children     = function(node) → iter() → Node|nil    -- optional
--   M.fields       = { key = function(node) → value }      -- lazy props
--   M.methods      = { name = function(node, ...) → ... }  -- verbs
--   M.descriptions = { key_or_method = "help text" }        -- for :help()

local ffi = require('ffi')

local load_handler   -- forward

local Node    = {}
local Node_mt = {}

local function join(path, name)
    if path == "\\" or path == "" then return "\\" .. name end
    return path .. "\\" .. name
end

function Node.new(parent, name, path, type_name)
    return setmetatable({
        parent    = parent,
        name      = name,
        path      = path,
        type_name = type_name,
    }, Node_mt)
end

function Node:handler()
    return load_handler(self.type_name)
end

-- Open + cache the backing NT handle via the type handler.
function Node:open()
    if self.__handle then return self.__handle end
    local h = self:handler()
    if not (h and h.open) then
        error(("<%s %s>: no open() for type"):format(
            self.type_name or "?", self.path), 2)
    end
    self.__handle = h.open(self)
    return self.__handle
end

function Node:close()
    if self.__handle then
        self.__handle:close()
        self.__handle = nil
    end
end

-- Yielding iterator over children. Returns a zero-arg function; each
-- call runs the handler's coroutine until the next yield. Types that
-- have no children (SymbolicLink, File) return the empty iterator.
--
-- If self.__virtuals is set, its entries are yielded AFTER the real
-- children. Used to graft synthetic entries like \Processes onto the
-- root Directory without touching the OM namespace.
function Node:iter()
    local h = self:handler()
    local real = (h and h.children) and h.children(self)
                 or function() return nil end
    local virts = rawget(self, "__virtuals")
    if not virts then return real end
    return coroutine.wrap(function()
        for c in real do coroutine.yield(c) end
        for _, spec in ipairs(virts) do
            coroutine.yield(Node.new(self, spec.name,
                                     join(self.path, spec.name),
                                     spec.type_name))
        end
    end)
end

-- Help text for the built-in Node surface. Type-specific descriptions
-- come from the handler's .descriptions table.
local CORE_DESCRIPTIONS = {
    parent      = "Parent Node (nil for root).",
    name        = "Leaf name of this node.",
    path        = "Absolute NT path.",
    type_name   = "NT TypeName (Directory, Key, SymbolicLink, ...).",
    iter        = "iter() → coroutine iterator yielding child Node per step.",
    open        = "open() → NT_HANDLE, cached on self.__handle.",
    close       = "close() → release the cached handle (idempotent).",
    handler     = "handler() → the type handler module for this node.",
    completions = "completions() → {fields, methods} for tab completion.",
    info        = "info() → {name, type, attributes, handle_count, ...} via NtQueryObject.",
    help        = "help(name) → description string for a field or method.",
}

-- For REPL tab-completion. Each entry is {name=..., kind="field"|"method",
-- description=...}. Core items always present; type-specific items come
-- from the handler.
function Node:completions()
    local out = {}
    local seen = {}
    local function push(name, kind, desc)
        if seen[name] then return end
        seen[name] = true
        out[#out+1] = { name = name, kind = kind, description = desc }
    end
    local h = self:handler()
    if h then
        local d = h.descriptions or {}
        for k in pairs(h.fields  or {}) do push(k, "field",  d[k]) end
        for k in pairs(h.methods or {}) do push(k, "method", d[k]) end
    end
    for _, k in ipairs({"iter", "open", "close", "handler",
                        "completions", "info", "help"}) do
        push(k, "method", CORE_DESCRIPTIONS[k])
    end
    for _, k in ipairs({"parent", "name", "path", "type_name"}) do
        push(k, "field", CORE_DESCRIPTIONS[k])
    end
    return out
end

-- Human-readable description for a field or method name, or nil if
-- unknown. Checks the handler's .descriptions, then falls back to the
-- built-in Node surface.
function Node:help(name)
    local h = self:handler()
    if h and h.descriptions and h.descriptions[name] then
        return h.descriptions[name]
    end
    return CORE_DESCRIPTIONS[name]
end

-- Generic object introspection via NtQueryObject. Works on any node
-- whose handler supports open(). Returns a table with basic info plus
-- the object's true name and TypeName as reported by the kernel —
-- handy when type_name on the Node was guessed or stale.
--
-- NT 3.5's NtQueryObject strict-checks the length field for fixed-size
-- info classes (ObjectBasicInformation) — Length must equal the struct
-- size exactly or it returns STATUS_INFO_LENGTH_MISMATCH. For variable
-- classes (Name, Type) it requires Length >= a header + the embedded
-- wchar_t data. Size each call accordingly.
function Node:info()
    local ob  = require('nt.dll.ob')
    local str = require('nt.dll.str')
    local h = self:open()
    local out = {}

    local bi      = ffi.new('OBJECT_BASIC_INFORMATION')
    local bi_size = ffi.sizeof('OBJECT_BASIC_INFORMATION')
    local ok_b    = pcall(ob.NtQueryObject, h, 0 --[[ Basic ]], bi, bi_size)
    if ok_b then
        out.attributes              = bi.Attributes
        out.granted_access          = bi.GrantedAccess
        out.handle_count            = bi.HandleCount
        out.pointer_count           = bi.PointerCount
        out.paged_pool_usage        = bi.PagedPoolUsage
        out.non_paged_pool_usage    = bi.NonPagedPoolUsage
        out.name_info_size          = bi.NameInfoSize
        out.type_info_size          = bi.TypeInfoSize
    end

    local buf = ffi.new('char[4096]')
    local ok_n = pcall(ob.NtQueryObject, h, 1 --[[ Name ]], buf, 4096)
    if ok_n then
        local ni = ffi.cast('OBJECT_NAME_INFORMATION *', buf)
        out.name = str.from_utf16(ni.Name)
    end

    local ok_t = pcall(ob.NtQueryObject, h, 2 --[[ Type ]], buf, 4096)
    if ok_t then
        local ti = ffi.cast('OBJECT_TYPE_INFORMATION *', buf)
        out.type              = str.from_utf16(ti.Name)
        out.total_objects     = ti.TotalNumberOfObjects
        out.total_handles     = ti.TotalNumberOfHandles
        out.valid_access_mask = ti.ValidAccessMask
    end

    return out
end

-- Path composition: root / "Registry" / "Machine" ...
-- The resulting node has nil type_name; handler dispatch will fall back
-- to the _default handler until something populates it (e.g. the caller
-- sets node.type_name, or the node came from iteration which does).
function Node_mt:__div(name)
    return Node.new(self, name, join(self.path, name), nil)
end

function Node_mt:__tostring()
    return ("<%s %s>"):format(self.type_name or "?", self.path)
end

-- __index order:
--   1. Core Node methods (iter, open, completions, ...)
--   2. Handler fields  (h.fields[k] — called as k(self))
--   3. Handler methods (h.methods[k] — returned raw for `node:k(...)`)
function Node_mt:__index(k)
    local m = Node[k]; if m then return m end
    local h = load_handler(rawget(self, "type_name"))
    if h then
        local f = h.fields  and h.fields [k]; if f  then return f(self) end
        local mm = h.methods and h.methods[k]; if mm then return mm      end
    end
    return nil
end

-- __pairs → yield (name, child) per directory entry. Needs LuaJIT built
-- with LUAJIT_ENABLE_LUA52COMPAT; without it, `for k,v in pairs(node)
-- do` is a no-op and callers should use :iter() directly.
function Node_mt:__pairs()
    local it = self:iter()
    return function()
        local child = it()
        if child == nil then return nil end
        return child.name, child
    end
end

-- ------------------------------------------------------------------
-- Handler registry
-- ------------------------------------------------------------------

-- TypeName → module basename. Explicit because FAT 8.3 on the boot
-- image won't let us just lowercase-and-append — "Directory" and
-- "SymbolicLink" both blow the 8-char base limit.
local TYPE_MODULE = {
    Directory    = "dir",
    SymbolicLink = "symlink",
    Key          = "key",
    Value        = "value",
    File         = "file",
    Device       = "device",
    Event        = "event",
    EventPair    = "evpair",
    Section      = "section",
    Mutant       = "mutant",
    Semaphore    = "semaph",
    Timer        = "timer",
    IoCompletion = "iocomp",
    Process      = "process",
    ProcessList  = "proclist",
    Thread       = "thread",
    SystemDir    = "sysdir",
    ModuleList   = "modlist",
    Module       = "module",
}

local handler_cache = {}
load_handler = function(type_name)
    if not type_name then return nil end
    local hit = handler_cache[type_name]
    if hit ~= nil then return hit or nil end
    local base = TYPE_MODULE[type_name]
    local h
    if base then
        local ok, mod = pcall(require, "nt.tree.types." .. base)
        if ok then h = mod end
    end
    if not h then
        local ok, def = pcall(require, "nt.tree.types.default")
        h = ok and def or false
    end
    handler_cache[type_name] = h or false
    return h or nil
end

-- ------------------------------------------------------------------
-- Module surface
-- ------------------------------------------------------------------

local M = {}

-- Synthetic children grafted onto the root. Not in the NT object
-- manager namespace — they surface kernel state that's not reachable
-- by path lookup (e.g. the process list via NtQuerySystemInformation).
local ROOT_VIRTUALS = {
    { name = "Processes", type_name = "ProcessList" },
    { name = "System",    type_name = "SystemDir"   },
}

-- Root of the unified namespace. \ is a Directory object; iteration
-- finds \Registry (a Key), \Device, \BaseNamedObjects, etc. and each
-- gets its matching handler when touched. Virtual children (\Processes
-- and friends) are appended after the real OM entries.
function M.root()
    local n = Node.new(nil, "", "\\", "Directory")
    n.__virtuals = ROOT_VIRTUALS
    return n
end

-- Resolve an absolute NT path to a Node with its real type_name.
-- Walks the tree one segment at a time via the parent's iterator —
-- simple and type-correct because dir/key iteration yields children
-- with their kernel-reported TypeName. Raises on a missing segment.
function M.resolve(path)
    if path == nil or path == "" or path == "\\" then return M.root() end
    if path:sub(1, 1) ~= "\\" then
        error("resolve: path must be absolute, got " .. tostring(path), 2)
    end
    local node = M.root()
    for seg in path:gmatch("[^\\]+") do
        local found
        local lseg = seg:lower()
        for child in node:iter() do
            -- NT paths are case-insensitive (OBJ_CASE_INSENSITIVE) and
            -- the kernel can return names in upper case (e.g. REGISTRY
            -- under \). Compare lowercased.
            if child.name:lower() == lseg then found = child; break end
        end
        if not found then
            error(("resolve: no such path %s (missing %q)"):format(path, seg), 2)
        end
        node = found
    end
    return node
end

M.Node = Node
return M
