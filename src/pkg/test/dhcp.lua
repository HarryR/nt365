-- DHCP acquire test.  Drives the v1 single-shot client against
-- QEMU slirp's built-in DHCP server (10.0.2.2) and verifies the
-- lease + post-config kernel state.
--
-- Expected slirp defaults (qemu user networking):
--   leased address    10.0.2.15
--   subnet mask       255.255.255.0
--   default gateway   10.0.2.2
--   server id         10.0.2.2
--
-- This test mutates kernel state (sets the NTE address + installs a
-- default route) and is one-shot per boot.  It must run BEFORE any
-- other test that depends on having a configured interface — see
-- selftest.lua for the ordering contract.
--
-- The vionet adapter must come up with IPAddress="0.0.0.0" and
-- SubnetMask="0.0.0.0" — see src/pkg/ntosbe/profiles/ide.lua.  The
-- NTE is created without NTE_VALID (NTIP.C:1607-1608); set_address
-- promotes it once the lease lands.

local bit  = require('bit')
local t    = require('test')
local info = require('nt.net.info')
local dhcp = require('nt.net.dhcp')

-- Normalize a 32-bit IPv4 address / mask to LuaJIT's signed int32
-- representation.  Kernel cdata ULONG fields come through tonumber()
-- as unsigned doubles (e.g. 0xFFFFFF00 → 4294967040), while
-- bit.bor/bit.lshift in dhcp.lua's packet parser return signed
-- int32 (0xFFFFFF00 → -256).  Comparing them directly fails any
-- time the top byte has the high bit set (255.x.x.x, 192.x.x.x via
-- byte 0, etc).  bit.tobit on both sides puts them in the same
-- representation so equality is correct.
local function u32(n) return bit.tobit(n) end

t.suite("dhcp")

-- Shared session for the post-acquire MIB checks.
local tcp_h = info.open()

-- Cache the lease across tests so we exercise the kernel state
-- mutation once.  Subsequent tests assert against the same lease.
local lease

t.test("acquire from QEMU slirp", function()
    lease = dhcp.acquire{ timeout = 5 }
    t.eq(lease.address_str, "10.0.2.15", "leased address")
    t.eq(lease.mask_str,    "255.255.255.0", "subnet mask")
    t.eq(lease.gateway_str, "10.0.2.2", "default gateway")
    t.ok(lease.lease_secs and lease.lease_secs > 0,
        "lease time present (" .. tostring(lease.lease_secs) .. "s)")
end)

t.test("address table reflects lease", function()
    t.ok(lease, "acquire ran first")
    local addrs = info.addresses(tcp_h)
    local found
    for _, a in ipairs(addrs) do
        if tonumber(a.iae_index) == lease.if_index then
            found = a
            break
        end
    end
    t.ok(found,
        "addresses(h) has an entry for if_index=" .. tostring(lease.if_index))
    -- iae_addr / iae_mask are network-order ULONG; compare as int32.
    t.eq(u32(tonumber(found.iae_addr)), u32(lease.address),
        "iae_addr matches leased address")
    t.eq(u32(tonumber(found.iae_mask)), u32(lease.mask),
        "iae_mask matches leased mask")
end)

t.test("route table contains default route", function()
    t.ok(lease, "acquire ran first")
    local rs = info.routes(tcp_h)
    local default
    for _, r in ipairs(rs) do
        if tonumber(r.ire_dest) == 0 and tonumber(r.ire_mask) == 0 then
            default = r
            break
        end
    end
    t.ok(default, "default route (0.0.0.0/0) present")
    t.eq(u32(tonumber(default.ire_nexthop)), u32(lease.gateway),
        "default-route nexthop matches gateway")
    t.eq(tonumber(default.ire_index), lease.if_index,
        "default-route if_index matches the leased interface")
end)
