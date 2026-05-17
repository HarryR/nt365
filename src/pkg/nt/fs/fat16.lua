-- nt.fs.fat16 — FAT16 volume builder.
--
-- Pure-Lua FAT16 volume image generator.  Produces partition
-- payload bytes only (no MBR, no disk-image stitching) — the
-- nt.fs.drive composer assembles the disk image around it.
--
-- Same on-disk format as the previous ntosbe.disk implementation
-- (FAT16 type 0x06, NT 3.5 atdisk + fastfat compatible).  Profile
-- knowledge — *which* files go on the disk — lives in
-- pkg/ntosbe/layers/ (composed per profile by ntosbe/compose.lua).
--
-- Usage:
--     local fs = require('nt.fs')
--     local vol = fs.fat16.new{ size_mb = 511, volume_label = "NT" }
--     vol:add_file("System32/ntoskrnl.exe", "/abs/path", platform)
--     vol:add_bytes("System32/config/SYSTEM", hive_bytes)
--     vol:mkdir("tmp")
--     local d = fs.drive.new{ table='mbr', signature=0x4E544653 }
--     d:add(vol, { active=true, gap_before_lba=2048 })
--     d:build(platform, "/path/to/esp.img")

local ffi = require('ffi')
local bit = require('bit')

local M = {}

-- ----------------------------------------------------------------
-- FAT16 constants.
-- ----------------------------------------------------------------

local SECTOR_SIZE      = 512
local RESERVED_SECTORS = 1
local NUM_FATS         = 2
local ROOT_DIR_ENTRIES = 512       -- FAT16 convention — fixed root
local ROOT_DIR_SECTORS = (ROOT_DIR_ENTRIES * 32) / SECTOR_SIZE   -- 32

local PARTITION_TYPE_FAT16 = 0x06     -- FAT16 >= 32 MB

-- FAT16 cluster size scaling.  FAT16 caps at ~65524 clusters; with
-- 2 KB clusters that's only ~128 MB.  Standard Microsoft FAT16
-- layout bumps cluster size with volume so 2 GB stays addressable.
-- Numbers here are sectors-per-cluster (each sector = 512 bytes).
local function default_spc(size_mb)
    if     size_mb <=   128 then return  4   -- 2 KB clusters
    elseif size_mb <=   256 then return  8   -- 4 KB
    elseif size_mb <=   512 then return 16   -- 8 KB
    elseif size_mb <=  1024 then return 32   -- 16 KB
    else                        return 64    -- 32 KB (max FAT16: 2 GB)
    end
end

local CLUSTER_EOC = 0xFFFF

local ATTR_VOLUME_ID = 0x08
local ATTR_DIRECTORY = 0x10
local ATTR_ARCHIVE   = 0x20

-- ----------------------------------------------------------------
-- 8.3 name encoding.  case-folds to uppercase, space-pads to 11.
-- ----------------------------------------------------------------

local function encode_83(name)
    name = name:upper()
    local stem, ext
    local dot = name:match("()%.[^.]*$")
    if dot then
        stem = name:sub(1, dot - 1)
        ext  = name:sub(dot + 1)
    else
        stem = name
        ext  = ""
    end
    if #stem > 8 or #ext > 3 then
        error("FAT16: name '" .. name .. "' exceeds the 8.3 limit "
              .. "(max 8-char stem + 3-char ext) — rename it, or build "
              .. "the disk with an NTFS layout (--layout=split-ntfs)", 2)
    end
    local function pad(s, n) return s .. string.rep(" ", n - #s) end
    return pad(stem, 8) .. pad(ext, 3)
end

-- FAT date / time encoding from a calendar table {year, month, day,
-- hour, min, sec}.  Returned values are 16-bit integers.
local function fat_time(cal)
    local fat_t = bit.bor(bit.lshift(cal.hour, 11),
                          bit.lshift(cal.min,   5),
                          math.floor(cal.sec / 2))
    local fat_d = bit.bor(bit.lshift(cal.year - 1980, 9),
                          bit.lshift(cal.month, 5),
                          cal.day)
    return fat_t, fat_d
end

-- ----------------------------------------------------------------
-- Tree of entries built up before layout.  Cluster numbers are
-- filled in during build().
-- ----------------------------------------------------------------

local function newentry(name, is_dir, opts)
    opts = opts or {}
    -- Validate up-front so errors surface at add-time, not build-time.
    encode_83(name)
    return {
        name          = name:upper(),
        is_dir        = is_dir,
        data          = opts.data or "",
        children      = {},
        attr          = opts.attr or 0,
        first_cluster = 0,
        mtime         = opts.mtime,    -- nil = use the volume's `now`
    }
end

local function find_child(dir, name_upper)
    for _, c in ipairs(dir.children) do
        if c.name == name_upper then return c end
    end
    return nil
end

-- ----------------------------------------------------------------
-- FatVolume — public API.
-- ----------------------------------------------------------------

local FatVolume = {}
FatVolume.__index = FatVolume

-- opts = { size_mb, volume_label, volume_serial, sectors_per_cluster, now }
--
-- size_mb is the *partition* size (volume payload).  The composer
-- adds any pre-partition gap on top of that to compute total disk
-- size.  `now` is a unix timestamp used for any directory entry
-- whose own mtime isn't supplied (root, mkdir'd intermediates,
-- in-memory blobs).
function M.new(opts)
    opts = opts or {}
    local size_mb = opts.size_mb or 16
    if size_mb < 4 then
        error("FAT16 volume must be at least 4 MB", 2)
    end
    local label = (opts.volume_label or "NT"):upper():sub(1, 11)
    local now   = opts.now or 0
    local spc   = opts.sectors_per_cluster or default_spc(size_mb)
    return setmetatable({
        _size_bytes         = size_mb * 1024 * 1024,
        sectors_per_cluster = spc,
        volume_label        = label,
        volume_serial       = bit.band(opts.volume_serial or now, 0xFFFFFFFF),
        now                 = now,
        root                = newentry("ROOT", true, { attr = ATTR_DIRECTORY,
                                                       mtime = now }),
    }, FatVolume)
