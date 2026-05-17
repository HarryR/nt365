-- ntosbe layer: drivers.net
--
-- The networking stack:
--   ndis   -> framework (ndis.sys)
--   vionet -> virtio-net miniport (vionet.sys)
--   tdi    -> TDI wrapper (tdi.sys)
--   tcpip  -> TCP/UDP/IP transport (tcpip.sys)
--   afd    -> socket emulation layer (afd.sys, \Device\Afd)
--
-- All Start=1 (system-start) — loaded post-boot by IoLoadDriver from
-- \SystemRoot\System32\Drivers, so the driver files stay on root.

local M = {}

M.name = "drivers.net"
M.description = "NDIS + virtio-net + TDI + TCP/IP + AFD socket layer"

function M.registry(h)
    local services = h:key("ControlSet001\\Services")

    -- NDIS reads <service>\Linkage\Bind to discover adapters and calls
    -- MPInitialize once per entry.  <service>\Parameters\<basename>\
    -- holds per-adapter config the miniport reads via
    -- NdisOpenConfiguration.  DependOnService enforces driver load
    -- order on top of the broader ServiceGroupOrder bucket.
    services:key("ndis")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "NDIS")

    services:key("vionet")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "NDIS Miniport")
        :set_multi_sz("DependOnService", { "ndis" })

    -- vionet's Linkage subkey — Bind="\Device\Vionet1".  NDIS parses
    -- out the trailing "Vionet1" as BaseFileName and looks for
    -- Parameters\Vionet1\.
    services:key("vionet\\Linkage")
        :set_multi_sz("Bind",   { "\\Device\\Vionet1" })
        :set_multi_sz("Export", { "\\Device\\Vionet1" })
        :set_multi_sz("Route",  { '"vionet"' })

    -- Per-adapter config under Services\<adapter>\Parameters\.
    -- NDIS reads BusType + BusNumber via RtlQueryRegistryValues.
    -- Missing values cause NdisInitializeInterrupt to fail with
    -- NDIS_STATUS_FAILURE.  BusType=5 = NdisInterfacePci, bus 0
    -- (QEMU's -machine pc has only bus 0).
    services:key("Vionet1\\Parameters")
        :set_dword("BusType",   5)
        :set_dword("BusNumber", 0)

    -- tcpip per-adapter IP config.  Zero-address NTE — NTIP.C:1607-1608
    -- creates the NTE without NTE_VALID when nte_addr == NULL_IP_ADDR.
    -- The Lua-side DHCP client (nt.net.dhcp.acquire) gets a real lease
    -- from QEMU slirp and promotes the NTE via IOCTL_IP_SET_ADDRESS.
    services:key("Vionet1\\Parameters\\Tcpip")
        :set_dword("EnableDHCP", 1)
        :set_multi_sz("IPAddress",  { "0.0.0.0" })
        :set_multi_sz("SubnetMask", { "0.0.0.0" })

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

    -- afd.sys — Ancillary Function Driver, \Device\Afd.  Sits above
    -- TDI; userland (Lua via nt.net.afd) opens \Device\Afd with an EA
    -- buffer naming the underlying TDI transport (\Device\Tcp / Udp).
    services:key("afd")
        :set_dword("Type", 1):set_dword("Start", 1):set_dword("ErrorControl", 1)
        :set_sz("Group", "TDI")
        :set_multi_sz("DependOnService", { "tcpip" })
end

function M.files(paths)
    return {
        { dest = "System32/Drivers/ndis.sys",   src = paths.sdk_lib .. "/ndis.sys" },
        { dest = "System32/Drivers/vionet.sys", src = paths.sdk_lib .. "/vionet.sys" },
        { dest = "System32/Drivers/tdi.sys",    src = paths.sdk_lib .. "/tdi.sys" },
        { dest = "System32/Drivers/tcpip.sys",  src = paths.sdk_lib .. "/tcpip.sys" },
        { dest = "System32/Drivers/afd.sys",    src = paths.sdk_lib .. "/afd.sys" },
    }
end

return M
