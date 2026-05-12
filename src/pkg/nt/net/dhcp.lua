-- nt.net.dhcp — single-shot DHCP client (RFC 2131).
--
-- DISCOVER → OFFER → REQUEST → ACK on the wire; on ACK we push the
-- leased address + default route into the kernel via nt.net.info's
-- set_address / add_route.  Renewal (T1/T2 timers, REBIND) is out
-- of v1 scope — single-shot acquire is enough to bring the
-- interface up.  Restart by calling acquire() again.
--
-- The client uses the existing AFD UDP datagram surface for I/O:
--   bind 0.0.0.0:68
--   udp_sendto 255.255.255.255:67   (with broadcast flag in BOOTP flags)
--   udp_recvfrom on the same socket until we see a reply with our XID
--
-- Wire format primer:
--   BOOTP header (240 bytes fixed) — op, htype, hlen, hops, xid,
--     secs, flags, ciaddr, yiaddr, siaddr, giaddr, chaddr[16],
--     sname[64], file[128], magic = 0x63825363.
--   Options — TLV.  We emit MessageType + (in REQUEST) RequestedIP
--     + ServerID + ParameterRequestList.  We parse MessageType +
--     RequestedIP echo + SubnetMask + Router + ServerID + LeaseTime.
--
-- Server selection: take the first OFFER that matches our XID.  No
-- multi-server arbitration — DHCP RFC says pick highest you like,
-- but in the QEMU slirp environment there's only one server (the
-- gateway 10.0.2.2 spoofs everything).

local ffi    = require('ffi')
local bit    = require('bit')
local afd    = require('nt.net.afd')
local info   = require('nt.net.info')
local ke     = require('nt.dll.ke')

local M = {}

-- ------------------------------------------------------------------
-- Wire constants.
-- ------------------------------------------------------------------

local DHCP_MAGIC      = 0x63825363
local BOOTREQUEST     = 1
local BOOTREPLY       = 2
local HTYPE_ETHERNET  = 1
local HLEN_ETHERNET   = 6

-- DHCP option codes.
local OPT_SUBNET_MASK     = 1
local OPT_ROUTER          = 3
local OPT_LEASE_TIME      = 51
local OPT_MSG_TYPE        = 53
local OPT_SERVER_ID       = 54
local OPT_REQUESTED_IP    = 50
local OPT_PARAM_LIST      = 55
local OPT_END             = 255

-- Message types (option 53 values).
local DHCPDISCOVER = 1
local DHCPOFFER    = 2
local DHCPREQUEST  = 3
local DHCPACK      = 5
local DHCPNAK      = 6

M.DHCPDISCOVER = DHCPDISCOVER
M.DHCPOFFER    = DHCPOFFER
M.DHCPREQUEST  = DHCPREQUEST
M.DHCPACK      = DHCPACK
M.DHCPNAK      = DHCPNAK

-- BOOTP flags.
local BOOTP_FLAG_BROADCAST = 0x8000

-- UDP ports.
local CLIENT_PORT = 68
local SERVER_PORT = 67

-- ------------------------------------------------------------------
-- Endianness helpers — DHCP is all network order, Lua native is
-- whatever, easiest is build with explicit byte order.
-- ------------------------------------------------------------------

local function u8(n) return string.char(bit.band(n, 0xFF)) end

local function u16be(n)
    return string.char(bit.band(bit.rshift(n, 8), 0xFF))
        .. string.char(bit.band(n, 0xFF))
end

local function u32be(n)
    -- Use bit ops in case n is a Lua number near 2^32.
    return string.char(bit.band(bit.rshift(n, 24), 0xFF))
        .. string.char(bit.band(bit.rshift(n, 16), 0xFF))
        .. string.char(bit.band(bit.rshift(n,  8), 0xFF))
        .. string.char(bit.band(n, 0xFF))
end

local function r_u8 (s, i) return string.byte(s, i) end
local function r_u16(s, i)
    return string.byte(s, i) * 256 + string.byte(s, i+1)
