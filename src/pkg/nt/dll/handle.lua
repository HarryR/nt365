-- nt.dll.handle — RAII wrapper for NT HANDLEs.
--
-- NT HANDLEs are kernel-retained tokens; userland releases via NtClose.
-- Without a wrapper, every Lua path that returns a handle is a leak
-- risk on error; with a wrapper, __gc closes on scope exit and
-- explicit close() / detach() express intent.
--
-- Every nt.dll.* wrapper takes NT_HANDLE only — no raw HANDLE inputs,
-- no pass-through fallback. The ownership model is uniform across the
-- whole module tree.
--
--
-- Shape:
--
--   typedef struct _NT_HANDLE {
--       HANDLE        __raw;    -- kernel handle value (internal)
--       unsigned char __owned;  -- 1 = __gc/close will NtClose; 0 = detached
--   } NT_HANDLE;
--
--
-- Public API (stable, memory-safe):
--
--   handle.wrap(raw)      Take ownership of a raw HANDLE. __gc will
--                         NtClose when the wrapper is collected.
--
--   handle.borrow(raw)    Wrap a raw HANDLE WITHOUT taking ownership.
--                         __gc and :close() are no-ops; the kernel
--                         handle's lifetime stays with whatever owns
--                         the original. Used when an NT_HANDLE wrapper
--                         is needed (e.g. to pass through nt.dll.* APIs
--                         that take NT_HANDLE) but the underlying
--                         handle is owned by something else (a parent
--                         object, another process, our own ctx).
--
--   handle.raw(h)         Extract the raw HANDLE value from an
--                         NT_HANDLE. Used by sub-modules to pass
--                         through to ntdll. The returned value is a
--                         BORROWED VIEW — valid only while `h` remains
--                         reachable. Do not save it past the wrapper.
--                         Errors if `h` isn't an NT_HANDLE.
--
--   h:close()             Explicit NtClose. Idempotent (safe to call
--                         more than once, and still safe after __gc
--                         fires). Prefer this over letting __gc run
--                         when you want deterministic cleanup.
--
--   h:detach()            Disown: clear the `owned` flag and return
--                         the raw HANDLE. Use when the handle is being
--                         transferred to something else that now owns
--                         it (child process via InheritHandles,
--                         cross-process NtDuplicateObject). After
--                         detach, __gc and close() become no-ops.
--
--
-- Direct field access (.__raw, .__owned) is not part of the API
-- contract — the underscore prefix marks them internal. Use the
-- methods above. Reading .__raw inside a Lua expression would be
-- fine because LuaJIT keeps the parent cdata alive for the expression
-- duration, but storing it in a longer-lived local and then dropping
-- the wrapper is a use-after-close waiting to happen: the wrapper's
-- __gc will NtClose the handle while your saved copy still refers to
-- the same kernel object — which may have been recycled for a
-- different resource by the time you next use it. Go through
-- handle.raw() if you need the value; go through :detach() if you
-- need to transfer ownership.

local ffi   = require('ffi')
local ntdll = require('nt.dll')

-- NtClose is universal to any handle object. Cdef here so the type +
-- destructor stay together; other nt.dll.* sub-modules access it via
-- the ntdll ffi handle.
ffi.cdef[[
typedef struct _NT_HANDLE {
    HANDLE        __raw;
    unsigned char __owned;
} NT_HANDLE;

long __stdcall NtClose(HANDLE);
]]

local M = {}

local methods = {}

function methods:close()
    if self.__owned ~= 0 and self.__raw ~= nil then
        ntdll.NtClose(self.__raw)
        self.__raw   = nil
        self.__owned = 0
    end
end

function methods:detach()
    local raw = self.__raw
    self.__owned = 0
    return raw
end

ffi.metatype('NT_HANDLE', {
    __index = methods,
    __gc    = function(self)
        if self.__owned ~= 0 and self.__raw ~= nil then
            ntdll.NtClose(self.__raw)
        end
    end,
})

-- Wrap a raw HANDLE, taking ownership (GC will close).
function M.wrap(raw_handle)
    local h    = ffi.new('NT_HANDLE')
    h.__raw    = raw_handle
    h.__owned  = 1
    return h
end

-- Wrap a raw HANDLE WITHOUT taking ownership. __gc and :close() are
-- no-ops on the resulting wrapper — the kernel handle stays alive
-- (and gets closed) under whoever owns it. Use when an nt.dll.* API
-- needs an NT_HANDLE but the underlying kernel handle is owned by
-- something else: a parent object, another process via DuplicateObject,
-- or in cr_thread.c's case, the CR_THREAD ctx that we want to wait on
-- without disturbing its own lifetime management.
function M.borrow(raw_handle)
    local h    = ffi.new('NT_HANDLE')
    h.__raw    = raw_handle
    h.__owned  = 0
    return h
end

-- Unwrap to a raw HANDLE. Requires an NT_HANDLE — raw HANDLE values
-- aren't accepted so the wrapped-handle contract is uniform across
-- every nt.dll.* syscall. Raises if given the wrong type.
function M.raw(h)
    if not ffi.istype('NT_HANDLE', h) then
        error("expected NT_HANDLE, got " .. tostring(h), 3)
    end
    return h.__raw
end

return M
