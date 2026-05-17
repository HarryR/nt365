-- ntosbe profile: selftest
--
-- The fast iteration disk: full hardware/driver config but WITHOUT the
-- NT source tree or the MS toolchain.  The kernel and test.fuzz.*
-- suites never touch \SystemRoot\src, so staging the source tree
-- (which dominates disk-compose wall time) buys nothing here.  For the
-- in-OS self-host build use the `selfhost` profile instead.
--
-- A profile is just a layer list + the init entry; see ntosbe/compose.lua.

return {
    layers = {
        "core", "lua",
        "drivers.storage.*", "drivers.fs.*",
        "drivers.net", "drivers.input", "drivers.video", "drivers.virtio.*",
    },
    init = { args = "\\SystemRoot\\lua\\selftest.lua" },
}
