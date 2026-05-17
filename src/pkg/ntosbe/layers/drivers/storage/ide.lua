-- ntosbe layer: drivers.storage.ide
--
-- Legacy IDE / ATA disk via atdisk.sys.  Stand-alone — atdisk talks to
-- the controller directly and does not go through the SCSI miniport
-- framework, so this layer has no `requires`.

local M = {}

M.name = "drivers.storage.ide"
M.description = "Legacy IDE/ATA disk (atdisk)"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    services:key("atdisk")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
end

-- Bucket 20 (miniport tier).  atdisk's DriverEntry decides whether an
-- IDE drive is present; on a non-IDE shape it declines and is logged.
function M.boot_drivers(paths)
    return {
        { name = "atdisk.sys", bucket = 20, src = paths.sdk_lib .. "/atdisk.sys" },
    }
end

return M