end
local function r_u32(s, i)
    -- Big-endian (network order) → host uint32.  For RFC integer
    -- fields like lease time (option 51).  NOT for IP addresses
    -- (see r_ipaddr below).
    local b0, b1, b2, b3 =
        string.byte(s, i),   string.byte(s, i+1),
        string.byte(s, i+2), string.byte(s, i+3)
    return bit.bor(bit.lshift(b0, 24), bit.lshift(b1, 16),
                   bit.lshift(b2, 8),  b3)
end

-- Read 4 wire bytes as the kernel's IPAddr convention.  This stack
-- represents IPv4 addresses as a ULONG whose low byte is the FIRST
-- octet (IPROUTE.H:71 IP_LOOPBACK(x) = (x & 0xff) == 0x7f checks
-- the low byte for 127; IP.H:38 CLASSD_ADDR reads (uchar*)&a which
-- is byte 0 on LE).  Wire bytes in network byte order map one-to-
-- one to this representation when read as a little-endian uint32:
-- byte 0 of the wire becomes the low byte of the ULONG.
--
-- Concretely "10.0.2.15" on the wire is [0x0A,0x00,0x02,0x0F] and
-- maps to IPAddr = 0x0F02000A.  Any IOCTL handing an IPAddr to the
-- kernel (set_address, add_route) must use this convention.
local function r_ipaddr(s, i)
    local b0, b1, b2, b3 =
        string.byte(s, i),   string.byte(s, i+1),
        string.byte(s, i+2), string.byte(s, i+3)
    return bit.bor(b0,
                   bit.lshift(b1, 8),
                   bit.lshift(b2, 16),
                   bit.lshift(b3, 24))
end

-- Inverse: kernel IPAddr → 4 wire bytes (network order).  Low byte
-- of the ULONG (= first octet) goes first on the wire.
local function w_ipaddr(n)
    return string.char(bit.band(n, 0xFF),
                       bit.band(bit.rshift(n, 8), 0xFF),
                       bit.band(bit.rshift(n, 16), 0xFF),
                       bit.band(bit.rshift(n, 24), 0xFF))
end

-- Kernel IPAddr → dotted-quad string.  Low byte of n is the first
-- octet of the string.
local function ipaddr_str(n)
    return string.format("%d.%d.%d.%d",
        bit.band(n, 0xFF),
        bit.band(bit.rshift(n, 8), 0xFF),
        bit.band(bit.rshift(n, 16), 0xFF),
        bit.band(bit.rshift(n, 24), 0xFF))
end

-- ------------------------------------------------------------------
-- Packet build.
-- ------------------------------------------------------------------

