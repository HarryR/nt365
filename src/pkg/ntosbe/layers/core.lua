-- ntosbe layer: core
--
-- The OS itself, with no application runtime.  Kernel, HAL, ntdll,
-- kernel32, the NLS tables, and the SYSTEM-hive skeleton (Select,
-- Control\Init, Session Manager environment, ServiceGroupOrder) plus
-- the base non-hardware system services (null, npfs, msfs, serial).
--
-- core deliberately carries NO application runtime: the build system
-- must be able to compose disks for applications that don't depend on
-- Lua.  The LuaJIT runtime is its own layer (layers/lua.lua); the
-- init-process Exe comes from whichever runtime layer a profile picks.
--
-- A layer module exports:
--   name         identifier, also the require() suffix
--   requires     other layers to pull in (inclusion closure)
--   init         optional partial init defaults {exe,args,stdio}
--   registry     function(h, ctx)         — populate the SYSTEM hive
--   files        function(paths,list_tree)— disk file entries
--   boot_drivers function(paths)           — boot-start driver set
-- All are optional except name.

local M = {}

M.name = "core"
M.description = "NT kernel, HAL, ntdll, kernel32, NLS + hive skeleton"

-- core supplies the stdio default (an NT device path, not application-
-- specific).  The Exe default belongs to the runtime layer.
M.init = {
    stdio = "\\Device\\Serial0",
}

function M.registry(h, ctx)
    local init = ctx.init

    h:key("Select")
        :set_dword("current",       1)
        :set_dword("default",       1)
        :set_dword("lastknowngood", 1)
        :set_dword("failed",        0)

    local control = h:key("ControlSet001\\Control")

    -- Control\Init — kernel's initial-process configuration, read by
    -- INIT.C::QueryInitConfig.  Exe is the image path (SystemRoot-
    -- relative; the kernel prepends \SystemRoot\).  Args is the argv
    -- tail.  Stdio is an NT device path the kernel opens inheritable
    -- and pipes into ProcessParameters.Standard{Input,Output,Error}.
    -- The composition engine has already resolved init from the
    -- runtime layer + profile + --init-args overrides.
    control:key("Init")
        :set_sz("Exe",   "\\SystemRoot\\" .. init.exe)
        :set_sz("Args",  init.args)
        :set_sz("Stdio", init.stdio)

    -- Environment: the UEFI loader doesn't populate SystemDrive, so we
    -- set it here to match the DOS Devices C: symlink convention.
    -- Missing SystemDrive would leave %SystemRoot% unexpanded.
    control:key("Session Manager\\Environment")
        :set_sz       ("SystemDrive", "C:")
        :set_expand_sz("SystemRoot",  "%SystemDrive%\\")
        :set_expand_sz("Path",        "%SystemRoot%\\System32")

    -- ServiceGroupOrder — order system-start drivers load in.  The
    -- group set is stable; layers only place their services into a
    -- group named here.  Video Init (port driver) before Video
    -- (miniports).  Virtio after Extended base so PCI bus-walk drivers
    -- come up once the kernel + HAL are fully alive.  SCSI miniport
    -- loads scsiport + miniports; SCSI Class loads scsidisk after.
    -- NDIS -> NDIS Miniport -> TDI for the network stack.
    control:key("ServiceGroupOrder")
        :set_multi_sz("List", {
            "Base",
            "Extended base",
            "Virtio",
            "SCSI miniport",
            "SCSI Class",
            "File System",
            "NDIS",
            "NDIS Miniport",
            "TDI",
            "Video Init",
            "Video",
            "Keyboard Class",
            "Pointer Class",
        })

    -- GroupOrderList — CmpFindDrivers requires this key to exist under
    -- Control even if no per-group tag ordering is needed.  Empty.
    control:key("GroupOrderList")

    -- ----------------- Base system services -----------------
    -- Type: 1 = SERVICE_KERNEL_DRIVER, 2 = SERVICE_FILE_SYSTEM_DRIVER
    -- Start: 0 = SERVICE_BOOT_START, 1 = SERVICE_SYSTEM_START
    -- ErrorControl: 1 = SERVICE_ERROR_NORMAL
    local services = h:key("ControlSet001\\Services")

    services:key("null")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)

    services:key("npfs")
        :set_dword("Type", 2):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "File System")

    services:key("msfs")
        :set_dword("Type", 2):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "File System")

    -- serial.sys — COM port driver.  Loads Phase 1 after registry up.
    -- Walks HKLM\Hardware\Description\System\MultifunctionAdapter\N\
    -- SerialController\M\ConfigurationData (emitted by our UEFI loader).
    -- core owns it because the serial console backs stdio.
    services:key("Serial")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Extended base")
