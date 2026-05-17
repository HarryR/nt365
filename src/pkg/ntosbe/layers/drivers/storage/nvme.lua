-- ntosbe layer: drivers.storage.nvme
--
-- NVMe storage controller miniport (nvme2k.sys).  Registers via
-- scsiport; scsidisk presents the namespace as \Device\Harddisk<N>.

local M = {}

M.name = "drivers.storage.nvme"
M.description = "NVMe controller miniport (nvme2k)"
M.requires = { "drivers.storage.scsi" }

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    services:key("nvme2k")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")
        :set_multi_sz("DependOnService", { "scsiport" })
end

-- Bucket 20 (miniport tier): after scsiport (10), before scsidisk (30).
function M.boot_drivers(paths)
    return {
        { name = "nvme2k.sys", bucket = 20, src = paths.sdk_lib .. "/nvme2k.sys" },
    }
end

return M
