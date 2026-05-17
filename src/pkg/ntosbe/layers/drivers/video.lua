-- ntosbe layer: drivers.video
--
-- Video port driver + bochsvga miniport, for the eventual pure-Lua UI.
-- Neither is auto-started — the Lua UI layer registers and starts them
-- when it is ready, then maps the framebuffer via
-- IOCTL_VIDEO_MAP_VIDEO_MEMORY.  So this layer stages files only and
-- contributes no SYSTEM-hive services.

local M = {}

M.name = "drivers.video"
M.description = "videoprt + bochsvga (staged, started on demand by the UI)"

function M.files(paths)
    return {
        { dest = "System32/Drivers/videoprt.sys", src = paths.sdk_lib .. "/videoprt.sys" },
        { dest = "System32/Drivers/bochsvga.sys", src = paths.sdk_lib .. "/bochsvga.sys" },
    }
end

return M
