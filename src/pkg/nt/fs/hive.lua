-- nt.fs.hive — NT 3.5 registry hive serializer.
--
-- Pure-Lua port of tools/mkhive.py.  Same on-disk format, same
-- byte-level layout, no behaviour change (verified by parity against
-- the Python output).  Profile knowledge — i.e. *which* keys + values
-- the SYSTEM hive should contain — lives in pkg/ntosbe/layers/, not
-- here.  This module just turns a declarative tree into the binary
-- hive format the kernel parses during Phase 0/1 init.
--
-- Lives under nt.fs/ alongside the volume builders: from the
-- composer's perspective a hive is just bytes that get added to a
-- FAT16 / NTFS volume at \System32\config\SYSTEM.
--
-- Format notes (from the Python original, see CmpDoOpenHandle):
--   - Hive version 1.2
--   - Names stored compressed ASCII (KEY_COMP_NAME / VALUE_COMP_NAME)
--   - Subkey index uses CM_KEY_INDEX_LEAF ("li" signature) — plain
--     HCELL_INDEX entries with no hashes.  "lf" / "lh" are post-XP.
--   - Children must be sorted by name (case-insensitive) for the
--     kernel's binary search.
--
-- Usage:
--     local hive = require('nt.fs').hive
--     local h = hive.new("SYSTEM")
--     h:key("Select"):set_dword("current", 1):set_dword("default", 1)
--     h:key("ControlSet001\\Services\\foo"):set_sz("Group", "Base")
--     local bytes = h:build()                  -- returns the full hive
--     -- or
--     h:write(platform.write_file, "SYSTEM")   -- writes via platform

local ffi = require('ffi')
local bit = require('bit')

local M = {}

-- ----------------------------------------------------------------
-- REG_* value types and key/value flags.  Mirror NTDDK.h.
-- ----------------------------------------------------------------

M.REG_NONE      = 0
M.REG_SZ        = 1
M.REG_EXPAND_SZ = 2
M.REG_BINARY    = 3
M.REG_DWORD     = 4
M.REG_MULTI_SZ  = 7

local KEY_HIVE_ENTRY = 0x0004
local KEY_COMP_NAME  = 0x0020
local VALUE_COMP_NAME = 0x0001

local PAGE          = 4096
local CELL_ALIGN    = 8
local HBIN_HDR_SIZE = 32

local function align(n, a)
    return bit.band(n + a - 1, bit.bnot(a - 1))
end

-- ----------------------------------------------------------------
-- Buffer — grow-on-demand uint8_t[?] with offset-based field writes.
--
-- Mirrors the Python bytearray + struct.pack_into pattern: append to
-- grow, write_uN(offset, value) to patch, append_zeros(n) to reserve
-- space, to_string() at the end to extract the result.
--
-- ffi.new('uint8_t[?]', n) zero-fills on alloc, so reserve-then-patch
-- and "free cell" trailing zeros come for free.
-- ----------------------------------------------------------------

local Buffer = {}
Buffer.__index = Buffer

local function newbuf(initial_cap)
    return setmetatable({
        data = ffi.new('uint8_t[?]', initial_cap or 4096),
        len  = 0,
        cap  = initial_cap or 4096,
    }, Buffer)
end

function Buffer:_ensure(min_size)
    if min_size <= self.cap then return end
    local new_cap = self.cap
    while new_cap < min_size do new_cap = new_cap * 2 end
    local new_data = ffi.new('uint8_t[?]', new_cap)
    ffi.copy(new_data, self.data, self.len)
    self.data = new_data
    self.cap  = new_cap
end

function Buffer:append_zeros(n)
    self:_ensure(self.len + n)
    -- buffer is zero-initialized; just advance the length.
    self.len = self.len + n
end

function Buffer:append_str(s)
    local n = #s
    self:_ensure(self.len + n)
    ffi.copy(self.data + self.len, s, n)
    self.len = self.len + n
end

function Buffer:write_u8(off, v)
    self.data[off] = v
