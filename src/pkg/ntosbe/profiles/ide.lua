-- ntosbe profile: ide
--
-- The current default — preserves the exact shape of the pre-port
-- mkhive.py:build_system_hive + mkdisk.py:_CORE_FILES.  Boots via
-- atdisk (legacy IDE) + fastfat, with the full driver set: storage,
-- input (i8042 + virtio kbd/mouse + class), video (videoprt + bochsvga),
-- networking (ndis + vionet + tdi + tcpip + afd), virtio peripherals,
-- and the LuaJIT runtime under \SystemRoot\lua\.
--
-- Future profiles (nvme.lua, virtio_scsi.lua, …) will be peers of this
-- one with different boot drivers and a trimmed driver set.

local M = {}

M.name        = "ide"
M.description = "Default boot disk: atdisk + bochsvga + full virtio driver set"

-- Init defaults — also overridable from the orchestrator (e.g. for the
-- `make selftest` flow that swaps main.lua → selftest.lua without
-- rebuilding the hive otherwise).
M.init = {
    exe   = "lua\\run.exe",
    args  = "\\SystemRoot\\lua\\main.lua",
    stdio = "\\Device\\Serial0",
}

-- ----------------------------------------------------------------
-- Hive content.
--
-- `apply` populates an existing hive; the orchestrator owns hive
-- creation and the build() / write() steps.  init = { exe, args, stdio }
-- — caller may pass partial overrides; missing fields fall back to the
-- profile defaults above.
-- ----------------------------------------------------------------

