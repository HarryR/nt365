-- ntosbe layer: drivers.storage.virtio
--
-- virtio-blk SCSI miniport (vioblk.sys) — canonical for GCP Persistent
-- Disk and generic KVM hosts.  Same role as drivers.storage.nvme for
-- virtio-blk-pci devices.  An empty PCI walk returns
-- STATUS_NO_SUCH_DEVICE and is logged + skipped (ErrorControl=Normal).

local M = {}

M.name = "drivers.storage.virtio"
M.description = "virtio-blk controller miniport (vioblk)"
M.requires = { "drivers.storage.scsi" }

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    services:key("vioblk")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")
        :set_multi_sz("DependOnService", { "scsiport" })
end

-- Bucket 20 (miniport tier): after scsiport (10), before scsidisk (30).
function M.boot_drivers(paths)
    return {
        { name = "vioblk.sys", bucket = 20, src = paths.sdk_lib .. "/vioblk.sys" },
    }
end

return M
