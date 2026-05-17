-- ntosbe layer: drivers.fs.fat
--
-- FAT filesystem driver (fastfat.sys).  Boot-start so it is eligible
-- to claim the boot volume; it registers a recognizer with the I/O
-- manager and probes each volume's BPB at mount time.

local M = {}

M.name = "drivers.fs.fat"
M.description = "FAT filesystem driver (fastfat)"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    -- Type 2 = SERVICE_FILE_SYSTEM_DRIVER, Start 0 = SERVICE_BOOT_START.
    services:key("fastfat")
        :set_dword("Type", 2):set_dword("Start", 0):set_dword("ErrorControl", 1)
end

-- Bucket 90: filesystems load last — they touch no hardware at
-- DriverEntry, just register a recognizer.
function M.boot_drivers(paths)
    return {
        { name = "fastfat.sys", bucket = 90, src = paths.sdk_lib .. "/fastfat.sys" },
    }
end

return M
