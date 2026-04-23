-- nt.dll.str — UTF-16 ↔ UTF-8 string conversion for NT APIs.
--
-- NT's string ABI is UTF-16 (UNICODE_STRING, wchar_t buffers). Lua
-- strings are byte-oriented; UTF-8 fits natively because a Lua string
-- is just a byte sequence. Everything in this module moves between
-- those two representations losslessly for any valid Unicode content.
-- ASCII passes through unchanged in both directions — the common case
-- is essentially free.
--
-- Invalid UTF-16 (orphan surrogates) and invalid UTF-8 both produce
-- U+FFFD REPLACEMENT CHARACTER. We never silently drop bytes.
--
-- Exports:
--   from_wchars(wp, nchars)  wchar_t* + char count → UTF-8 Lua string.
--   from_utf16(us)           UNICODE_STRING*      → UTF-8 Lua string.
--   decode_utf8(s)           UTF-8 Lua string → table of UTF-16 code
--                            units (pre-surrogate-paired). Shared with
--                            nt.dll.oa for fused-struct construction.
--   to_wbuf(s)               UTF-8 Lua string → (wchar_t[n+1] buffer, n).
--                            Raw array; caller owns lifetime. See
--                            NOTES.md "Shape 2: caller-owned buffer".
--   to_utf16(s)              UTF-8 Lua string → NT_STRING cdata.
--                            Fused allocation — UNICODE_STRING and wbuf
--                            in one cdata, us.Buffer inline. Caller
--                            holds one ref; no UAF possible between
--                            UNICODE_STRING and its backing storage.
--                            Pass to syscalls via ns.us (LuaJIT takes
--                            address automatically) or
--                            ffi.cast('UNICODE_STRING *', ns) for
--                            stored pointers.
--   new_utf16(capacity)      Empty NT_STRING ready for a callee to
--                            fill (NtQuerySymbolicLinkObject etc.).
--                            Same fused guarantee; Length = 0,
--                            MaximumLength = capacity * 2 bytes.
--
-- NT_STRING lifetime contract: the UNICODE_STRING and its backing wbuf
-- live in one cdata. Ref-count is 1. When the caller drops the cdata,
-- both disappear together. No way to end up with UNICODE_STRING.Buffer
-- pointing at freed memory.

local ffi = require('ffi')
local bit = require('bit')
require('nt.dll')   -- ensures UNICODE_STRING is cdef'd

local band, bor        = bit.band, bit.bor
local rshift, lshift   = bit.rshift, bit.lshift

ffi.cdef[[
#pragma pack(push, 4)
typedef struct _NT_STRING {
    UNICODE_STRING us;
    wchar_t        data[?];
} NT_STRING;
#pragma pack(pop)
]]

local M = {}

local REPL = "\xEF\xBF\xBD"   -- UTF-8 encoding of U+FFFD

