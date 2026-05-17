-- ntosbe layer: lua
--
-- The LuaJIT application runtime.  A thin trampoline EXE (run.exe), the
-- shared VM DLL (lua.dll), and the entire src/pkg/ Lua tree staged at
-- \SystemRoot\lua\.  This layer also supplies the init-process Exe
-- default — a profile that boots a Lua entry script includes `lua`;
-- one that boots a native application omits it and supplies its own.

local M = {}

M.name = "lua"
M.description = "LuaJIT runtime + the pkg/ Lua tree"

-- The runtime layer owns the init Exe.  Profiles supply init.args
-- (the entry script); core supplies init.stdio.
M.init = {
    exe = "lua\\run.exe",
}

function M.files(paths, list_tree)
    -- lua.dll lands in System32 because NT 3.5's kernel-side image
    -- loader searches *only* System32 for the initial process's
    -- imports (the "EXE dir first" rule is a kernel32 LoadLibrary
    -- policy, not used for the init process).
    local files = {
        { dest = "lua/run.exe",      src = paths.cr_dir .. "/run.exe" },
        { dest = "System32/lua.dll", src = paths.cr_dir .. "/lua.dll" },
    }

    -- pkg/ tree: stage every file under src/pkg/ at \SystemRoot\lua\<rel>.
    -- The Lua application sets package.path = "\SystemRoot\lua\?.lua;..."
    -- so require('nt.dll.fs') resolves correctly.
    --
    -- Markdown lives in the pkg tree for developers (plan notes and
    -- design docs next to the code) but is never require()d at runtime
    -- — and doc names like `iocp-plan.md` blow the FAT16 8.3 limit.
    -- Stage only what the guest actually loads.
    for _, rel in ipairs(list_tree(paths.pkg_root)) do
        if not rel:match("%.md$") then
            files[#files + 1] = {
                dest = "lua/" .. rel,
                src  = paths.pkg_root .. "/" .. rel,
            }
        end
    end

    return files
end

return M
