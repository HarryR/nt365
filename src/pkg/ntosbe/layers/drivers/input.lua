-- ntosbe layer: drivers.input
--
-- Keyboard and pointer input.  The class drivers (kbdclass / mouclass)
-- auto-start and bind to whatever port driver published a port symlink;
-- vioinput drives kbdclass/mouclass via IOCTL_INTERNAL_*_CONNECT for
-- virtio-keyboard-pci / virtio-mouse-pci devices.
--
-- i8042prt (the PS/2 port driver) ships as a file but is not
-- auto-started — the Lua UI layer registers and starts it on demand.

local M = {}

M.name = "drivers.input"
M.description = "i8042 + virtio input + keyboard/pointer class drivers"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")

    -- virtio-input — keyboard/mouse via virtio-keyboard-pci /
    -- virtio-mouse-pci.  Loads in the Virtio group so PCI bus-walk
    -- drivers come up after the kernel + HAL are fully alive.
    services:key("vioinput")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Virtio")

    -- kbdclass / mouclass — load after Virtio so the port symlinks
    -- (\Device\KeyboardPort<K>, \Device\PointerPort<P>) already exist.
    services:key("kbdclass")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Keyboard Class")
    services:key("mouclass")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Pointer Class")
end

function M.files(paths)
    return {
        { dest = "System32/Drivers/i8042prt.sys", src = paths.sdk_lib .. "/i8042prt.sys" },
        { dest = "System32/Drivers/kbdclass.sys", src = paths.sdk_lib .. "/kbdclass.sys" },
        { dest = "System32/Drivers/mouclass.sys", src = paths.sdk_lib .. "/mouclass.sys" },
        { dest = "System32/Drivers/vioinput.sys", src = paths.sdk_lib .. "/vioinput.sys" },
    }
end

return M