-- Encode a single TLV.  data is a Lua string of payload bytes.
local function opt(code, data)
    return string.char(code) .. string.char(#data) .. data
end

-- Build a DHCP packet.  `t` carries the variable fields:
--   xid       (number)
--   ciaddr    (number, network-order ULONG; 0 for DISCOVER/REQUEST)
--   mac       (6-byte string)
--   msg_type  (DHCPDISCOVER / DHCPREQUEST / ...)
--   requested_ip (number, optional — REQUEST only)
--   server_id    (number, optional — REQUEST only)
--   broadcast (bool — set the flags bit; we use true so the server
--              broadcasts the reply since we don't have an IP yet)
local function build_packet(t)
    local flags = t.broadcast and BOOTP_FLAG_BROADCAST or 0

    -- chaddr: 6 bytes of MAC + 10 bytes of padding.
    local chaddr_pad = t.mac .. string.rep("\0", 16 - #t.mac)
    -- sname, file: zero-filled.
    local sname = string.rep("\0", 64)
    local file_ = string.rep("\0", 128)

    local header =
        u8(BOOTREQUEST) .. u8(HTYPE_ETHERNET) .. u8(HLEN_ETHERNET) .. u8(0)
        .. u32be(t.xid)
        .. u16be(0)                              -- secs
        .. u16be(flags)
        .. w_ipaddr(t.ciaddr or 0)               -- ciaddr (IP — kernel convention)
        .. w_ipaddr(0)                           -- yiaddr (server fills)
        .. w_ipaddr(0)                           -- siaddr
        .. w_ipaddr(0)                           -- giaddr
        .. chaddr_pad
        .. sname
        .. file_
        .. u32be(DHCP_MAGIC)

    local options = opt(OPT_MSG_TYPE, u8(t.msg_type))
    if t.requested_ip then
        options = options .. opt(OPT_REQUESTED_IP, w_ipaddr(t.requested_ip))
    end
    if t.server_id then
        options = options .. opt(OPT_SERVER_ID, w_ipaddr(t.server_id))
    end
    -- Tell the server what we care about.  Subnet mask + router are
    -- mandatory for our config push; lease time is informational.
    options = options .. opt(OPT_PARAM_LIST,
        string.char(OPT_SUBNET_MASK, OPT_ROUTER, OPT_LEASE_TIME))
    options = options .. string.char(OPT_END)

    return header .. options
end

-- ------------------------------------------------------------------
-- Packet parse — returns { op, xid, yiaddr, siaddr, chaddr,
--                          options = {[code]=raw_value_string} }
-- or nil if the packet doesn't look like DHCP (too short, wrong
-- magic).  Caller checks op + msg_type to decide what to do.
-- ------------------------------------------------------------------

local function parse_packet(s)
    if #s < 240 then return nil, "too short" end
    if r_u32(s, 237) ~= DHCP_MAGIC then  -- magic at offset 236 (1-based 237)
        return nil, "bad magic"
    end

    local p = {
        op       = r_u8(s, 1),
        htype    = r_u8(s, 2),
        hlen     = r_u8(s, 3),
        xid      = r_u32(s, 5),       -- opaque (BE pack OK)
        ciaddr   = r_ipaddr(s, 13),   -- IP fields use kernel convention
        yiaddr   = r_ipaddr(s, 17),
        siaddr   = r_ipaddr(s, 21),
        giaddr   = r_ipaddr(s, 25),
        chaddr   = s:sub(29, 28 + 6),
        options  = {},
    }

    -- Options start at offset 240 (1-based 241).
    local i = 241
    while i <= #s do
        local code = string.byte(s, i)
        if code == OPT_END then break end
        if code == 0 then
            -- pad option, single byte
            i = i + 1
        else
            local len = string.byte(s, i + 1)
            if not len or i + 1 + len > #s then break end
            p.options[code] = s:sub(i + 2, i + 1 + len)
            i = i + 2 + len
        end
    end

    return p
end

-- Extract the DHCP message type from a parsed packet.  Nil if absent.
local function msg_type_of(p)
    local v = p.options[OPT_MSG_TYPE]
    if v and #v >= 1 then return string.byte(v, 1) end
    return nil
end

-- ------------------------------------------------------------------
-- Discovery helpers.
-- ------------------------------------------------------------------

-- Pick an Ethernet interface from info.interfaces(h).  Returns
-- (if_index, mac_string).  Errors if none found.
local function find_ethernet(h)
    local list = info.interfaces(h)
    for _, e in ipairs(list) do
        if tonumber(e.if_type) == info.IF_TYPE_ETHERNET
           and tonumber(e.if_physaddrlen) == HLEN_ETHERNET then
            local mac = ffi.string(e.if_physaddr, HLEN_ETHERNET)
            return tonumber(e.if_index), mac
        end
    end
    error("nt.net.dhcp: no Ethernet interface found", 2)
end

-- Find the NTE context for the given if_index.  Returns the
-- iae_context (the byte IP_SET_ADDRESS_REQUEST needs).  Errors if
-- the interface has no address-table entry.
local function find_nte_context(h, if_index)
    local addrs = info.addresses(h)
    for _, a in ipairs(addrs) do
        if tonumber(a.iae_index) == if_index then
            return tonumber(a.iae_context)
        end
    end
    error(string.format(
        "nt.net.dhcp: no NTE found for if_index=%d", if_index), 2)
end

-- ------------------------------------------------------------------
-- Wire I/O.  Bind a fresh UDP socket to 0.0.0.0:68 (BOOTP client
-- port) and reuse it across DISCOVER → REQUEST.  Replies come back
-- to the same socket; we filter by XID.
-- ------------------------------------------------------------------

-- Bind with the DHCP marker so the kernel AddrObj gets AO_DHCP_FLAG
-- (UDP.C:769-780): bypass route lookup, send from NULL_IP_ADDR,
-- accept inbound UDP/68 on an interface that doesn't yet have a
-- valid NTE.  Without the marker the DISCOVER send pends forever
-- because IPGetLocalRoute returns NULL_IP_ADDR and UDPSend treats
-- that as TDI_DEST_UNREACHABLE.
local function open_socket()
    local s = afd.udp()
    afd.bind(s, "0.0.0.0", CLIENT_PORT, nil, { dhcp = true })
    return s
end

-- Receive packets until one arrives with our XID and a matching
-- message-type set.  Each recv uses the full timeout — on QEMU
-- slirp the server replies in well under a second so this is fine;
-- a future production path would track a deadline.  An unmatched
-- packet (different XID or wrong msg type) loops; if we never see
-- a match the per-recv timeout fires and afd.udp_recvfrom raises.
local function wait_for(sock, xid, accept_types, timeout_secs, label, diag_h)
    local ok, result = pcall(function()
        while true do
            local data = afd.udp_recvfrom(sock, 1500, timeout_secs)
            local p = parse_packet(data)
            if p and p.op == BOOTREPLY and p.xid == xid then
                local mt = msg_type_of(p)
                for _, want in ipairs(accept_types) do
                    if mt == want then return p end
                end
                if mt == DHCPNAK then
                    error("nt.net.dhcp: server NAK", 3)
                end
                -- Wrong type with our XID — drop and keep listening.
            end
            -- Different XID / malformed — keep listening.
        end
    end)
    if ok then return result end
    -- Receive failed (timeout, etc).  Print kernel-side counters so
    -- we can see where the reply got stuck before re-raising.
    if diag_h then
        local ok2, ip, udp = pcall(function()
            return info.ip_stats(diag_h), info.udp_stats(diag_h)
        end)
        if ok2 then
            print(string.format(
                "[dhcp:%s timeout] inreceives=%d indelivers=%d indiscards=%d  " ..
                "udp_indatagrams=%d noports=%d inerrors=%d outdatagrams=%d",
                label,
                tonumber(ip.ipsi_inreceives), tonumber(ip.ipsi_indelivers),
                tonumber(ip.ipsi_indiscards),
                tonumber(udp.us_indatagrams), tonumber(udp.us_noports),
                tonumber(udp.us_inerrors),    tonumber(udp.us_outdatagrams)))
        end
    end
    error(result, 2)
end

-- ------------------------------------------------------------------
-- Public surface.
-- ------------------------------------------------------------------

-- acquire(opts) → lease table.  opts:
--   timeout    overall timeout in seconds (default 10)
--   tcp_handle  open \Device\Tcp handle (caller-supplied so the
--               module doesn't take ownership of the lifecycle).
--               If nil we open one and close it on the way out.
--   ip_handle   same for \Device\Ip.
--   push        whether to push the lease into the kernel (default
--               true).  Set false for dry-run / observation tests.
--
-- Lease table:
--   address    leased IP (number, network-order ULONG)
--   address_str dotted-quad string for logging
--   mask       subnet mask (number)
--   mask_str   dotted-quad string
--   gateway    default-route nexthop (number, nil if server omitted opt 3)
--   gateway_str  string
--   server_id  the server we accepted from (number)
--   lease_secs lease lifetime (number, nil if server omitted opt 51)
--   if_index   the interface we configured
function M.acquire(opts)
    opts = opts or {}
    local timeout = opts.timeout or 10
    local push    = (opts.push ~= false)

    local owns_tcp = (opts.tcp_handle == nil)
    local owns_ip  = (opts.ip_handle  == nil)
    local h_tcp    = opts.tcp_handle or info.open()
    local h_ip     = opts.ip_handle  or (push and info.open_ip()) or nil

    -- 1. Discover the NIC + its NTE.
    local if_index, mac    = find_ethernet(h_tcp)
    local nte_ctx          = push and find_nte_context(h_tcp, if_index) or nil

    -- 2. XID for this acquire — uniqueness within a session is the
    -- only requirement (single acquire at a time).  Low 32 bits of
    -- system time make a fine identifier.
    local t = ke.NtQuerySystemTime()
    local xid = bit.bxor(tonumber(t.LowPart), tonumber(t.HighPart))

    -- 3. Open UDP socket, send DISCOVER.
    local sock = open_socket()
    local discover = build_packet{
        xid = xid, mac = mac, msg_type = DHCPDISCOVER, broadcast = true,
    }
    afd.udp_sendto(sock, "255.255.255.255", SERVER_PORT, discover, timeout)

    -- 4. Wait for OFFER.
    local offer = wait_for(sock, xid, { DHCPOFFER }, timeout, "OFFER", h_tcp)
    local server_id_raw = offer.options[OPT_SERVER_ID]
    if not server_id_raw or #server_id_raw < 4 then
        error("nt.net.dhcp: OFFER missing server-id (option 54)", 2)
    end
    local server_id = r_ipaddr(server_id_raw, 1)

    -- 5. Send REQUEST.
    local req = build_packet{
        xid = xid, mac = mac, msg_type = DHCPREQUEST, broadcast = true,
        requested_ip = offer.yiaddr,
        server_id    = server_id,
    }
    afd.udp_sendto(sock, "255.255.255.255", SERVER_PORT, req, timeout)

    -- 6. Wait for ACK.
    local ack = wait_for(sock, xid, { DHCPACK }, timeout, "ACK", h_tcp)
    sock:close()

    -- 7. Build the lease table from the ACK options.
    local mask_raw = ack.options[OPT_SUBNET_MASK]
    if not mask_raw or #mask_raw < 4 then
        error("nt.net.dhcp: ACK missing subnet mask (option 1)", 2)
    end
    local mask = r_ipaddr(mask_raw, 1)

    local gw_raw = ack.options[OPT_ROUTER]
    local gateway = (gw_raw and #gw_raw >= 4) and r_ipaddr(gw_raw, 1) or nil

    local lease_raw = ack.options[OPT_LEASE_TIME]
    -- lease_time is a network-order uint32 (RFC 2132 §9.2), so BE.
    local lease_secs = (lease_raw and #lease_raw >= 4) and r_u32(lease_raw, 1) or nil

    local lease = {
        if_index    = if_index,
        address     = ack.yiaddr,
        address_str = ipaddr_str(ack.yiaddr),
        mask        = mask,
        mask_str    = ipaddr_str(mask),
        gateway     = gateway,
        gateway_str = gateway and ipaddr_str(gateway) or nil,
        server_id   = server_id,
        lease_secs  = lease_secs,
        xid         = xid,
        mac         = mac,
    }

    -- 8. Push to kernel.
    if push then
        local st = info.set_address(h_ip, nte_ctx, lease.address, lease.mask)
        if st ~= info.STATUS_SUCCESS then
            error(string.format(
                "nt.net.dhcp: set_address failed st=0x%08X", st), 2)
        end
        if gateway then
            local rst = info.add_route(h_tcp, {
                dest     = 0,
                mask     = 0,
                nexthop  = gateway,
                if_index = if_index,
                metric   = 1,
                type     = info.IRE_TYPE_INDIRECT,
                proto    = info.IRE_PROTO_LOCAL,  -- DHCP convention
            })
            if rst ~= info.STATUS_SUCCESS then
                error(string.format(
                    "nt.net.dhcp: add_route failed st=0x%08X", rst), 2)
            end
        end
    end

    if owns_ip  and h_ip  then h_ip:close()  end
    if owns_tcp           then h_tcp:close() end

    return lease
end

-- Re-exported parser for tests / debugging.
M.parse_packet = parse_packet
M.build_packet = build_packet

return M