end

-- Eight tables ship from WINDOWS/WINNLS/DATA/ (canonical NT 3.5
-- location).  C_1252 / C_437 / L_INTL feed RtlInitNlsTables for the
-- kernel + ntdll case-fold path.  UNICODE / LOCALE / CTYPE / SORTKEY /
-- SORTTBLS back the \NLS\NlsSection<X> named-section namespace.
--
-- The per-entry `where` tag routes between partitions:
--   'esp'  read by boot-efi pre-handoff only (kernel, HAL).
--   'root' on the system partition under \SystemRoot (default).
--   'both' staged + re-opened at runtime (the three NLS code-pages:
--          boot-efi pulls them for the kernel NLS_DATA_BLOCK, then
--          nt.nls.publish() re-opens them to seed the namespace).
function M.files(paths)
    local nls = paths.nt .. "/PRIVATE/WINDOWS/WINNLS/DATA"
    return {
        { dest = "System32/ntoskrnl.exe", where = 'esp',
          src  = paths.obj("NTOS/INIT/UP") .. "/ntoskrnl.exe" },
        { dest = "System32/hal.dll", where = 'esp',
          src  = paths.obj("NTOS/NTHALS/HAL") .. "/hal.dll" },
        { dest = "System32/c_1252.nls", where = 'both', src = nls .. "/C_1252.NLS" },
        { dest = "System32/c_437.nls",  where = 'both', src = nls .. "/C_437.NLS"  },
        { dest = "System32/l_intl.nls", where = 'both', src = nls .. "/L_INTL.NLS" },
        { dest = "System32/unicode.nls",  src = nls .. "/UNICODE.NLS"  },
        { dest = "System32/locale.nls",   src = nls .. "/LOCALE.NLS"   },
        { dest = "System32/ctype.nls",    src = nls .. "/CTYPE.NLS"    },
        { dest = "System32/sortkey.nls",  src = nls .. "/SORTKEY.NLS"  },
        { dest = "System32/sorttbls.nls", src = nls .. "/SORTTBLS.NLS" },
        { dest = "System32/ntdll.dll",    src = paths.sdk_lib .. "/ntdll.dll"    },
        { dest = "System32/kernel32.dll", src = paths.sdk_lib .. "/kernel32.dll" },
        { dest = "System32/advapi32.dll", src = paths.sdk_lib .. "/advapi32.dll" },
        { dest = "System32/user32.dll",   src = paths.sdk_lib .. "/user32.dll"   },
        { dest = "System32/shell32.dll",  src = paths.sdk_lib .. "/shell32.dll"  },

        -- Base system drivers — loaded post-boot by IoLoadDriver from
        -- \SystemRoot\System32\Drivers, so they stay on the root volume.
        { dest = "System32/Drivers/null.sys",   src = paths.sdk_lib .. "/null.sys" },
        { dest = "System32/Drivers/npfs.sys",   src = paths.sdk_lib .. "/npfs.sys" },
        { dest = "System32/Drivers/msfs.sys",   src = paths.sdk_lib .. "/msfs.sys" },
        { dest = "System32/Drivers/serial.sys", src = paths.sdk_lib .. "/serial.sys" },
    }
end

return M
