-- ntosbe layer: drivers.virtio.console
--
-- virtio-serial console (vioser.sys) — a single-port virtio-serial
-- device, surfaces \Device\VirtioCon0.  Links against the shared
-- virtio.lib and loads in the Virtio group.

local M = {}

M.name = "drivers.virtio.console"
M.description = "virtio-serial console (vioser)"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    services:key("vioser")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Virtio")
end

function M.files(paths)
    return {
        { dest = "System32/Drivers/vioser.sys", src = paths.sdk_lib .. "/vioser.sys" },
    }
end

return M
