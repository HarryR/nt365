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
    --
    -- Boot-start (Start=0) so the loader pre-loads it alongside atdisk.
    -- This lets the same image boot from either an IDE or NVMe disk:
    -- whichever controller is present at runtime, the matching driver
    -- claims the boot volume; the other returns STATUS_NO_SUCH_DEVICE
    -- and gets logged (ErrorControl=Normal, not Critical).
    services:key("scsiport")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")

    -- nvme2k — NVMe storage controller miniport.  Registers via scsiport;
    -- SCSIDISK presents the namespace as \Device\Harddisk<N>.
    services:key("nvme2k")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")
        :set_multi_sz("DependOnService", { "scsiport" })

    -- vioblk — virtio-blk SCSI miniport.  Same role as nvme2k for
    -- virtio-blk-pci devices (canonical for GCP Persistent Disk +
    -- generic KVM hosts).  Empty PCI walk → STATUS_NO_SUCH_DEVICE,
    -- logged + skipped under ErrorControl=Normal.
    services:key("vioblk")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI miniport")
        :set_multi_sz("DependOnService", { "scsiport" })

    -- SCSI disk class driver — walks miniports' device chains, parses
    -- partition tables, surfaces \Device\Harddisk<N>\Partition<P>.
    services:key("scsidisk")
        :set_dword("Type", 1):set_dword("Start", 0):set_dword("ErrorControl", 1)
        :set_sz("Group", "SCSI Class")
        :set_multi_sz("DependOnService", { "scsiport" })

    services:key("null")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)

    services:key("fastfat")
        :set_dword("Type", 2):set_dword("Start", 0):set_dword("ErrorControl", 1)

    -- NTFS filesystem driver.  Boot-start (Start=0) so it's eligible
    -- to claim the boot volume; ErrorControl=1 (Normal) so a no-NTFS-
    -- volume world (today: every disk is FAT16) gets a logged decline,
    -- not a bugcheck.  fastfat will still claim FAT16 volumes; ntfs's
    -- mount probe sees the FAT BPB and returns STATUS_UNRECOGNIZED_VOLUME.
    services:key("ntfs")
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
    -- Per-entry `where` tag controls which partition(s) host the
    -- file.  Default (omitted) = 'root'.
    --
    --   'esp'   only on the ESP (read by boot-efi pre-handoff,
    --           never re-opened from disk at runtime).  Examples:
    --           kernel, HAL, the seven boot-start drivers.
    --   'root'  only on the system partition under \SystemRoot.
    --   'both'  on both — for files boot-efi stages AND user-mode
    --           re-opens via \SystemRoot.  The three NLS code-pages
    --           are like this: boot-efi pulls them for kernel-side
    --           NLS_DATA_BLOCK, then nt.nls.publish() opens them
    --           again to seed the named-section namespace.
    local files = {
        { dest = "System32/ntoskrnl.exe", where = 'esp',
          src  = paths.obj("NTOS/INIT/UP") .. "/ntoskrnl.exe" },
        { dest = "System32/hal.dll", where = 'esp',
          src  = paths.obj("NTOS/NTHALS/HAL") .. "/hal.dll" },
        { dest = "System32/c_1252.nls", where = 'both',
                                          src = nls .. "/C_1252.NLS"   },
        { dest = "System32/c_437.nls",  where = 'both',
                                          src = nls .. "/C_437.NLS"    },
        { dest = "System32/l_intl.nls", where = 'both',
                                          src = nls .. "/L_INTL.NLS"   },
        { dest = "System32/unicode.nls",  src = nls .. "/UNICODE.NLS"  },
        { dest = "System32/locale.nls",   src = nls .. "/LOCALE.NLS"   },
        { dest = "System32/ctype.nls",    src = nls .. "/CTYPE.NLS"    },
        { dest = "System32/sortkey.nls",  src = nls .. "/SORTKEY.NLS"  },
        { dest = "System32/sorttbls.nls", src = nls .. "/SORTTBLS.NLS" },
        { dest = "System32/ntdll.dll",   src = paths.sdk_lib .. "/ntdll.dll"    },
        { dest = "System32/kernel32.dll", src = paths.sdk_lib .. "/kernel32.dll" },

        -- Storage / FS / COM.  Boot-start drivers (atdisk, scsi*, nvme2k,
        -- vioblk, fastfat, ntfs) are read by boot-efi pre-handoff and
        -- so live on the ESP only.  Type-1 drivers (input, video,
        -- network, npfs/msfs/serial/null) are loaded post-boot by
        -- IoLoadDriver via \SystemRoot\System32\Drivers and stay on
        -- root.
        { dest = "System32/Drivers/atdisk.sys",   where = 'esp',
                                  src = paths.sdk_lib .. "/atdisk.sys" },
        { dest = "System32/Drivers/scsiport.sys", where = 'esp',
                                  src = paths.sdk_lib .. "/scsiport.sys" },
        { dest = "System32/Drivers/scsidisk.sys", where = 'esp',
                                  src = paths.sdk_lib .. "/scsidisk.sys" },
        { dest = "System32/Drivers/nvme2k.sys",   where = 'esp',
                                  src = paths.sdk_lib .. "/nvme2k.sys" },
        { dest = "System32/Drivers/vioblk.sys",   where = 'esp',
                                  src = paths.sdk_lib .. "/vioblk.sys" },
        { dest = "System32/Drivers/fastfat.sys",  where = 'esp',
                                  src = paths.sdk_lib .. "/fastfat.sys" },
        { dest = "System32/Drivers/ntfs.sys",     where = 'esp',
                                  src = paths.sdk_lib .. "/ntfs.sys" },
        { dest = "System32/Drivers/null.sys",     src = paths.sdk_lib .. "/null.sys" },
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
        -- NMAKE.EXE — deferred earlier; pulled in for the self-host
        -- attempt where ntosbe.build runs in-OS and drives nmake.
        { dest = "pkg/msvc20/NMAKE.EXE",    src = msvc .. "/NMAKE.EXE"    },
        { dest = "pkg/msvc20/NMAKE.ERR",    src = msvc .. "/NMAKE.ERR"    },
        -- CRTDLL.DLL is the CRT used by NMAKE / LINK / CVTRES.  It
        -- lives in PUBLIC/SDK/LIB on the host tree (not OAK/BIN with
        -- the toolchain EXEs) — so the source path differs, but it's
        -- still a toolchain-runtime artifact and belongs alongside
        -- the EXEs that import it.  ps.spawn passes the msvc20 dir
        -- as DllPath so the loader resolves it from there.
        { dest = "pkg/msvc20/CRTDLL.DLL",   src = paths.sdk_lib .. "/CRTDLL.DLL" },
        -- NT 3.5 cmd.exe (lifted from stuff/, in-tree at WINDOWS/CMD/).
        -- NMAKE shells inline commands through COMSPEC; we point
        -- COMSPEC at this exact path via tchain's NT_ENV.  Drops the
        -- former cmd-stub staging — that's a host-side wibo-iteration
        -- binary, conceptually wrong on guest.  Real cmd.exe handles
        -- `if exist`, `for`, `set`, `%VAR%` expansion that cmd-stub
        -- can't, so any future MAKEFILE.DEF rule we add gets full
        -- shell semantics.
        { dest = "pkg/msvc20/cmd.exe",
          src  = paths.nt .. "/PRIVATE/WINDOWS/CMD/obj/i386/cmd.exe" },
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

    -- ----------------------------------------------------------------
    -- NT source tree → \SystemRoot\src\NT\…
    --
    -- Self-host enabler: the booted guest needs the SOURCES files,
    -- C/asm/inc inputs, MAKEFILEs, and PUBLIC/{SDK,OAK}/{INC,LIB} on
    -- disk so ntosbe.build can drive NMAKE.EXE in-process.  Filtered
    -- staging — drops obj/ outputs, wibo-tools/, RC/MIDL temp files,
    -- and (until renamed) the few 8.3-violating MicroNT additions
    -- (NVME2K/VIRTIO/HAL extras).  null.sys + ntoskrnl path is clean.
    --
    -- Anything top-level under src/ that's host-only (cr/, boot-efi/,
    -- wibo-tools/, cmd-stub/, tools/, build.sh, bootstrap.sh, boot.sh,
    -- OVMF*.fd) is deliberately excluded — guest can't run gcc/mingw,
    -- doesn't need the host bootstrap helpers.
    -- ----------------------------------------------------------------

    local function is_8_3(name)
        local stem, ext = name:match("^(.+)%.([^.]+)$")
        if not stem then return #name <= 8 end
        return #stem <= 8 and #ext <= 3
    end

    local nt_skip_dirs = {
        ["obj"] = true, ["Obj"] = true,
    }
    local nt_skip_basenames = {
        -- nmake / rc temp droppings (RC[a-z]NNNNN, nmNNN).  Filter
        -- here so a stale tree doesn't block the disk build; the
        -- clean target removes them properly on host.
    }

    -- Pre-walk to drop files inside any obj/ subdir.  list_tree
    -- doesn't expose intermediate dir names, so we test path segments.
    -- Also drops source tarballs (.tgz / .tar.gz / .zip / .bak) that
    -- the user might have stashed in the tree as backups — they
    -- violate 8.3 and aren't OS sources anyway.
    --
    -- Build outputs that happen to land in the source tree (notably
    -- splitsym's .DBG and dbg2dwf's .dwf sidecars next to drivers in
    -- PUBLIC/SDK/LIB/I386) are also dropped — they're produced by
    -- the build, not consumed by it, so self-hosted re-build doesn't
    -- need them.  pdbs likewise.
    local function nt_path_excluded(rel)
        for seg in rel:gmatch("[^/]+") do
            if nt_skip_dirs[seg] then return true end
        end
        local lower = rel:lower()
        if lower:match("%.tgz$")     then return true end
        if lower:match("%.tar%.gz$") then return true end
        if lower:match("%.zip$")     then return true end
        if lower:match("%.bak$")     then return true end
        if lower:match("%.dbg$")     then return true end
        if lower:match("%.dwf$")     then return true end
        if lower:match("%.pdb$")     then return true end
        return false
    end

    -- FAT16 is case-insensitive: a dir holding both `SERLOG.h` (mc.exe
    -- output) and `serlog.h` (the lower-case copy ensure_serlog stages
    -- alongside it) collides.  Keep the lower-case form when both
    -- exist; the codegen-normalised name is the canonical one the
    -- include path looks for.
    local seen = {}                 -- key = upper(rel) → idx in files
    local skipped_8_3 = {}
    for _, rel in ipairs(list_tree(paths.nt)) do
        if not nt_path_excluded(rel) then
            local base = rel:match("([^/]+)$") or rel
            if is_8_3(base) then
                local key = rel:upper()
                local existing = seen[key]
                local entry = {
                    dest = "src/NT/" .. rel,
                    src  = paths.nt .. "/" .. rel,
                }
                if existing then
                    -- Prefer the all-lowercase basename when one
                    -- variant clashes with another.
                    if base == base:lower() then
                        files[existing] = entry
                    end
                    -- Otherwise leave the existing entry alone.
                else
                    files[#files + 1] = entry
                    seen[key] = #files
                end
            else
                skipped_8_3[#skipped_8_3 + 1] = rel
            end
        end
    end
    if #skipped_8_3 > 0 then
        io.stderr:write(string.format(
            "ide.lua: %d 8.3-violating NT source file(s) cannot be staged on FAT16:\n",
            #skipped_8_3))
        for _, rel in ipairs(skipped_8_3) do
            io.stderr:write("  " .. rel .. "\n")
        end
        io.stderr:write(
            "Rename them to fit 8.3 (max 8-char stem + 3-char ext) or move to an FS that allows long names.\n")
        error(string.format(
            "ide.lua: %d source file(s) cannot be staged on FAT16 disk image (see stderr)",
            #skipped_8_3))
    end

    return files
end

return M