-- UTF-16 → UTF-8. Iterates code units, pairs surrogates, handles the
-- four UTF-8 length classes (1/2/3/4 bytes). Null-pointer tolerant:
-- nil wp or nchars <= 0 returns "" rather than dereferencing.
function M.from_wchars(wp, nchars)
    if wp == nil or nchars <= 0 then return "" end
    local out = {}
    local i = 0
    while i < nchars do
        local c = wp[i]
        i = i + 1

        if c >= 0xD800 and c <= 0xDBFF then
            -- High surrogate — expect a low surrogate next.
            if i < nchars and wp[i] >= 0xDC00 and wp[i] <= 0xDFFF then
                local cp = 0x10000
                         + lshift(band(c,        0x3FF), 10)
                         + band  (wp[i],         0x3FF)
                i = i + 1
                out[#out+1] = string.char(
                    bor(0xF0, rshift(cp, 18)),
                    bor(0x80, band(rshift(cp, 12), 0x3F)),
                    bor(0x80, band(rshift(cp,  6), 0x3F)),
                    bor(0x80, band(cp,              0x3F)))
            else
                out[#out+1] = REPL   -- orphan high surrogate
            end
        elseif c >= 0xDC00 and c <= 0xDFFF then
            out[#out+1] = REPL       -- orphan low surrogate
        elseif c < 0x80 then
            out[#out+1] = string.char(c)
        elseif c < 0x800 then
            out[#out+1] = string.char(
                bor(0xC0, rshift(c, 6)),
                bor(0x80, band(c, 0x3F)))
        else
            out[#out+1] = string.char(
                bor(0xE0, rshift(c, 12)),
                bor(0x80, band(rshift(c, 6), 0x3F)),
                bor(0x80, band(c,            0x3F)))
        end
    end
    return table.concat(out)
end

-- UNICODE_STRING → UTF-8 Lua string. us.Length is in bytes, so halve
-- for wchar count. Delegates to from_wchars for the null guard.
function M.from_utf16(us)
    return M.from_wchars(us.Buffer, us.Length / 2)
end

-- UTF-8 → UTF-16 code units (Lua integer table, pre-surrogate-paired).
-- Exposed because nt.dll.oa also needs it to fill a fused NT_OA_PATH
-- without going through to_utf16 + copy.
function M.decode_utf8(s)
    local out = {}
    local i, n = 1, #s
    while i <= n do
        local b1 = s:byte(i)
        local cp
        if b1 < 0x80 then
            cp = b1
            i = i + 1
        elseif b1 < 0xC2 then
            -- Invalid: stray continuation or overlong 2-byte leader.
            cp = 0xFFFD
            i = i + 1
        elseif b1 < 0xE0 then
            local b2 = s:byte(i+1) or 0
            cp = bor(lshift(band(b1, 0x1F), 6), band(b2, 0x3F))
            i = i + 2
        elseif b1 < 0xF0 then
            local b2, b3 = s:byte(i+1) or 0, s:byte(i+2) or 0
            cp = bor(lshift(band(b1, 0x0F), 12),
                     lshift(band(b2, 0x3F),  6),
                     band  (b3, 0x3F))
            i = i + 3
        elseif b1 < 0xF8 then
            local b2 = s:byte(i+1) or 0
            local b3 = s:byte(i+2) or 0
            local b4 = s:byte(i+3) or 0
            cp = bor(lshift(band(b1, 0x07), 18),
                     lshift(band(b2, 0x3F), 12),
                     lshift(band(b3, 0x3F),  6),
                     band  (b4, 0x3F))
            i = i + 4
        else
            cp = 0xFFFD
            i = i + 1
        end

        if cp <= 0xFFFF then
            out[#out+1] = cp
        else
            -- Supplementary plane → surrogate pair.
            cp = cp - 0x10000
            out[#out+1] = bor(0xD800, rshift(cp, 10))
            out[#out+1] = bor(0xDC00, band(cp, 0x3FF))
        end
    end
    return out
end

-- UTF-8 Lua string → (wchar_t[n+1] buffer, n wchars). Raw buffer;
-- caller is responsible for its lifetime. Unused by to_utf16 — that
-- path allocates inside the NT_STRING cdata directly. Kept for
-- callers that only want the array (e.g. passing a wchar_t* to a
-- syscall that doesn't take a UNICODE_STRING).
function M.to_wbuf(s)
    local wchars = M.decode_utf8(s)
    local n = #wchars
    local buf = ffi.new('wchar_t[?]', n + 1)
    for k = 1, n do
        buf[k-1] = wchars[k]
    end
    buf[n] = 0
    return buf, n
end

-- Allocate an empty NT_STRING with `capacity` wchar slots, ready for a
-- callee (typically an NT query syscall) to fill. Length starts at 0,
-- MaximumLength is set to capacity * 2 bytes. us.Buffer points inline
-- at data[] — same fused-lifetime guarantee as to_utf16.
function M.new_utf16(capacity)
    local ns = ffi.new('NT_STRING', capacity)
    ns.us.Buffer        = ns.data
    ns.us.Length        = 0
    ns.us.MaximumLength = capacity * 2
    return ns
end

-- UTF-8 Lua string → NT_STRING (single cdata; UNICODE_STRING and wbuf
-- live in one allocation). us.Buffer points at the cdata's own data[],
-- so the two lifetimes are fused by construction. Caller holds one ref.
function M.to_utf16(s)
    local wchars = M.decode_utf8(s)
    local n = #wchars
    local ns = ffi.new('NT_STRING', n + 1)   -- n wchars + trailing NUL
    for k = 1, n do
        ns.data[k-1] = wchars[k]
    end
    ns.data[n] = 0
    ns.us.Buffer        = ns.data
    ns.us.Length        = n * 2
    ns.us.MaximumLength = (n + 1) * 2
    return ns
end

return M
