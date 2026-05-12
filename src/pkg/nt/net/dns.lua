-- nt.net.dns — minimal pure-Lua DNS A-record resolver over UDP.
--
-- Built on nt.net.afd. Single-shot per call (no retries), no cache, no
-- search-list expansion — the caller passes a fully-qualified name.
-- That's deliberate: this module exists so higher layers can resolve
-- without hard-coded literals, not to be a recursive resolver.
--
-- Coverage (v1):
--   resolve_a(name, [server], [timeout])  → "x.x.x.x"  (first A record)
--   resolve_all_a(name, [server], [timeout]) → { "x.x.x.x", ... }
--
-- Both raise on failure (parse error, NXDOMAIN, no A record, timeout).
-- Caller-side `pcall` for soft handling.
--
-- Wire format follows RFC 1035: 12-byte header, QNAME (length-prefixed
-- labels, NUL-terminated), QTYPE=A=1, QCLASS=IN=1. Response uses the
-- same QNAME plus answer RRs; we walk past the question section,
-- iterate answer RRs, return the first A record's RDATA. Compression
-- pointers (0xC0) are skipped, not followed — we never need to read
-- the names back out, just step past them.
--
-- Default server is 8.8.8.8 to keep the API ergonomic in tests; in
-- production callers should pass the system resolver from /etc/resolv
-- equivalent (or whatever registry-driven mechanism we end up wiring).

local bit = require('bit')
local afd = require('nt.net.afd')

local M = {}

-- Module-local transaction-id counter. Starts at an arbitrary value
-- so a stray response from a previous test run doesn't accidentally
-- match a fresh query's txid (we'd still mismatch the question name,
-- but txid is the cheap first-line check).
local _next_txid = 0xCAFE
local function next_txid()
    local id = _next_txid
    _next_txid = (_next_txid + 1) % 0x10000
    return id
end

-- ------------------------------------------------------------------
-- Encode / decode helpers
-- ------------------------------------------------------------------

local function read_u16(pkt, pos)
    return (pkt:byte(pos) * 256) + pkt:byte(pos + 1)
end

-- "example.com" → "\7example\3com\0". Each label gets a 1-byte length
-- prefix; trailing zero terminates. Labels > 63 bytes are rejected.
local function encode_qname(name)
    local parts = {}
    local i = 1
    for label in name:gmatch("[^.]+") do
        if #label == 0 or #label > 63 then
            error("dns: invalid label length in " .. name, 3)
        end
        parts[i] = string.char(#label) .. label
        i = i + 1
    end
    if i == 1 then
        error("dns: empty name", 3)
    end
    return table.concat(parts) .. "\0"
end

-- Build a standard recursive A/IN query. Returns (bytes, txid).
local function build_query(name, txid)
    local hi = bit.band(bit.rshift(txid, 8), 0xFF)
    local lo = bit.band(txid, 0xFF)
    local hdr = string.char(hi, lo,
                            0x01, 0x00,    -- QR=0, OPCODE=0, RD=1
                            0x00, 0x01,    -- QDCOUNT = 1
                            0x00, 0x00,    -- ANCOUNT
                            0x00, 0x00,    -- NSCOUNT
                            0x00, 0x00)    -- ARCOUNT
    return hdr .. encode_qname(name) .. string.char(0x00, 0x01,
                                                    0x00, 0x01)
end

-- Step past a domain name in `pkt` starting at 1-based `pos`. Handles
-- compression pointers (top two bits == 11) by treating them as
-- terminating 2-byte tokens. We don't need to read the name back, so
-- we don't follow the pointer — just return the position after it.
local function skip_name(pkt, pos)
    while true do
        local b = pkt:byte(pos)
        if b == nil then
            error("dns: truncated name in response", 4)
        end
        if b == 0 then
            return pos + 1
        end
        if bit.band(b, 0xC0) == 0xC0 then
            return pos + 2
        end
        pos = pos + 1 + b
    end
end

-- Walk the response and return all A-record RDATAs as dotted-quad
-- strings. Raises on header / parse / rcode failures.
local function parse_answers_a(pkt, expected_txid)
    if #pkt < 12 then error("dns: short response (" .. #pkt .. "B)", 3) end
    local txid = read_u16(pkt, 1)
    if txid ~= expected_txid then
        error(string.format(
            "dns: txid mismatch: got 0x%04x expected 0x%04x",
            txid, expected_txid), 3)
    end
    local flags = read_u16(pkt, 3)
    if bit.band(flags, 0x8000) == 0 then
        error("dns: response QR bit not set", 3)
    end
    local rcode = bit.band(flags, 0x000F)
    if rcode ~= 0 then
        -- Common rcodes: 1=FORMERR, 2=SERVFAIL, 3=NXDOMAIN, 4=NOTIMP, 5=REFUSED.
        error(string.format("dns: rcode %d", rcode), 3)
    end
    local qdcount = read_u16(pkt, 5)
    local ancount = read_u16(pkt, 7)

    local pos = 13
    -- Skip question section: each question is QNAME + QTYPE(2) + QCLASS(2).
    for _ = 1, qdcount do
        pos = skip_name(pkt, pos)
        pos = pos + 4
    end

    -- Walk answer section. Each RR: NAME + TYPE(2) + CLASS(2) +
    -- TTL(4) + RDLEN(2) + RDATA(RDLEN). We collect every type-A /
    -- class-IN entry; the rest (CNAME, AAAA, etc.) get skipped.
    local results = {}
    for _ = 1, ancount do
        pos = skip_name(pkt, pos)
        local rtype  = read_u16(pkt, pos)
        local rclass = read_u16(pkt, pos + 2)
        local rdlen  = read_u16(pkt, pos + 8)
        if rtype == 1 and rclass == 1 and rdlen == 4 then
            results[#results + 1] = string.format("%d.%d.%d.%d",
                pkt:byte(pos + 10), pkt:byte(pos + 11),
                pkt:byte(pos + 12), pkt:byte(pos + 13))
        end
        pos = pos + 10 + rdlen
    end
    return results
end

-- ------------------------------------------------------------------
-- Public API
-- ------------------------------------------------------------------

local DEFAULT_SERVER  = "8.8.8.8"
local DEFAULT_TIMEOUT = 5.0

-- Resolve `name` to a list of A-record IPs (Lua array of dotted-quad
-- strings). May return an empty array if the response had no A
-- records (e.g. the name has only AAAA / MX / CNAME). Raises on
-- network or protocol errors.
function M.resolve_all_a(name, server, timeout_secs)
    server       = server       or DEFAULT_SERVER
    timeout_secs = timeout_secs or DEFAULT_TIMEOUT
    local txid   = next_txid()
    local query  = build_query(name, txid)

    local s = afd.udp()
    afd.bind(s, "0.0.0.0", 0)
    afd.connect(s, server, 53, timeout_secs)
    afd.send(s, query, timeout_secs)
    local resp = afd.recv(s, 512, timeout_secs)
    s:close()
    return parse_answers_a(resp, txid)
end

-- Convenience: first A record. Raises if the answer has none.
function M.resolve_a(name, server, timeout_secs)
    local ips = M.resolve_all_a(name, server, timeout_secs)
    if #ips == 0 then
        error("dns: no A record for " .. name, 2)
    end
    return ips[1]
end

return M