end

function Buffer:write_u16(off, v)
    ffi.cast('uint16_t*', self.data + off)[0] = v
end

function Buffer:write_u32(off, v)
    ffi.cast('uint32_t*', self.data + off)[0] = v
end

function Buffer:write_i32(off, v)
    ffi.cast('int32_t*', self.data + off)[0] = v
end

function Buffer:write_u64(off, v)
    ffi.cast('uint64_t*', self.data + off)[0] = v
end

function Buffer:write_str(off, s)
    ffi.copy(self.data + off, s, #s)
end

function Buffer:to_string()
    return ffi.string(self.data, self.len)
end

-- ----------------------------------------------------------------
-- FILETIME helpers — uint64_t arithmetic, since unix*1e7 overshoots
-- IEEE-754 double precision (year 2026 → ~1.7e16 > 2^53).
-- ----------------------------------------------------------------

local FILETIME_EPOCH = 116444736000000000ULL  -- 1970-01-01 in 100-ns ticks
local FILETIME_PER_S = 10000000ULL

local function unix_to_filetime(unix_secs)
    return ffi.cast('uint64_t', unix_secs) * FILETIME_PER_S + FILETIME_EPOCH
end

-- ----------------------------------------------------------------
-- ASCII -> UTF-16-LE encoding.  Hive name (regf header) and the few
-- string-shaped places we care about are all ASCII; we don't try to
-- handle anything broader.
-- ----------------------------------------------------------------

local function utf16le(s)
    local out = {}
    for i = 1, #s do
        out[#out + 1] = s:sub(i, i)
        out[#out + 1] = "\0"
    end
    return table.concat(out)
end

-- ----------------------------------------------------------------
-- Key — declarative subkey tree, built up before serialization.
-- ----------------------------------------------------------------

local Key = {}
Key.__index = Key

local function newkey()
    return setmetatable({
        subkeys = {},   -- name -> Key
        values  = {},   -- array of {name, type, data}
        flags   = 0,
    }, Key)
end

-- Get or create a subkey by '\'-separated path.  Empty components and
-- redundant separators are tolerated (matches Python's split behaviour).
function Key:key(path)
    local k = self
    for part in path:gmatch("[^\\]+") do
        local sub = k.subkeys[part]
        if not sub then
            sub = newkey()
            k.subkeys[part] = sub
        end
        k = sub
    end
    return k
end

function Key:set_dword(name, value)
    -- Pack as little-endian 4 bytes.  bit.tobit normalises to int32; the
    -- u32 cast in the cell builder takes care of the unsigned shape.
    local b0 = bit.band(value,                   0xff)
    local b1 = bit.band(bit.rshift(value,  8),   0xff)
    local b2 = bit.band(bit.rshift(value, 16),   0xff)
    local b3 = bit.band(bit.rshift(value, 24),   0xff)
    self.values[#self.values + 1] = {
        name, M.REG_DWORD, string.char(b0, b1, b2, b3),
    }
    return self
end

function Key:set_sz(name, value)
    self.values[#self.values + 1] = {
        name, M.REG_SZ, utf16le(value) .. "\0\0",
    }
    return self
end

function Key:set_expand_sz(name, value)
    self.values[#self.values + 1] = {
        name, M.REG_EXPAND_SZ, utf16le(value) .. "\0\0",
    }
    return self
end

function Key:set_multi_sz(name, strings)
    local parts = {}
    for _, s in ipairs(strings) do
        parts[#parts + 1] = utf16le(s) .. "\0\0"
    end
    parts[#parts + 1] = "\0\0"
    self.values[#self.values + 1] = {
        name, M.REG_MULTI_SZ, table.concat(parts),
    }
    return self
end

function Key:set_binary(name, data)
    self.values[#self.values + 1] = { name, M.REG_BINARY, data }
    return self
end

function Key:set_value(name, vtype, data)
    self.values[#self.values + 1] = { name, vtype, data }
    return self
end

-- ----------------------------------------------------------------
-- Hive — root key + cell-level binary serializer.
--
-- The serialization model mirrors the Python:
--   - One growing _bin buffer.
--   - _alloc(payload_size) reserves a cell with negative-size prefix
--     plus zero-filled payload, returns the cell's hive index (offset
--     from hbin start = HBIN_HDR_SIZE + position in _bin).
--   - _patch(cell, offset, ...) writes inside the cell payload.
--   - The cell builders (_nk / _vk / _value_list / _index_leaf / _ks)
--     produce one cell each and return its index.
-- ----------------------------------------------------------------

local Hive = {}
Hive.__index = Hive

function M.new(name)
    local h = setmetatable({
        name = name or "SYSTEM",
        root = newkey(),
        _bin = nil,           -- Buffer, allocated in build()
    }, Hive)
    h.root.flags = KEY_HIVE_ENTRY
    return h
end

-- Path access: hive:key("ControlSet001\\Services\\foo") -> Key.
function Hive:key(path)
    return self.root:key(path)
end

-- Low-level cell allocator.  Returns the cell index (HBIN_HDR_SIZE +
-- position in _bin).  Callers patch fields via the helpers below.
function Hive:_alloc(payload_size)
    local total = align(4 + payload_size, CELL_ALIGN)
    local bin_offset = self._bin.len
    -- Negative size = allocated cell.
    self._bin:append_zeros(total)
    self._bin:write_i32(bin_offset, -total)
    return HBIN_HDR_SIZE + bin_offset
end

-- Patch helpers — cell-relative offsets into the payload (so 0 = first
-- byte after the 4-byte size prefix).
function Hive:_patch_str(cell, off, s)
    self._bin:write_str((cell - HBIN_HDR_SIZE) + 4 + off, s)
end

function Hive:_patch_u16(cell, off, v)
    self._bin:write_u16((cell - HBIN_HDR_SIZE) + 4 + off, v)
end

function Hive:_patch_u32(cell, off, v)
    self._bin:write_u32((cell - HBIN_HDR_SIZE) + 4 + off, v)
end

function Hive:_patch_u64(cell, off, v)
    self._bin:write_u64((cell - HBIN_HDR_SIZE) + 4 + off, v)
end

-- ---------------- Cell factories ----------------

-- CM_KEY_NODE ("nk") cell — the per-key record the kernel walks during
-- registry lookups.  76-byte fixed header + compressed-ASCII name.
-- MaxNameLen / MaxValueNameLen are reported by RegQueryInfoKey in
-- WCHARs (kernel returns the length AS IF the name were uncompressed),
-- MaxValueDataLen in bytes — see RegQueryInfoKey doco for the trap.
function Hive:_nk(name, parent, flags,
                  subkey_count, subkey_list,
                  value_count, value_list,
                  security, max_name_len, max_class_len,
                  max_value_name_len, max_value_data_len)
    security           = security           or 0xFFFFFFFF
    max_name_len       = max_name_len       or 0
    max_class_len      = max_class_len      or 0
    max_value_name_len = max_value_name_len or 0
    max_value_data_len = max_value_data_len or 0

    local name_b = name      -- ASCII; KEY_COMP_NAME means raw bytes
    local cell   = self:_alloc(76 + #name_b)

    -- +0   "nk" + flags
    self:_patch_str(cell,  0, "nk")
    self:_patch_u16(cell,  2, bit.bor(flags, KEY_COMP_NAME))

    -- +4   FILETIME LastWriteTime
    self:_patch_u64(cell,  4, unix_to_filetime(self._now))

    -- +12  Spare (already zero from alloc)
    -- +16  Parent
    self:_patch_u32(cell, 16, parent)

    -- +20  SubKeyCounts (Stable, Volatile)
    self:_patch_u32(cell, 20, subkey_count)
    -- +24  Volatile count = 0 (already zero)

    -- +28  SubKeyLists (Stable, Volatile)
    self:_patch_u32(cell, 28, subkey_list)
    self:_patch_u32(cell, 32, 0xFFFFFFFF)   -- Volatile sentinel

    -- +36  ValueList (count, list)
    self:_patch_u32(cell, 36, value_count)
    self:_patch_u32(cell, 40, value_list)

    -- +44  Security, Class
    self:_patch_u32(cell, 44, security)
    self:_patch_u32(cell, 48, 0xFFFFFFFF)   -- no class

    -- +52  MaxNameLen, MaxClassLen, MaxValueNameLen, MaxValueDataLen
    self:_patch_u32(cell, 52, max_name_len)
    self:_patch_u32(cell, 56, max_class_len)
    self:_patch_u32(cell, 60, max_value_name_len)
    self:_patch_u32(cell, 64, max_value_data_len)

    -- +68  WorkVar (already zero)
    -- +72  NameLength, ClassLength
    self:_patch_u16(cell, 72, #name_b)
    -- +74  ClassLength = 0 (already zero)

    -- +76  Name
    self:_patch_str(cell, 76, name_b)

    return cell
end

-- CM_KEY_VALUE ("vk") cell.  Small values (<= 4 bytes) are stored
-- inline in the DataOffset field with the high bit of DataLength set.
-- Larger values get their own cell whose index is stored in DataOffset.
function Hive:_vk(name, vtype, data)
    local name_b = name
    local cell   = self:_alloc(20 + #name_b)

    local data_off, data_len
    if #data <= 4 then
        local padded = data .. "\0\0\0\0"
        local p = ffi.cast('const uint8_t*', padded)
        data_off = bit.bor(p[0],
                           bit.lshift(p[1], 8),
                           bit.lshift(p[2], 16),
                           bit.lshift(p[3], 24))
        -- Make unsigned 32-bit for the u32 write below.
        if data_off < 0 then data_off = data_off + 0x100000000 end
        data_len = bit.bor(#data, 0x80000000)
    else
        local data_cell = self:_alloc(#data)
        self:_patch_str(data_cell, 0, data)
        data_off = data_cell
        data_len = #data
    end

    -- +0   "vk"
    self:_patch_str(cell,  0, "vk")
    -- +2   NameLength
    self:_patch_u16(cell,  2, #name_b)
    -- +4   DataLength (high bit = inline)
    self:_patch_u32(cell,  4, data_len)
    -- +8   DataOffset
    self:_patch_u32(cell,  8, data_off)
    -- +12  Type
    self:_patch_u32(cell, 12, vtype)
    -- +16  Flags (VALUE_COMP_NAME)
    self:_patch_u16(cell, 16, VALUE_COMP_NAME)
    -- +18  Spare (already zero)
    -- +20  Name
    self:_patch_str(cell, 20, name_b)

    return cell
end

-- Array of HCELL_INDEX referenced by an nk's ValueList field.
function Hive:_value_list(cells)
    local n = #cells
    local cell = self:_alloc(4 * n)
    for i, c in ipairs(cells) do
        self:_patch_u32(cell, (i - 1) * 4, c)
    end
    return cell
end

-- CM_KEY_INDEX_LEAF ("li") — flat list of HCELL_INDEX, no hashes.
-- NT 3.5 doesn't support "lf"/"lh" (those are XP+).  Caller passes
-- entries already in name-sorted order.
function Hive:_index_leaf(cells)
    local n = #cells
    local cell = self:_alloc(4 + 4 * n)
    self:_patch_str(cell, 0, "li")
    self:_patch_u16(cell, 2, n)
    for i, c in ipairs(cells) do
        self:_patch_u32(cell, 4 + (i - 1) * 4, c)
    end
    return cell
end

-- CM_KEY_SECURITY ("ks") cell — wraps a self-relative SECURITY_DESCRIPTOR.
-- Flink/Blink form a circular list per hive; for one shared SD they
-- both point at the cell itself.
function Hive:_ks(descriptor, ref_count)
    local cell = self:_alloc(20 + #descriptor)

    self:_patch_str(cell,  0, "ks")
    -- +2   Reserved (zero)
    self:_patch_u32(cell,  4, cell)         -- Flink = self
    self:_patch_u32(cell,  8, cell)         -- Blink = self
    self:_patch_u32(cell, 12, ref_count)
    self:_patch_u32(cell, 16, #descriptor)
    self:_patch_str(cell, 20, descriptor)

    return cell
end

-- Minimal self-relative SECURITY_DESCRIPTOR.  SeValidSecurityDescriptor
-- (SE/CAPTURE.C:1979) requires:
--   - Revision == 1
--   - Control & SE_SELF_RELATIVE (0x8000)
--   - Owner SID present + valid (mandatory)
--   - Group / DACL optional; NULL DACL with SE_DACL_PRESENT means "allow all"
-- Layout (32 bytes):
--   +0   SECURITY_DESCRIPTOR_RELATIVE (20 bytes)
--   +20  Owner SID: S-1-5-18 (Local System, 12 bytes)
local function null_dacl_descriptor()
    -- Owner SID: S-1-5-18
    -- Rev=1, SubAuthCount=1, IdentifierAuthority={0,0,0,0,0,5}, SubAuth[0]=18
    local sid = string.char(
        1,                       -- Revision
        1,                       -- SubAuthorityCount
        0, 0, 0, 0, 0, 5         -- IdentifierAuthority
    ) .. string.char(
        18, 0, 0, 0              -- SubAuthority[0] = 18, LE u32
    )

    -- SD header: Rev=1, Sbz1=0, Control=0x8004 (SE_SELF_RELATIVE | SE_DACL_PRESENT)
    -- OwnerOff=20, GroupOff=0, SaclOff=0, DaclOff=0 (NULL DACL = allow all)
    local hdr = string.char(
        1, 0,                    -- Rev, Sbz1
        0x04, 0x80               -- Control LE u16: 0x8004
    ) .. string.char(
        20, 0, 0, 0,             -- OwnerOff = 20
        0,  0, 0, 0,             -- GroupOff = 0
        0,  0, 0, 0,             -- SaclOff  = 0
        0,  0, 0, 0              -- DaclOff  = 0 (= NULL DACL)
    )
    return hdr .. sid
end

-- Recursive tree walker.  Emits values, then recurses to children, then
-- emits the nk and the subkey index pointing at the children's cells.
-- Returns the cell index of the emitted nk.
function Hive:_emit_key(name, key, parent_cell, security_cell, is_root)
    -- Values
    local value_cells = {}
    for i, v in ipairs(key.values) do
        value_cells[i] = self:_vk(v[1], v[2], v[3])
    end
    local value_list = (#value_cells > 0) and self:_value_list(value_cells)
                                          or  0xFFFFFFFF

    -- Children sorted by uppercase-name (case-insensitive, matches the
    -- kernel's case-folded binary search).
    local children = {}
    for n, k in pairs(key.subkeys) do
        children[#children + 1] = { name = n, key = k }
    end
    table.sort(children, function(a, b)
        return a.name:upper() < b.name:upper()
    end)

    -- RegQueryInfoKey returns these straight from the nk header.  Name
    -- and value-name lengths in WCHARs (= byte length × 2 for ASCII);
    -- value-data length in bytes.
    local max_name_len = 0
    for _, c in ipairs(children) do
        local L = #c.name * 2
        if L > max_name_len then max_name_len = L end
    end
    local max_value_name_len = 0
    local max_value_data_len = 0
    for _, v in ipairs(key.values) do
        local nL = #v[1] * 2
        local dL = #v[3]
        if nL > max_value_name_len then max_value_name_len = nL end
        if dL > max_value_data_len then max_value_data_len = dL end
    end

    local nk_cell = self:_nk(
        name, parent_cell, key.flags,
        #children, 0xFFFFFFFF,    -- subkey list patched below
        #value_cells, value_list,
        security_cell,
        max_name_len, 0,
        max_value_name_len, max_value_data_len
    )

    -- Root's parent points to itself.
    if is_root then
        self:_patch_u32(nk_cell, 16, nk_cell)
    end

    -- Emit children with us as parent, then point our subkey list at
    -- the freshly-built index leaf.
    if #children > 0 then
        local child_cells = {}
        for i, c in ipairs(children) do
            child_cells[i] = self:_emit_key(c.name, c.key, nk_cell, security_cell)
        end
        local sl_cell = self:_index_leaf(child_cells)
        self:_patch_u32(nk_cell, 28, sl_cell)
    end

    return nk_cell
end

-- ---------------- Final assembly ----------------

-- Build the binary hive.  `now` (optional) is the unix timestamp the
-- regf header + every nk's LastWriteTime are stamped with — defaults
-- to host wall-clock; tests can pass a fixed value for reproducible
-- byte-for-byte output.
function Hive:build(now)
    self._now = now or os.time()
    self._bin = newbuf()

    -- One shared SD cell referenced by every key.  CmCheckRegistry walks
    -- the circular list (root key's Security pointer) and runs
    -- SeValidSecurityDescriptor on every entry — so we need at least
    -- one valid SD.  Ref count is set to a large constant so
    -- decrement-on-delete never underflows; the kernel doesn't actually
    -- enforce it equals the live key count.
    local sd = null_dacl_descriptor()
    local security_cell = self:_ks(sd, 0x10000)

    local root_cell = self:_emit_key(self.name, self.root, 0,
                                     security_cell, true)

    -- Pad bin area to PAGE alignment.  Trailing slack >= 8 bytes becomes
    -- a single "free cell" (positive size); < 8 bytes is just zero pad.
    local total = align(HBIN_HDR_SIZE + self._bin.len, PAGE)
    local pad   = total - HBIN_HDR_SIZE - self._bin.len
    if pad >= 8 then
        local free_off = self._bin.len
        self._bin:append_zeros(pad)
        self._bin:write_i32(free_off, pad)
    elseif pad > 0 then
        self._bin:append_zeros(pad)
    end

    -- hbin header.
    local hbin_size = HBIN_HDR_SIZE + self._bin.len
    local hbin_hdr  = newbuf(HBIN_HDR_SIZE)
    hbin_hdr:append_zeros(HBIN_HDR_SIZE)
    hbin_hdr:write_str(0,  "hbin")
    hbin_hdr:write_u32(4,  0)              -- offset of this bin
    hbin_hdr:write_u32(8,  hbin_size)
    -- +12, +16: zero (already)

    -- regf base block.
    local base = newbuf(PAGE)
    base:append_zeros(PAGE)
    base:write_str(0,  "regf")
    base:write_u32(4,  1)                  -- seq1
    base:write_u32(8,  1)                  -- seq2
    base:write_u64(12, unix_to_filetime(self._now))
    base:write_u32(20, 1)                  -- major version = 1
    base:write_u32(24, 2)                  -- minor version = 2
    base:write_u32(28, 0)                  -- type = primary
    base:write_u32(32, 1)                  -- format = direct memory load
    base:write_u32(36, root_cell)          -- root cell
    base:write_u32(40, hbin_size)          -- length of hive bins
    base:write_u32(44, 1)                  -- cluster
    -- Hive name UTF-16-LE at +48 (≤ 64 bytes).
    local fname = utf16le(self.name) .. "\0\0"
    base:write_str(48, fname)

    -- Checksum at +508: XOR of DWORDs 0..507.
    local cksum = 0
    for i = 0, 504, 4 do
        local v = ffi.cast('uint32_t*', base.data + i)[0]
        cksum = bit.bxor(cksum, tonumber(v))
    end
    base:write_u32(508, cksum)

    return base:to_string() .. hbin_hdr:to_string() .. self._bin:to_string()
end

-- Convenience: build + write via the platform's write_file.
function Hive:write(write_file_fn, path, now)
    local bytes = self:build(now)
    write_file_fn(path, bytes)
    return #bytes
end

return M
