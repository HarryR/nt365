-- ntosbe layer: drivers.virtio.rng
--
-- virtio-rng entropy device (viorng.sys), surfaces \Device\VirtioRng0.
-- Links against the shared virtio.lib and loads in the Virtio group.

local M = {}

M.name = "drivers.virtio.rng"
M.description = "virtio-rng entropy device (viorng)"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    services:key("viorng")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Virtio")
end

function M.files(paths)
    return {
        { dest = "System32/Drivers/viorng.sys", src = paths.sdk_lib .. "/viorng.sys" },
    }
end

return M