end

-- size_bytes — committed at construction; the composer reads this
-- before calling :build() to lay out partition LBAs.
function FatVolume:size_bytes()
    return self._size_bytes
end

-- mkdir -p style: walks `path` (forward-slash separated; backslashes
-- accepted too), creating directories along the way.  Returns the
-- terminal directory entry.
function FatVolume:mkdir(path)
    local d = self.root
    for part in path:gsub("\\", "/"):gmatch("[^/]+") do
        local existing = find_child(d, part:upper())
        if existing == nil then
            local new = newentry(part, true,
                                 { attr = ATTR_DIRECTORY, mtime = self.now })
            d.children[#d.children + 1] = new
            d = new
        else
            if not existing.is_dir then
                error(path .. ": " .. part .. " is a file, not a dir", 2)
            end
            d = existing
        end
    end
    return d
end

-- Internal: split `dest` into (parent dir entry, leaf name).
local function split_dest(self, dest)
    local parts = {}
    for part in dest:gsub("\\", "/"):gmatch("[^/]+") do
        parts[#parts + 1] = part
    end
    if #parts == 0 then
        error("empty dest path", 3)
    end
    local leaf = parts[#parts]
    local parent
    if #parts == 1 then
        parent = self.root
    else
        local sub = table.concat(parts, "/", 1, #parts - 1)
        parent = self:mkdir(sub)
    end
    return parent, leaf
end

-- Add a file from disk.  `data` and `mtime` are read via the
-- supplied platform module so this code stays portable; we don't
-- assume io.open.
function FatVolume:add_file(dest, src_path, platform)
    local parent, leaf = split_dest(self, dest)
    if find_child(parent, leaf:upper()) then
        error(dest .. ": already exists", 2)
    end
    local data = platform.read_file(src_path)
    if not data then
        error("add_file: cannot read " .. src_path, 2)
    end
    local entry = newentry(leaf, false, {
        data  = data,
        attr  = ATTR_ARCHIVE,
        mtime = platform.mtime(src_path) or self.now,
    })
    parent.children[#parent.children + 1] = entry
    return entry
end

-- Add an in-memory blob.
function FatVolume:add_bytes(dest, data)
    local parent, leaf = split_dest(self, dest)
    if find_child(parent, leaf:upper()) then
        error(dest .. ": already exists", 2)
    end
    local entry = newentry(leaf, false, {
        data  = data,
        attr  = ATTR_ARCHIVE,
        mtime = self.now,
    })
    parent.children[#parent.children + 1] = entry
    return entry
end

-- ----------------------------------------------------------------
-- Layout + emit.  All the FAT16 maths lives here.
-- ----------------------------------------------------------------

-- Build one 32-byte FAT directory entry.
local function dir_entry(name11, attr, first_cluster, size, mtime, platform)
    if #name11 ~= 11 then error("name11 must be 11 bytes", 2) end
    local cal = platform.localtime(mtime)
    -- Year before 1980 (FAT epoch) clamps to 1980-01-01 so encoding
    -- doesn't under/overflow.  Real timestamps are always far above
    -- this; only the {mtime = 0} caller (root sentinel) can hit it.
    if cal.year < 1980 then
        cal = { year = 1980, month = 1, day = 1, hour = 0, min = 0, sec = 0 }
    end
    local fat_t, fat_d = fat_time(cal)

    local buf = ffi.new('uint8_t[32]')
    ffi.copy(buf, name11, 11)
    buf[11] = attr
    -- buf[12..13]: reserved + creation time fine — leave zero
    ffi.cast('uint16_t*', buf + 14)[0] = fat_t   -- creation time
    ffi.cast('uint16_t*', buf + 16)[0] = fat_d   -- creation date
    ffi.cast('uint16_t*', buf + 18)[0] = fat_d   -- last access date
    -- buf[20..21]: high cluster (FAT32 — 0 here)
    ffi.cast('uint16_t*', buf + 22)[0] = fat_t   -- last modify time
    ffi.cast('uint16_t*', buf + 24)[0] = fat_d   -- last modify date
    ffi.cast('uint16_t*', buf + 26)[0] = bit.band(first_cluster, 0xFFFF)
    ffi.cast('uint32_t*', buf + 28)[0] = size
    return ffi.string(buf, 32)
end

-- ctx = { start_lba, sector_size }
--   start_lba is the partition's absolute LBA on the disk; goes into
--   the FAT16 BPB HiddenSectors field.  The composer passes this in.
function FatVolume:build(platform, ctx)
    if not platform then
        error("FatVolume:build: pass platform module as 1st arg", 2)
    end
    ctx = ctx or {}
    local start_lba = ctx.start_lba or 0

    local img = ffi.new('uint8_t[?]', self._size_bytes)

    -- ---- Partition geometry (LBAs are partition-relative now) ----
    local part_sectors = math.floor(self._size_bytes / SECTOR_SIZE)
    local spc          = self.sectors_per_cluster

    -- Solve for sectors_per_fat by iteration (FAT size depends on
    -- cluster count, which depends on data size, which depends on
    -- FAT size).
    local sectors_per_fat = 1
    local clusters
    while true do
        local data_sectors = part_sectors - RESERVED_SECTORS
                           - NUM_FATS * sectors_per_fat - ROOT_DIR_SECTORS
        clusters = math.floor(data_sectors / spc)
        local needed_fat_bytes   = (clusters + 2) * 2
        local needed_fat_sectors = math.floor(
            (needed_fat_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE)
        if needed_fat_sectors <= sectors_per_fat then break end
        sectors_per_fat = needed_fat_sectors
    end
    local total_clusters = clusters

    local fat1_lba = RESERVED_SECTORS               -- partition-relative
    local fat2_lba = fat1_lba + sectors_per_fat
    local root_lba = fat2_lba + sectors_per_fat
    local data_lba = root_lba + ROOT_DIR_SECTORS

    -- ---- Cluster assignment ----
    local fat_entries = { [0] = 0xFFF8, [1] = 0xFFFF }
    local next_cluster = 2

    local function alloc_clusters(n_bytes)
        if n_bytes == 0 then return 0, 0 end
        local cluster_bytes = spc * SECTOR_SIZE
        local n = math.floor((n_bytes + cluster_bytes - 1) / cluster_bytes)
        local first = next_cluster
        for i = 0, n - 1 do
            local cl = next_cluster + i
            if cl + 1 >= total_clusters + 2 then
                error("FAT16 volume full during layout", 2)
            end
            fat_entries[cl] = (i + 1 < n) and (cl + 1) or CLUSTER_EOC
        end
        next_cluster = next_cluster + n
        return first, n
    end

    local function assign(entry, parent_is_root)
        if entry.is_dir then
            if entry ~= self.root then
                local n_entries = 2 + #entry.children
                local est_bytes = n_entries * 32
                if est_bytes < 32 then est_bytes = 32 end
                local first = alloc_clusters(est_bytes)
                entry.first_cluster = first
            end
            for _, child in ipairs(entry.children) do
                assign(child, entry == self.root)
            end
        else
            local first = alloc_clusters(#entry.data)
            entry.first_cluster = first
        end
    end
    assign(self.root, true)

    local function copy_to_img(byte_offset, src, n)
        ffi.copy(img + byte_offset, src, n)
    end

    -- ---- FAT16 boot sector (partition LBA 0) ----
    local bs = ffi.new('uint8_t[?]', SECTOR_SIZE)
    -- jmp short + nop so BPB doesn't get executed.
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90
    ffi.copy(bs + 3, "MSDOS5.0", 8)                -- OEM name
    ffi.cast('uint16_t*', bs + 0x0B)[0] = SECTOR_SIZE
    bs[0x0D] = spc
    ffi.cast('uint16_t*', bs + 0x0E)[0] = RESERVED_SECTORS
    bs[0x10] = NUM_FATS
    ffi.cast('uint16_t*', bs + 0x11)[0] = ROOT_DIR_ENTRIES

    local big_total = 0
    if part_sectors <= 0xFFFF then
        ffi.cast('uint16_t*', bs + 0x13)[0] = part_sectors
    else
        big_total = part_sectors
        ffi.cast('uint16_t*', bs + 0x13)[0] = 0
    end
    bs[0x15] = 0xF8                                -- media descriptor
    ffi.cast('uint16_t*', bs + 0x16)[0] = sectors_per_fat
    ffi.cast('uint16_t*', bs + 0x18)[0] = 63       -- sectors per track
    ffi.cast('uint16_t*', bs + 0x1A)[0] = 255      -- heads
    ffi.cast('uint32_t*', bs + 0x1C)[0] = start_lba   -- HiddenSectors
    ffi.cast('uint32_t*', bs + 0x20)[0] = big_total
    bs[0x24] = 0x80                                -- drive number
    bs[0x26] = 0x29                                -- ext boot sig
    ffi.cast('uint32_t*', bs + 0x27)[0] = self.volume_serial
    -- Volume label (11 bytes, space-padded).
    local label_padded = self.volume_label
                       .. string.rep(" ", 11 - #self.volume_label)
    ffi.copy(bs + 0x2B, label_padded, 11)
    ffi.copy(bs + 0x36, "FAT16   ", 8)
    bs[0x1FE] = 0x55
    bs[0x1FF] = 0xAA
    copy_to_img(0, bs, SECTOR_SIZE)

    -- ---- FAT tables ----
    local fat_bytes = sectors_per_fat * SECTOR_SIZE
    local fat = ffi.new('uint8_t[?]', fat_bytes)
    for cl, val in pairs(fat_entries) do
        ffi.cast('uint16_t*', fat + cl * 2)[0] = val
    end
    copy_to_img(fat1_lba * SECTOR_SIZE, fat, fat_bytes)
    copy_to_img(fat2_lba * SECTOR_SIZE, fat, fat_bytes)

    -- ---- Root directory ----
    local root_bytes_buf = {}
    if #self.volume_label > 0 then
        local lbl = self.volume_label
                  .. string.rep(" ", 11 - #self.volume_label)
        root_bytes_buf[#root_bytes_buf + 1] = dir_entry(
            lbl, ATTR_VOLUME_ID, 0, 0, self.now, platform)
    end
    for _, child in ipairs(self.root.children) do
        local size = (child.is_dir) and 0 or #child.data
        root_bytes_buf[#root_bytes_buf + 1] = dir_entry(
            encode_83(child.name), child.attr,
            child.first_cluster, size,
            child.mtime or self.now, platform)
    end
    local root_str = table.concat(root_bytes_buf)
    copy_to_img(root_lba * SECTOR_SIZE, root_str, #root_str)

    -- ---- File data + subdirectory clusters ----
    local function write_entry(entry, parent_cluster)
        if entry.is_dir then
            local parts = {}
            -- Non-root dirs prepend "." + ".." entries.
            parts[1] = dir_entry(
                ".          ", ATTR_DIRECTORY,
                entry.first_cluster, 0, entry.mtime or self.now, platform)
            parts[2] = dir_entry(
                "..         ", ATTR_DIRECTORY,
                parent_cluster, 0, entry.mtime or self.now, platform)
            for _, child in ipairs(entry.children) do
                local size = (child.is_dir) and 0 or #child.data
                parts[#parts + 1] = dir_entry(
                    encode_83(child.name), child.attr,
                    child.first_cluster, size,
                    child.mtime or self.now, platform)
            end
            local s = table.concat(parts)
            local lba = data_lba + (entry.first_cluster - 2) * spc
            copy_to_img(lba * SECTOR_SIZE, s, #s)
            for _, child in ipairs(entry.children) do
                write_entry(child, entry.first_cluster)
            end
        else
            if entry.first_cluster == 0 then return end   -- empty file
            local lba = data_lba + (entry.first_cluster - 2) * spc
            copy_to_img(lba * SECTOR_SIZE, entry.data, #entry.data)
        end
    end
    for _, child in ipairs(self.root.children) do
        write_entry(child, 0)        -- root: parent cluster = 0
    end

    local used_clusters = next_cluster - 2
    local free_clusters = total_clusters - used_clusters
    return {
        bytes       = ffi.string(img, self._size_bytes),
        type_code   = PARTITION_TYPE_FAT16,
        label       = self.volume_label,
        sector_size = SECTOR_SIZE,
        stats = {
            used_clusters       = used_clusters,
            total_clusters      = total_clusters,
            free_kb             = math.floor(
                free_clusters * spc * SECTOR_SIZE / 1024),
            sectors_per_cluster = spc,
            sectors_per_fat     = sectors_per_fat,
        },
    }
end

return M
