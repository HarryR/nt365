-- ntosbe profile: selfhost
--
-- The full disk: everything `selftest` stages, plus the NT source tree
-- (ntsrc) and the MS toolchain (msvc).  This is what the in-OS
-- self-host build needs — test.ntosbe drives NMAKE against
-- \SystemRoot\src and test.msvc spawns the toolchain EXEs.
--
-- selfhost is also the default profile for a bare `ntosbe` invocation,
-- preserving the pre-layer full-disk behaviour.

return {
    layers = {
        "core", "lua",
        "drivers.storage.*", "drivers.fs.*",
        "drivers.net", "drivers.input", "drivers.video", "drivers.virtio.*",
        "ntsrc", "msvc",
    },
    init = { args = "\\SystemRoot\\lua\\selfhost.lua" },
}
