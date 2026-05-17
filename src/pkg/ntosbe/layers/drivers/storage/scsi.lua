-- ntosbe layer: drivers.storage.scsi
--
-- The SCSI miniport framework + disk class driver.  scsiport provides
-- ScsiPortInitialize and the SRB dispatch surface miniports register
-- against; scsidisk walks the miniports' device chains, parses
-- partition tables, and surfaces \Device\Harddisk<N>\Partition<P>.
--
-- This layer ships no controller of its own — it is `requires`d by the
-- concrete miniport layers (drivers.storage.nvme, drivers.storage.virtio).

local M = {}

M.name = "drivers.storage.scsi"
M.description = "SCSI miniport framework (scsiport) + disk class (scsidisk)"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")

    -- Boot-start (Start=0) so the loader pre-loads it alongside the
    -- miniports.  A miniport whose hardware is absent returns
    -- STATUS_NO_SUCH_DEVICE and is logged (ErrorControl=Normal).
    services:key("scsiport")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")

    -- SCSI disk class driver — loads after the miniports have published
    -- their devices (SCSI Class group, after SCSI miniport).
    services:key("scsidisk")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI Class")
        :set_multi_sz("DependOnService", { "scsiport" })
end

-- Bucket 10: framework, before any miniport (bucket 20) which imports
-- ScsiPortInitialize.  Bucket 30: class driver, after the miniports —
-- scsidisk's DriverEntry eagerly walks \Device\ScsiPort0..N.
function M.boot_drivers(paths)
    return {
        { name = "scsiport.sys", bucket = 10,
          src = paths.sdk_lib .. "/scsiport.sys" },
        { name = "scsidisk.sys", bucket = 30,
          src = paths.sdk_lib .. "/scsidisk.sys" },
    }
end

return M
