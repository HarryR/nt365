-- ntosbe layer: drivers.fs.ntfs
--
-- NTFS filesystem driver (ntfs.sys).  Boot-start so it is eligible to
-- claim the boot volume; ErrorControl=Normal so a FAT-only world gets
-- a logged decline, not a bugcheck — ntfs's mount probe sees a FAT BPB
-- and returns STATUS_UNRECOGNIZED_VOLUME, leaving the volume to fastfat.

local M = {}

M.name = "drivers.fs.ntfs"
M.description = "NTFS filesystem driver (ntfs)"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")
    -- Type 2 = SERVICE_FILE_SYSTEM_DRIVER, Start 0 = SERVICE_BOOT_START.
    services:key("ntfs")
        :set_dword("Type", 2):set_dword("Start", 0):set_dword("ErrorControl", 1)
end

-- Bucket 90: filesystems load last — they touch no hardware at
-- DriverEntry, just register a recognizer.
function M.boot_drivers(paths)
    return {
        { name = "ntfs.sys", bucket = 90, src = paths.sdk_lib .. "/ntfs.sys" },
    }
end

return M
