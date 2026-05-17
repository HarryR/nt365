-- ntosbe profile: smoke
--
-- Minimal "did the system boot on this hardware shape?" disk.  Same
-- layer set as `selftest` (full driver config, so the smoke check
-- exercises real hardware bring-up) — only the init entry differs:
-- smoke.lua prints SMOKE OK and powers off.
--
-- The file stem is `smoke` (5 chars); `smoketest` (9) would violate the
-- FAT16 8.3 limit when this profile rides the pkg/ tree onto the disk.

return {
    layers = {
        "core", "lua",
        "drivers.storage.*", "drivers.fs.*",
        "drivers.net", "drivers.input", "drivers.video", "drivers.virtio.*",
    },
    init = { args = "\\SystemRoot\\lua\\smoke.lua" },
}