function M.apply(h, init)
    init = init or {}
    local exe   = init.exe   or M.init.exe
    local args  = init.args  or M.init.args
    local stdio = init.stdio or M.init.stdio

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
    control:key("Init")
        :set_sz("Exe",   "\\SystemRoot\\" .. exe)
        :set_sz("Args",  args)
        :set_sz("Stdio", stdio)

    -- Environment: the UEFI loader doesn't populate SystemDrive, so we
    -- set it here to match the DOS Devices C: symlink convention.
    -- Missing SystemDrive would leave %SystemRoot% unexpanded.
    control:key("Session Manager\\Environment")
        :set_sz       ("SystemDrive", "C:")
        :set_expand_sz("SystemRoot",  "%SystemDrive%\\")
        :set_expand_sz("Path",        "%SystemRoot%\\System32")

    -- ServiceGroupOrder — order system-start drivers load in.
    -- Video Init (port driver) before Video (miniports).  Virtio after
    -- Extended base so PCI bus-walk drivers come up once the kernel +
    -- HAL are fully alive.  SCSI miniport loads scsiport + nvme2k (and
    -- any future virtio-scsi); SCSI Class loads scsidisk after the
    -- miniports have published their devices.  NDIS → NDIS Miniport →
    -- TDI for the network stack; DependOnService inside each driver
    -- enforces the actual link-time order.
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

    -- ----------------- Services -----------------
    -- Type: 1 = SERVICE_KERNEL_DRIVER, 2 = SERVICE_FILE_SYSTEM_DRIVER
    -- Start: 0 = SERVICE_BOOT_START, 1 = SERVICE_SYSTEM_START
    -- ErrorControl: 1 = SERVICE_ERROR_NORMAL
    local services = h:key("ControlSet001\\Services")

    services:key("atdisk")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)

    -- SCSI miniport framework.  Provides ScsiPortInitialize + the SRB
    -- dispatch surface miniports register against.
    services:key("scsiport")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")

    -- nvme2k — NVMe storage controller miniport.  Registers via scsiport;
    -- SCSIDISK presents the namespace as \Device\Harddisk<N>.
    services:key("nvme2k")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")
        :set_multi_sz("DependOnService", { "scsiport" })

    -- SCSI disk class driver — walks miniports' device chains, parses
    -- partition tables, surfaces \Device\Harddisk<N>\Partition<P>.
    services:key("scsidisk")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI Class")
        :set_multi_sz("DependOnService", { "scsiport" })

    services:key("null")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)

    services:key("fastfat")
        :set_dword("Type", 2):set_dword("Start", 0):set_dword("ErrorControl", 1)

    services:key("npfs")
        :set_dword("Type", 2):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "File System")

    services:key("msfs")
        :set_dword("Type", 2):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "File System")

    -- serial.sys — COM port driver.  Loads Phase 1 after registry up.
    -- Walks HKLM\Hardware\Description\System\MultifunctionAdapter\N\
    -- SerialController\M\ConfigurationData (emitted by our UEFI loader).
    services:key("Serial")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Extended base")

    -- virtio-rng — entropy device, surfaces \Device\VirtioRng0.
    services:key("viorng")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Virtio")

    -- virtio-console — single-port virtio-serial, surfaces \Device\VirtioCon0.
    services:key("vioser")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Virtio")

    -- virtio-input — keyboard/mouse via virtio-keyboard-pci / virtio-mouse-pci.
    -- Drives kbdclass/mouclass via IOCTL_INTERNAL_*_CONNECT and exposes
    -- per-class symlinks (\Device\KeyboardPort<K>, \Device\PointerPort<P>).
    services:key("vioinput")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Virtio")

    -- kbdclass / mouclass — load after Virtio so port symlinks exist.
    services:key("kbdclass")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Keyboard Class")
    services:key("mouclass")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "Pointer Class")

    -- ----------------- Networking stack -----------------
    --
    --   ndis   -> framework (ndis.sys)
    --   vionet -> virtio-net miniport (vionet.sys)
    --   tdi    -> TDI wrapper (tdi.sys)
    --   tcpip  -> TCP/UDP/IP transport (tcpip.sys)
    --   afd    -> socket emulation layer (afd.sys, \Device\Afd)
    --
    -- NDIS reads <service>\Linkage\Bind to discover adapters and calls
    -- MPInitialize once per entry.  <service>\Parameters\<basename>\
    -- holds per-adapter config the miniport reads via NdisOpenConfiguration.
    -- tcpip's own Linkage\Bind names which adapter(s) the protocol attaches
    -- to.  DependOnService enforces driver load order on top of the broader
    -- ServiceGroupOrder bucket.

    services:key("ndis")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "NDIS")

    services:key("vionet")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "NDIS Miniport")
        :set_multi_sz("DependOnService", { "ndis" })

    -- vionet's Linkage subkey — Bind="\Device\Vionet1".  NDIS parses out
    -- the trailing "Vionet1" as BaseFileName and looks for
    -- Parameters\Vionet1\.
    services:key("vionet\\Linkage")
        :set_multi_sz("Bind",   { "\\Device\\Vionet1" })
        :set_multi_sz("Export", { "\\Device\\Vionet1" })
        :set_multi_sz("Route",  { '"vionet"' })

    -- Per-adapter config under Services\<adapter>\Parameters\.
    -- NDIS reads BusType + BusNumber via RtlQueryRegistryValues with
    -- path = RTL_REGISTRY_SERVICES + "Vionet1".  Missing values cause
    -- NdisInitializeInterrupt to fail with NDIS_STATUS_FAILURE.
    -- BusType=5 = NdisInterfacePci, bus 0 (QEMU's -machine pc has only
    -- bus 0).
    services:key("Vionet1\\Parameters")
        :set_dword("BusType",   5)
        :set_dword("BusNumber", 0)

    -- tcpip per-adapter IP config under Services\<adapter>\Parameters\Tcpip.
    -- Static config tuned for QEMU's -netdev user NAT defaults.
    services:key("Vionet1\\Parameters\\Tcpip")
        :set_dword("EnableDHCP", 0)
        :set_multi_sz("IPAddress",      { "10.0.2.15" })
        :set_multi_sz("SubnetMask",     { "255.255.255.0" })
        :set_multi_sz("DefaultGateway", { "10.0.2.2" })

    services:key("tdi")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "TDI")

    services:key("tcpip")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "TDI")
        :set_multi_sz("DependOnService", { "ndis", "tdi" })
    services:key("tcpip\\Linkage")
        :set_multi_sz("Bind", { "\\Device\\Vionet1" })
    -- Empty Parameters subkey — tcpip uses its built-in defaults until
    -- DHCP wiring + Adapters\<name> subkeys land.
    services:key("tcpip\\Parameters")

    -- afd.sys — Ancillary Function Driver, \Device\Afd.  Sits above TDI;
    -- userland (Lua via nt.afd) opens \Device\Afd with an EA buffer
    -- naming the underlying TDI transport (\Device\Tcp / \Device\Udp).
    services:key("afd")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "TDI")
        :set_multi_sz("DependOnService", { "tcpip" })

    -- (videoprt / bochsvga / i8042prt — not auto-started; the Lua UI
    -- layer registers + starts them when it's ready.)
end

-- ----------------------------------------------------------------
-- Disk file inventory.
--
-- `paths` is a table of resolved roots the orchestrator computes:
--   nt        = absolute path to src/NT
--   sdk_lib   = absolute path to src/NT/PUBLIC/SDK/LIB/I386
--   cr_dir    = absolute path to src/cr
--   pkg_root  = absolute path to src/pkg
--   obj(comp) = function returning src/NT/PRIVATE/<comp>/obj/i386
--
-- Returns an array of { dest = "...", src = "<abs path>" } entries.
-- The SYSTEM hive bytes are added separately by the orchestrator (it
-- holds the bytes in memory rather than a file).
--
-- Eight tables ship from WINDOWS/WINNLS/DATA/ (canonical NT 3.5
-- location).  C_1252 / C_437 / L_INTL feed RtlInitNlsTables for the
-- kernel + ntdll case-fold path.  UNICODE / LOCALE / CTYPE / SORTKEY /
-- SORTTBLS back the \NLS\NlsSection<X> named-section namespace
-- published at boot by nt.nls (replacing basesrv's role from stock
-- NT) so unmodified nlslib (kernel32's NLS half) opens them by name
-- from any process.  Sortkey/Sorttbls are static read-only weight
-- tables, same shape as the others — locale-with-exceptions builds a
-- separate RW per-locale section via basesrv (unhandled in MicroNT,
-- only matters for non-default locales like cs-CZ that need bespoke
-- collation rules).
-- ----------------------------------------------------------------

function M.disk_files(paths, list_tree)
    local nls = paths.nt .. "/PRIVATE/WINDOWS/WINNLS/DATA"
    local files = {
        { dest = "System32/ntoskrnl.exe",
          src  = paths.obj("NTOS/INIT/UP") .. "/ntoskrnl.exe" },
        { dest = "System32/hal.dll",
          src  = paths.obj("NTOS/NTHALS/HAL") .. "/hal.dll" },
        { dest = "System32/c_1252.nls",   src = nls .. "/C_1252.NLS"   },
        { dest = "System32/c_437.nls",    src = nls .. "/C_437.NLS"    },
        { dest = "System32/l_intl.nls",   src = nls .. "/L_INTL.NLS"   },
        { dest = "System32/unicode.nls",  src = nls .. "/UNICODE.NLS"  },
        { dest = "System32/locale.nls",   src = nls .. "/LOCALE.NLS"   },
        { dest = "System32/ctype.nls",    src = nls .. "/CTYPE.NLS"    },
        { dest = "System32/sortkey.nls",  src = nls .. "/SORTKEY.NLS"  },
        { dest = "System32/sorttbls.nls", src = nls .. "/SORTTBLS.NLS" },
        { dest = "System32/ntdll.dll",   src = paths.sdk_lib .. "/ntdll.dll"    },
        { dest = "System32/kernel32.dll", src = paths.sdk_lib .. "/kernel32.dll" },

        -- Storage / FS / COM.
        { dest = "System32/Drivers/atdisk.sys",   src = paths.sdk_lib .. "/atdisk.sys" },
        -- SCSI port + class + nvme miniport.
        { dest = "System32/Drivers/scsiport.sys", src = paths.sdk_lib .. "/scsiport.sys" },
        { dest = "System32/Drivers/scsidisk.sys", src = paths.sdk_lib .. "/scsidisk.sys" },
        { dest = "System32/Drivers/nvme2k.sys",   src = paths.sdk_lib .. "/nvme2k.sys" },
        { dest = "System32/Drivers/null.sys",     src = paths.sdk_lib .. "/null.sys" },
        { dest = "System32/Drivers/fastfat.sys",  src = paths.sdk_lib .. "/fastfat.sys" },
        { dest = "System32/Drivers/npfs.sys",     src = paths.sdk_lib .. "/npfs.sys" },
        { dest = "System32/Drivers/msfs.sys",     src = paths.sdk_lib .. "/msfs.sys" },
        { dest = "System32/Drivers/serial.sys",   src = paths.sdk_lib .. "/serial.sys" },

        -- Input + video — for the eventual pure-Lua UI.  Lua drives
        -- kbdclass/mouclass via NtDeviceIoControlFile and maps the
        -- framebuffer from bochsvga via IOCTL_VIDEO_MAP_VIDEO_MEMORY.
        { dest = "System32/Drivers/i8042prt.sys", src = paths.sdk_lib .. "/i8042prt.sys" },
        { dest = "System32/Drivers/kbdclass.sys", src = paths.sdk_lib .. "/kbdclass.sys" },
        { dest = "System32/Drivers/mouclass.sys", src = paths.sdk_lib .. "/mouclass.sys" },
        { dest = "System32/Drivers/videoprt.sys", src = paths.sdk_lib .. "/videoprt.sys" },
        { dest = "System32/Drivers/bochsvga.sys", src = paths.sdk_lib .. "/bochsvga.sys" },

        -- Virtio device drivers (link against the shared virtio.lib).
        { dest = "System32/Drivers/viorng.sys",   src = paths.sdk_lib .. "/viorng.sys" },
        { dest = "System32/Drivers/vioser.sys",   src = paths.sdk_lib .. "/vioser.sys" },
        { dest = "System32/Drivers/vioinput.sys", src = paths.sdk_lib .. "/vioinput.sys" },

        -- Networking stack: ndis framework + vionet miniport + tdi
        -- wrapper + tcpip transport + afd socket layer.
        { dest = "System32/Drivers/ndis.sys",     src = paths.sdk_lib .. "/ndis.sys" },
        { dest = "System32/Drivers/vionet.sys",   src = paths.sdk_lib .. "/vionet.sys" },
        { dest = "System32/Drivers/tdi.sys",      src = paths.sdk_lib .. "/tdi.sys" },
        { dest = "System32/Drivers/tcpip.sys",    src = paths.sdk_lib .. "/tcpip.sys" },
        { dest = "System32/Drivers/afd.sys",      src = paths.sdk_lib .. "/afd.sys" },

        -- LuaJIT runtime: thin trampoline EXE + shared VM DLL.  lua.dll
        -- lands in System32 because NT 3.5's kernel-side image loader
        -- searches *only* System32 for the initial process's imports
        -- (the "EXE dir first" rule is a kernel32 LoadLibrary policy,
        -- not used here).
        { dest = "lua/run.exe",      src = paths.cr_dir .. "/run.exe" },
        { dest = "System32/lua.dll", src = paths.cr_dir .. "/lua.dll" },
    }

    -- ----------------------------------------------------------------
    -- MSVC NT 3.5 toolchain — staged under \SystemRoot\pkg\msvc20\.
    -- pkg/ is the convention for optional bundles; toolchain isn't
    -- part of the OS surface.  Loader still finds kernel32.dll /
    -- ntdll.dll via System32 (standard NT search order), so toolchain
    -- EXEs don't need to be on the loader path.
    --
    -- Initial set targets the test ladder:
    --   ML.EXE, MC.EXE                — kernel32-only, no CRT
    --   CVTRES.EXE                    — CRTDLL only (transitive test)
    --   RC.EXE + RCDLL + RCPP         — kernel32 + RCDLL chain
    --   LINK.EXE                      — kernel32 + CRTDLL + IMAGEHLP
    --   CL chain (CL/CL386/C1/C1XX/C2)— full driver, MSVCRT20 + DBI
    --
    -- LIB.EXE / NMAKE.EXE / midl* deferred until they're on the
    -- ladder.  CRTDLL.DLL / IMAGEHLP.DLL not in OAK/BIN — staged
    -- below from PUBLIC/SDK/LIB if needed by LINK rung.
    -- ----------------------------------------------------------------
    local msvc = paths.nt .. "/PUBLIC/OAK/BIN/I386"
    local msvc_files = {
        { dest = "pkg/msvc20/ML.EXE",       src = msvc .. "/ML.EXE"       },
        { dest = "pkg/msvc20/ML.ERR",       src = msvc .. "/ML.ERR"       },
        { dest = "pkg/msvc20/MC.EXE",       src = msvc .. "/MC.EXE"       },
        { dest = "pkg/msvc20/CVTRES.EXE",   src = msvc .. "/CVTRES.EXE"   },
        { dest = "pkg/msvc20/cvtres.err",   src = msvc .. "/cvtres.err"   },
        { dest = "pkg/msvc20/RC.EXE",       src = msvc .. "/RC.EXE"       },
        { dest = "pkg/msvc20/RCDLL.DLL",    src = msvc .. "/RCDLL.DLL"    },
        { dest = "pkg/msvc20/RCPP.EXE",     src = msvc .. "/RCPP.EXE"     },
        { dest = "pkg/msvc20/RCPP.ERR",     src = msvc .. "/RCPP.ERR"     },
        { dest = "pkg/msvc20/LINK.EXE",     src = msvc .. "/LINK.EXE"     },
        { dest = "pkg/msvc20/LINK.ERR",     src = msvc .. "/LINK.ERR"     },
        { dest = "pkg/msvc20/CL.EXE",       src = msvc .. "/CL.EXE"       },
        { dest = "pkg/msvc20/CL386.EXE",    src = msvc .. "/CL386.EXE"    },
        { dest = "pkg/msvc20/CL.ERR",       src = msvc .. "/CL.ERR"       },
        { dest = "pkg/msvc20/C1.EXE",       src = msvc .. "/C1.EXE"       },
        { dest = "pkg/msvc20/C1.ERR",       src = msvc .. "/C1.ERR"       },
        { dest = "pkg/msvc20/C1XX.EXE",     src = msvc .. "/C1XX.EXE"     },
        { dest = "pkg/msvc20/C2.EXE",       src = msvc .. "/C2.EXE"       },
        { dest = "pkg/msvc20/CL32.MSG",     src = msvc .. "/CL32.MSG"     },
        { dest = "pkg/msvc20/MSVCRT20.DLL", src = msvc .. "/MSVCRT20.DLL" },
        { dest = "pkg/msvc20/DBI.DLL",      src = msvc .. "/DBI.DLL"      },
    }
    for _, e in ipairs(msvc_files) do files[#files + 1] = e end

    -- pkg/ tree: stage every file under src/pkg/ at \SystemRoot\lua\<rel>.
    -- The Lua application sets package.path = "\SystemRoot\lua\?.lua;..."
    -- so require('nt.dll.fs') resolves correctly.  list_tree is
    -- platform.list_tree, supplied by the orchestrator.
    for _, rel in ipairs(list_tree(paths.pkg_root)) do
        files[#files + 1] = {
            dest = "lua/" .. rel,
            src  = paths.pkg_root .. "/" .. rel,
        }
    end

    return files
end

return M
