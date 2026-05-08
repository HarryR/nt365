-- nt.fs.ntfs -- NTFS 1.1 volume builder.
--
-- Pure-Lua, generates a bootable empty NTFS volume that NT 3.5's
-- ntfs.sys mounts.  Same shape as nt.fs.fat16 -- new, mkdir,
-- add_file, add_bytes, build -- so the nt.fs.drive composer can
-- swap one for the other based on partition slot.
--
-- Produces partition payload bytes only; the composer wraps it in
-- an MBR (or later GPT) at the disk-image level.
--
-- Reference implementation: NT 3.5 UNTFS/SRC/FORMAT.CXX, the
-- userland tool that backs FORMAT.EXE for NTFS volumes.  See
-- sub-modules (boot.lua, mft.lua, attrs.lua) for byte-level
-- encoders.
--
-- Phase 1 scope (this file): empty mountable volume with the 10
-- system files and no user data.
--
-- Within the partition (LCN 0 = sector ctx.start_lba of disk), for
-- a 64 MB volume / 4 KB cluster:
--   LCN  0..1      $Boot     (8 KB boot area)
--   LCN  2..5      $MFT      (16 records × 1 KB = 16 KB)
--   LCN  6         $MFTMirr  (4 records = 4 KB)
--   LCN  7..22     $LogFile  (16 clusters = 64 KB; zero-filled MVP)
--   LCN  23        $AttrDef  (2240 B fits in 1 cluster)
--   LCN  24..55    $UpCase   (128 KB = 32 clusters)
--   LCN  56        $Bitmap   (cluster allocation bitmap)
--   LCN  57+       free space
--
-- Backup boot sector at NumberSectors/2 deferred (mounting works
-- with primary alone; ntfs.sys only falls back to backup on primary
-- read failure).

local ffi = require('ffi')
local bit = require('bit')

local M = {}

M.boot  = require('nt.fs.ntfs.boot')
M.mft   = require('nt.fs.ntfs.mft')
M.attrs = require('nt.fs.ntfs.attrs')

-- Constants from UNTFS.HXX + NTFS.H.
M.SECTOR_SIZE              = 512
M.MFT_RECORD_SIZE          = 1024
M.INDEX_BUFFER_SIZE        = 4096
M.BYTES_IN_BOOT_AREA       = 0x2000   -- 8 KB
M.REFLECTED_MFT_SEGMENTS   = 4        -- $MFTMirr holds first 4 MFT records
M.FIRST_USER_FILE_NUMBER   = 16
M.NTFS_MAJOR_VERSION       = 1
M.NTFS_MINOR_VERSION       = 1        -- NT 3.5 wrote 1.1

-- System file numbers (NTFS.H:353-373).
M.MASTER_FILE_TABLE_NUMBER   = 0
M.MASTER_FILE_TABLE2_NUMBER  = 1
M.LOG_FILE_NUMBER            = 2
M.VOLUME_DASD_NUMBER         = 3
M.ATTRIBUTE_DEF_TABLE_NUMBER = 4
M.ROOT_FILE_NAME_INDEX_NUMBER= 5
M.BIT_MAP_FILE_NUMBER        = 6
M.BOOT_FILE_NUMBER           = 7
M.BAD_CLUSTER_FILE_NUMBER    = 8
M.UPCASE_TABLE_NUMBER        = 10

-- Default $LogFile size (clusters).  LFS internally requires at least
-- MINIMUM_LFS_PAGES (0x30 = 48) usable log pages plus 2 restart pages,
-- i.e. 50 × PAGE_SIZE = 200 KB on a 4 KB-page system.  Anything smaller
-- causes LfsNormalizeBasicLogFile to raise STATUS_INSUFFICIENT_RESOURCES
-- at mount time.  We allocate 1024 clusters = 4 MB to match NT 3.5/4.0's
-- default sizing for small system volumes — comfortably above the minimum
-- and identical to what FORMAT.CXX produces for ~500 MB volumes.
M.LOG_FILE_CLUSTERS          = 1024

-- Partition type code returned to the disk composer.
M.PARTITION_TYPE_NTFS        = 0x07   -- NTFS / HPFS

-- INDX leaf-buffer layout (used by compute_layout's sizing decision
-- AND by build_indexed_dir_attrs' actual write).  These MUST agree:
-- if the sizing thinks more entries fit than the writer can place,
-- ffi.copy walks past the 4 KB buffer and corrupts the LuaJIT heap.
--
--   0x00..0x07  MULTI_SECTOR_HEADER ("INDX" + USA off + USA size)
--   0x08..0x0F  Lsn (zero)
--   0x10..0x17  Vcn
--   0x18..0x27  INDEX_HEADER (16 B)
--   0x28..      USA — (sectors_per_buf + 1) × 2 bytes
--   <align 8>   FIRST_ENTRY_OFFSET — entries land here
--   ...         entries
--   <- end>     END terminator (16 B)
local function quad_align_const(n) return bit.band(n + 7, bit.bnot(7)) end
M.INDX_USA_OFFSET   = 0x28
M.INDX_USA_BYTES    = ((M.INDEX_BUFFER_SIZE / M.SECTOR_SIZE) + 1) * 2
M.INDX_FIRST_ENTRY_OFFSET = quad_align_const(M.INDX_USA_OFFSET
                                              + M.INDX_USA_BYTES)
M.INDX_END_ENTRY_BYTES = 16
M.INDX_LEAF_CAPACITY   = M.INDEX_BUFFER_SIZE
                          - M.INDX_FIRST_ENTRY_OFFSET
                          - M.INDX_END_ENTRY_BYTES

-- Unix seconds (UTC) → FILETIME (100-ns ticks since 1601-01-01 UTC).
-- Used at the composer boundary so every `time_filetime` field stored
-- on an NtfsImage node is a real FILETIME, not Unix seconds — readers
-- of $STANDARD_INFORMATION / $FILE_NAME interpret these as 100-ns
-- ticks, and stuffing Unix seconds in there reads back as "year 1601 +
-- a couple of minutes", which makes NMAKE's _stat-based mtime
-- comparison treat every image-baked file as far older than any
-- guest-written file.
--
-- Constant matches rtl.lua's EPOCH_BIAS_SEC (Unix epoch is
-- 11_644_473_600 s after 1601-01-01).  Kept inline rather than via
-- require'd to keep this composer pure-Lua arithmetic, since the
-- composer runs on host without nt.dll.rtl loaded.
local function unix_to_filetime(secs)
    if not secs or secs == 0 then return 0 end
    return (secs + 11644473600) * 10000000
end

-- ----------------------------------------------------------------
-- Build a FILE_REFERENCE (8 bytes): low 6 bytes = file number,
-- high 2 bytes = sequence number.  Returned as a Lua-number that
-- fits in a uint64 cast.  For system files we use seq = file_number
-- (FORMAT.CXX convention; see RootFileIndexSegment setup at line 454).
-- ----------------------------------------------------------------
local function file_ref(file_number, seq_number)
    return ffi.cast('uint64_t', file_number) +
           ffi.cast('uint64_t', seq_number) * 2 ^ 48
end

-- For system files, sequence number = file number (FORMAT.CXX
-- convention).  Special: $MFT (file 0) gets seq 1 (since 0 is invalid).
local function sys_file_ref(file_number)
    local seq = (file_number == 0) and 1 or file_number
    return file_ref(file_number, seq)
end

-- ----------------------------------------------------------------
-- Build one system-file MFT record.  Common attributes: $STANDARD_INFO
-- + $FILE_NAME + the per-file specific attributes.
--
-- spec = {
--   file_number     = N,
--   name            = "$MFT" or similar,
--   is_directory    = false,
--   file_attributes = HIDDEN | SYSTEM,
--   parent_ref      = file_ref(root file 5, seq 5),
--   instance_start  = 3,                    (next attribute instance after 2)
--   extra_attrs     = { encoded attribute bytes, ... } -- type-specific
--   allocated_size  = bytes shown in $FILE_NAME (for system files = 0
--                     since they're hidden/special, OR the actual size)
--   file_size       = same
-- }
-- ----------------------------------------------------------------
local function build_sysfile_record(spec)
    local rec = M.mft.new_record{
        sequence_number = (spec.file_number == 0) and 1 or spec.file_number,
        is_directory    = spec.is_directory or false,
        usa_seq         = 1,
        -- Instances: SI=0, FN=1, SD=2, extras=3..3+N-1 → next free = 3+N.
        next_instance   = spec.instance_start or (3 + #(spec.extra_attrs or {})),
    }

    -- $STANDARD_INFORMATION (instance 0).
    local std_value = M.attrs.std_info_value{
        time_filetime   = spec.time_filetime or 0,
        file_attributes = spec.file_attributes or
                          bit.bor(M.attrs.FILE_ATTRIBUTE_HIDDEN,
                                  M.attrs.FILE_ATTRIBUTE_SYSTEM),
    }
    rec:append(M.attrs.resident{
        type_code = M.attrs.TYPE_STANDARD_INFORMATION,
        value     = std_value,
        instance  = 0,
    })

    -- $FILE_NAME (instance 1).
    local fn_value = M.attrs.file_name_value{
        parent_ref      = spec.parent_ref,
        time_filetime   = spec.time_filetime or 0,
        allocated_size  = spec.allocated_size or 0,
        file_size       = spec.file_size or 0,
        file_attributes = spec.file_attributes or
                          bit.bor(M.attrs.FILE_ATTRIBUTE_HIDDEN,
                                  M.attrs.FILE_ATTRIBUTE_SYSTEM),
        name            = spec.name,
    }
    rec:append(M.attrs.resident{
        type_code = M.attrs.TYPE_FILE_NAME,
        value     = fn_value,
        instance  = 1,
    })

    -- $SECURITY_DESCRIPTOR (instance 2) — required by NT 3.5; without
    -- it NtfsLoadSecurityDescriptor raises STATUS_FILE_CORRUPT_ERROR
    -- on any file open.  Use an inheritable allow-all DACL so newly-
    -- created files inherit the same grant via SeAssignSecurity (a
    -- NULL DACL grants all on direct check but yields STATUS_NO_INHERITANCE,
    -- leaving new files at the mercy of the boot token's default DACL).
    rec:append(M.attrs.resident{
        type_code = M.attrs.TYPE_SECURITY_DESCRIPTOR,
        value     = M.attrs.world_security_descriptor(),
        instance  = 2,
    })

    -- Type-specific attributes (caller-encoded with their own instance
    -- ids).  Caller must start at instance 3 now that SD takes 2.
    for i, attr_bytes in ipairs(spec.extra_attrs or {}) do
        rec:append(attr_bytes)
    end

    return rec:finalize()
end

-- ----------------------------------------------------------------
-- Build a "reserved" MFT record (in_use=false, all zeros except header).
-- Used for records 9 ($Quota -- v3.0+ only) and 11..15.
-- ----------------------------------------------------------------
local function build_reserved_record(file_number)
    local rec = M.mft.new_record{
        sequence_number = (file_number == 0) and 1 or file_number,
        in_use          = false,
        usa_seq         = 1,
    }
    return rec:finalize()
end

-- ----------------------------------------------------------------
-- Build the $MFT $BITMAP value: one bit per MFT record, set if the
-- record is in use.
--
-- System records 0-10 except 9 ($Quota, unused in v1.1).
-- User records 16..16+n_files-1.
-- Records 11-15 reserved (unused).
--
-- Bit layout: byte 0 bit 0 = file 0; byte 0 bit 7 = file 7;
-- byte 1 bit 0 = file 8; etc.  Pad to 8 bytes minimum (NTFS
-- bitmap allocations are typically 8-byte aligned).
-- ----------------------------------------------------------------
local function build_mft_bitmap(total_records, n_user_files)
    local bytes = math.max(8, math.ceil(total_records / 8))
    local buf = ffi.new('uint8_t[?]', bytes)

    local function set(file_num)
        buf[math.floor(file_num / 8)] = bit.bor(
            buf[math.floor(file_num / 8)],
            bit.lshift(1, file_num % 8))
    end

    -- System files 0..10 minus 9.
    for i = 0, 10 do
        if i ~= 9 then set(i) end
    end
    -- User files 16..16+n-1.
    for i = 0, n_user_files - 1 do
        set(M.FIRST_USER_FILE_NUMBER + i)
    end

    return ffi.string(buf, bytes)
end

-- Quad-word alignment helper -- used by every attribute encoder.
-- Local at file scope so all later functions capture it.
local function quad_align(n)
    return bit.band(n + 7, bit.bnot(7))
end

-- Forward declaration: build_dir_record needs build_indexed_dir_attrs
-- which is defined later in this module.  Lua local-scope rules mean
-- the local must be declared before any reference to its name.
local build_indexed_dir_attrs

-- ----------------------------------------------------------------
-- Build a $FILE_NAME index entry for one child node, ready to be
-- placed in a parent directory's $INDEX_ROOT or $INDEX_ALLOCATION
-- buffer.
-- ----------------------------------------------------------------
local function child_index_entry(child, parent_ref, cluster_size)
    local size, alloc
    if child.is_dir then
        size, alloc = 0, 0
    else
        size = child.data_size
        alloc = child.resident and 0 or (child.cluster_count * cluster_size)
    end
    local fn_value = M.attrs.file_name_value{
        parent_ref      = parent_ref,
        time_filetime   = child.time_filetime or 0,
        allocated_size  = alloc,
        file_size       = size,
        file_attributes = child.is_dir
                           and M.attrs.DIRECTORY_ATTRIBUTES
                           or  M.attrs.FILE_ATTRIBUTE_ARCHIVE,
        name            = child.name,
    }
    -- Sequence number for system files = file_number; for user files
    -- we follow the same convention so file references stay unique
    -- across the MFT.
    return M.attrs.index_entry_filename(child.file_number,
                                         child.file_number,
                                         fn_value)
end

-- ----------------------------------------------------------------
-- Build the MFT record for one directory node (root or subdir).
--
-- Two cases:
--   1. Few entries: $INDEX_ROOT contains the entries directly +
--      END terminator.  No $INDEX_ALLOCATION.
--   2. Many entries: $INDEX_ROOT becomes a B+ tree intermediate
--      node containing split-key entries (each pointing at a leaf
--      INDX buffer in $INDEX_ALLOCATION via VCN).  $INDEX_ALLOCATION
--      and $BITMAP attributes are present.
-- ----------------------------------------------------------------
local function file_ref_packed(file_number, seq)
    return ffi.cast('uint64_t', file_number) +
           ffi.cast('uint64_t', seq) * 2 ^ 48
end

local function build_dir_record(dir_node, parent_ref, cluster_size, file_attrs)
    -- Self-reference for INDEX_ENTRYs' parent_ref.
    local self_ref = file_ref_packed(dir_node.file_number,
                                      (dir_node.file_number == 5)
                                          and 5 or dir_node.file_number)

    -- Build INDEX_ENTRYs in already-sorted order.
    local entries = {}
    for _, name in ipairs(dir_node.sorted_child_names or {}) do
        table.insert(entries,
                     child_index_entry(dir_node.children[name],
                                        self_ref, cluster_size))
    end

    local extra_attrs = {}
    local clusters_per_index_buf = math.max(1, M.INDEX_BUFFER_SIZE / cluster_size)

    if not dir_node.use_index_alloc then
        -- Case 1: small dir, all entries in $INDEX_ROOT.
        table.insert(extra_attrs, M.attrs.resident{
            type_code = M.attrs.TYPE_INDEX_ROOT,
            name      = '$I30',
            value     = M.attrs.index_root_filename(entries,
                                                     M.INDEX_BUFFER_SIZE,
                                                     clusters_per_index_buf),
            instance  = 3,
        })
    else
        -- Case 2: large dir, multi-level B+ tree.  build_indexed_dir_attrs
        -- emits leaves + intermediate INDX buffers + the $INDEX_ROOT
        -- value, all derived from compute_layout's pre-computed
        -- dir_node.index_levels structure.
        local indx_blob, ir_attr, alloc_attr, bitmap_attr =
            build_indexed_dir_attrs(dir_node, parent_ref,
                                    cluster_size, clusters_per_index_buf)
        dir_node.index_alloc_blob = indx_blob
        table.insert(extra_attrs, ir_attr)
        table.insert(extra_attrs, alloc_attr)
        table.insert(extra_attrs, bitmap_attr)
    end

    return build_sysfile_record{
        file_number     = dir_node.file_number,
        name            = (dir_node.parent == nil) and '.' or dir_node.name,
        is_directory    = true,
        file_attributes = file_attrs or M.attrs.DIRECTORY_ATTRIBUTES,
        parent_ref      = parent_ref,
        time_filetime   = dir_node.time_filetime or 0,
        extra_attrs     = extra_attrs,
    }
end

-- ----------------------------------------------------------------
-- Build a multi-level B+ tree's on-disk artefacts for an overflow
-- directory.  Reads the pre-computed structure on dir_node (set by
-- compute_layout via build_btree_layout):
--
--   dir_node.index_levels[]: list of {kind='leaf'|'intermediate',
--                                       buffers=[{vcn, entries[],
--                                                  tail_vcn?}]}
--   dir_node.index_root_inputs[]: {name, child_vcn} entries that go
--                                  into $INDEX_ROOT
--   dir_node.index_root_tail_vcn: VCN for $INDEX_ROOT's END entry
--   dir_node.index_total_buffers: total INDX buffers (all levels)
--
-- Returns the concatenated $INDEX_ALLOCATION blob (in VCN order),
-- the resident $INDEX_ROOT attribute, the non-resident
-- $INDEX_ALLOCATION attribute, and the $BITMAP attribute.
-- ----------------------------------------------------------------
build_indexed_dir_attrs = function(dir_node, parent_ref,
                                    cluster_size, clusters_per_index_buf)
    local USA_OFFSET = M.INDX_USA_OFFSET
    local USA_BYTES  = M.INDX_USA_BYTES
    local FE_OFF     = M.INDX_FIRST_ENTRY_OFFSET
    local BUF_SZ     = M.INDEX_BUFFER_SIZE
    -- Local copies because the corresponding file-scope constants
    -- (LEAF_END_SIZE etc.) are declared further down the file, after
    -- this function value has been captured by the forward decl.
    local LEAF_END_SIZE = 16

    -- Self-reference for entries that name a child of dir_node.
    local self_ref = file_ref_packed(dir_node.file_number,
                                      (dir_node.file_number == 5)
                                          and 5 or dir_node.file_number)

    -- Encode a leaf entry (no trailing VCN).
    local function encode_leaf_entry(name)
        return child_index_entry(dir_node.children[name],
                                  self_ref, cluster_size)
    end

    -- Encode an intermediate entry.  If name is nil this is the END
    -- entry (no FILE_NAME, only the trailing VCN).  Otherwise the key
    -- is the named child, and child_vcn is the LEFT subtree VCN.
    local function encode_intermediate_entry(name, child_vcn, is_end)
        local fn_value
        if is_end then
            fn_value = ''
        else
            local child = dir_node.children[name]
            fn_value = M.attrs.file_name_value{
                parent_ref      = self_ref,
                time_filetime   = child.time_filetime or 0,
                allocated_size  = 0,
                file_size       = (child.is_dir and 0) or child.data_size,
                file_attributes = child.is_dir
                                   and M.attrs.DIRECTORY_ATTRIBUTES
                                   or  M.attrs.FILE_ATTRIBUTE_ARCHIVE,
                name            = child.name,
            }
        end
        local attr_len = #fn_value
        local total    = quad_align(0x10 + attr_len + 8)
        local buf      = ffi.new('uint8_t[?]', total)
        if not is_end then
            local child = dir_node.children[name]
            ffi.cast('uint64_t*', buf + 0x00)[0] =
                file_ref_packed(child.file_number, child.file_number)
        end
        ffi.cast('uint16_t*', buf + 0x08)[0] = total
        ffi.cast('uint16_t*', buf + 0x0A)[0] = attr_len
        local flags = M.attrs.INDEX_ENTRY_NODE
        if is_end then flags = bit.bor(flags, M.attrs.INDEX_ENTRY_END) end
        ffi.cast('uint16_t*', buf + 0x0C)[0] = flags
        if attr_len > 0 then ffi.copy(buf + 0x10, fn_value, attr_len) end
        ffi.cast('uint64_t*', buf + total - 8)[0] =
            ffi.cast('uint64_t', child_vcn)
        return ffi.string(buf, total)
    end

    -- Encode one INDX buffer (leaf or intermediate).
    local function encode_indx_buffer(buf_descriptor, kind)
        local out = ffi.new('uint8_t[?]', BUF_SZ)
        local function bcopy(off, src, n)
            if off < 0 or off + n > BUF_SZ then
                error(string.format(
                    "ntfs: INDX buffer overflow on dir '%s' "
                    .. "vcn=%d kind=%s (off=%d n=%d buf=%d)",
                    dir_node.name or '/', buf_descriptor.vcn, kind,
                    off, n, BUF_SZ), 2)
            end
            ffi.copy(out + off, src, n)
        end

        -- MULTI_SECTOR_HEADER
        out[0] = 0x49; out[1] = 0x4E; out[2] = 0x44; out[3] = 0x58
        ffi.cast('uint16_t*', out + 0x04)[0] = USA_OFFSET
        ffi.cast('uint16_t*', out + 0x06)[0] = USA_BYTES / 2
        -- VCN
        ffi.cast('uint64_t*', out + 0x10)[0] =
            ffi.cast('uint64_t', buf_descriptor.vcn)
        -- INDEX_HEADER (offsets relative to +0x18)
        ffi.cast('uint32_t*', out + 0x18 + 0x00)[0] = FE_OFF - 0x18
        out[0x18 + 0x0C] = (kind == 'intermediate') and 1 or 0  -- LARGE_INDEX

        -- Entries + END terminator.
        local off = FE_OFF
        if kind == 'leaf' then
            for _, input in ipairs(buf_descriptor.entries) do
                local e = encode_leaf_entry(input.name)
                bcopy(off, e, #e)
                off = off + #e
            end
            -- LEAF END (16 bytes; no VCN, no FILE_NAME).
            local end_blob = ffi.new('uint8_t[?]', LEAF_END_SIZE)
            ffi.cast('uint16_t*', end_blob + 0x08)[0] = LEAF_END_SIZE
            ffi.cast('uint16_t*', end_blob + 0x0C)[0] = M.attrs.INDEX_ENTRY_END
            bcopy(off, end_blob, LEAF_END_SIZE)
            off = off + LEAF_END_SIZE
        else
            for _, input in ipairs(buf_descriptor.entries) do
                local e = encode_intermediate_entry(input.name,
                                                      input.child_vcn, false)
                bcopy(off, e, #e)
                off = off + #e
            end
            local end_blob = encode_intermediate_entry(nil,
                                                         buf_descriptor.tail_vcn,
                                                         true)
            bcopy(off, end_blob, #end_blob)
            off = off + #end_blob
        end

        ffi.cast('uint32_t*', out + 0x18 + 0x04)[0] = off - 0x18
        ffi.cast('uint32_t*', out + 0x18 + 0x08)[0] = BUF_SZ - 0x18

        -- USA tear-protection.
        local usa_seq = 1
        ffi.cast('uint16_t*', out + USA_OFFSET)[0] = usa_seq
        for i = 0, (BUF_SZ / M.SECTOR_SIZE) - 1 do
            local tail = (i + 1) * M.SECTOR_SIZE - 2
            local orig = ffi.cast('uint16_t*', out + tail)[0]
            ffi.cast('uint16_t*', out + USA_OFFSET + 2 * (i + 1))[0] = orig
            ffi.cast('uint16_t*', out + tail)[0] = usa_seq
        end
        return ffi.string(out, BUF_SZ)
    end

    -- Emit all INDX buffers in VCN (= disk) order.  Levels are stored
    -- bottom-up; VCNs were assigned in level order, so concatenating
    -- by level gives the right disk layout.
    local total_buffers = dir_node.index_total_buffers
    local all_blobs = {}
    for _, level in ipairs(dir_node.index_levels) do
        for _, buf_descriptor in ipairs(level.buffers) do
            -- Slot by VCN (1-based for table.concat).
            all_blobs[buf_descriptor.vcn + 1] =
                encode_indx_buffer(buf_descriptor, level.kind)
        end
    end
    local index_alloc_blob = table.concat(all_blobs)

    -- $INDEX_ROOT contains the topmost level's intermediate entries +
    -- END.  Same encoding as an intermediate INDX buffer's body, sans
    -- MULTI_SECTOR_HEADER / VCN / USA.
    local ir_entries = {}
    for _, p in ipairs(dir_node.index_root_inputs) do
        ir_entries[#ir_entries + 1] =
            encode_intermediate_entry(p.name, p.child_vcn, false)
    end
    ir_entries[#ir_entries + 1] =
        encode_intermediate_entry(nil, dir_node.index_root_tail_vcn, true)
    local entries_blob = table.concat(ir_entries)

    local total_root = 16 + 16 + #entries_blob
    local root_buf   = ffi.new('uint8_t[?]', total_root)
    ffi.cast('uint32_t*', root_buf + 0x00)[0] = M.attrs.TYPE_FILE_NAME
    ffi.cast('uint32_t*', root_buf + 0x04)[0] = M.attrs.COLLATION_FILE_NAME
    ffi.cast('uint32_t*', root_buf + 0x08)[0] = M.INDEX_BUFFER_SIZE
    root_buf[0x0C] = clusters_per_index_buf
    ffi.cast('uint32_t*', root_buf + 0x10 + 0x00)[0] = 0x10
    ffi.cast('uint32_t*', root_buf + 0x10 + 0x04)[0] = 0x10 + #entries_blob
    ffi.cast('uint32_t*', root_buf + 0x10 + 0x08)[0] = 0x10 + #entries_blob
    root_buf[0x10 + 0x0C] = 1   -- LARGE_INDEX
    ffi.copy(root_buf + 0x20, entries_blob, #entries_blob)

    local ir_attr = M.attrs.resident{
        type_code = M.attrs.TYPE_INDEX_ROOT,
        name      = '$I30',
        value     = ffi.string(root_buf, total_root),
        instance  = 3,
    }
    local alloc_attr = M.attrs.nonresident_single_extent{
        type_code      = M.attrs.TYPE_INDEX_ALLOCATION,
        name           = '$I30',
        start_lcn      = dir_node.index_lcn,
        cluster_count  = dir_node.index_clusters,
        allocated_size = dir_node.index_clusters * cluster_size,
        data_size      = dir_node.index_clusters * cluster_size,
        instance       = 4,
    }
    -- $BITMAP tracks all INDX buffers (leaves + intermediates).  All
    -- of ours are in use; bulk-load doesn't leave gaps.
    local bitmap_bytes = math.max(8, math.ceil(total_buffers / 8))
    local bm_buf = ffi.new('uint8_t[?]', bitmap_bytes)
    for i = 0, total_buffers - 1 do
        bm_buf[math.floor(i / 8)] = bit.bor(bm_buf[math.floor(i / 8)],
                                             bit.lshift(1, i % 8))
    end
    local bitmap_attr = M.attrs.resident{
        type_code = M.attrs.TYPE_BITMAP,
        name      = '$I30',
        value     = ffi.string(bm_buf, bitmap_bytes),
        instance  = 5,
    }
    return index_alloc_blob, ir_attr, alloc_attr, bitmap_attr
end

-- ----------------------------------------------------------------
-- Build all MFT records (system + user).  Returns a Lua string of
-- exactly layout.mft_records_total * 1024 bytes.
-- ----------------------------------------------------------------
local function build_mft_records(layout)
    local root_ref = sys_file_ref(M.ROOT_FILE_NAME_INDEX_NUMBER)
    local cluster_size = layout.cluster_size

    local records = {}

    -- Common helper: build a non-resident $DATA attribute pointing at
    -- a single contiguous cluster run.
    --
    -- data_size is the LOGICAL file size in bytes; if nil, defaults to
    -- the cluster-aligned allocation.  Matters when the logical
    -- contents are smaller than the cluster-rounded allocation -- e.g.
    -- $Bitmap (2016 B in 1 × 4 KB cluster), $AttrDef (2240 B in 1 ×
    -- 4 KB), $UpCase happens to be cluster-aligned.  Tools that scan
    -- the bitmap or attrdef read up to data_size, not allocated -- if
    -- we lie, ntfsinfo reports nonsense like "Free Clusters: 200%".
    local function nrdata(start_lcn, cluster_count, instance, data_size)
        return M.attrs.nonresident_single_extent{
            type_code      = M.attrs.TYPE_DATA,
            start_lcn      = start_lcn,
            cluster_count  = cluster_count,
            allocated_size = cluster_count * cluster_size,
            data_size      = data_size or (cluster_count * cluster_size),
            instance       = instance,
        }
    end

    -- File 0: $MFT.  Has $DATA pointing at the MFT itself + $BITMAP
    -- tracking which MFT records are in use.  Bitmap covers system
    -- records (0..10 minus 9) + every user file record (16..16+N-1).
    -- Bytes are LE-bit-numbered: byte 0 bit 0 = file 0.
    local mft_bitmap = build_mft_bitmap(layout.mft_records_total,
                                        #layout.node_list)
    local mft_data_bytes = layout.mft_records_total * M.MFT_RECORD_SIZE
    -- $MFT/$BITMAP is nonresident: see the layout comment above
    -- mft_bitmap_lcn for why.  data_size is the live byte count; the
    -- kernel zero-extends within the cluster as it sets new bits, and
    -- grows the allocation when it crosses a cluster boundary.
    records[1] = build_sysfile_record{
        file_number     = 0,
        name            = '$MFT',
        parent_ref      = root_ref,
        allocated_size  = layout.mft_clusters * cluster_size,
        file_size       = mft_data_bytes,
        extra_attrs     = {
            nrdata(layout.mft_lcn, layout.mft_clusters, 3, mft_data_bytes),
            M.attrs.nonresident_single_extent{
                type_code      = M.attrs.TYPE_BITMAP,
                start_lcn      = layout.mft_bitmap_lcn,
                cluster_count  = layout.mft_bitmap_clusters,
                allocated_size = layout.mft_bitmap_clusters * cluster_size,
                data_size      = #mft_bitmap,
                instance       = 4,
            },
        },
        instance_start  = 5,
    }

    -- File 1: $MFTMirr.
    records[2] = build_sysfile_record{
        file_number     = 1,
        name            = '$MFTMirr',
        parent_ref      = root_ref,
        allocated_size  = layout.mft_mirr_clusters * cluster_size,
        file_size       = layout.mft_mirr_clusters * cluster_size,
        extra_attrs     = { nrdata(layout.mft_mirr_lcn,
                                   layout.mft_mirr_clusters, 3) },
    }

    -- File 2: $LogFile.
    records[3] = build_sysfile_record{
        file_number     = 2,
        name            = '$LogFile',
        parent_ref      = root_ref,
        allocated_size  = layout.log_clusters * cluster_size,
        file_size       = layout.log_clusters * cluster_size,
        extra_attrs     = { nrdata(layout.log_lcn,
                                   layout.log_clusters, 3) },
    }

    -- File 3: $Volume.  Has $VOLUME_NAME + $VOLUME_INFORMATION instead
    -- of $DATA -- the volume itself doesn't have file content.
    local vol_name_w = M.attrs.utf16le(layout.volume_label)
    records[4] = build_sysfile_record{
        file_number     = 3,
        name            = '$Volume',
        parent_ref      = root_ref,
        extra_attrs     = {
            M.attrs.resident{
                type_code = M.attrs.TYPE_VOLUME_NAME,
                value     = vol_name_w,
                instance  = 3,
            },
            M.attrs.resident{
                type_code = M.attrs.TYPE_VOLUME_INFORMATION,
                value     = M.attrs.volume_info_value(M.NTFS_MAJOR_VERSION,
                                                      M.NTFS_MINOR_VERSION,
                                                      0),
                instance  = 4,
            },
        },
    }

    -- File 4: $AttrDef.  Logical size = 14 entries × 160 B = 2240 B,
    -- packed in 1 × 4 KB cluster (cluster-rounded allocation).
    local attrdef_size = 14 * 160
    records[5] = build_sysfile_record{
        file_number     = 4,
        name            = '$AttrDef',
        parent_ref      = root_ref,
        allocated_size  = cluster_size,
        file_size       = attrdef_size,
        extra_attrs     = { nrdata(layout.attrdef_lcn, 1, 3, attrdef_size) },
    }

    -- File 5: . (root directory).  Build via the shared dir-record
    -- helper below (same code path used for every subdirectory).
    records[6] = build_dir_record(layout.root_node, root_ref,
                                  cluster_size,
                                  bit.bor(M.attrs.FILE_ATTRIBUTE_HIDDEN,
                                          M.attrs.FILE_ATTRIBUTE_SYSTEM,
                                          M.attrs.DIRECTORY_ATTRIBUTES))

    -- File 6: $Bitmap.  data_size = exactly the bytes needed to track
    -- total_clusters bits (ntfsinfo computes free-cluster pct from
    -- this; cluster-rounded data_size makes the tail look like
    -- bonus free clusters and the math goes >100%).
    records[7] = build_sysfile_record{
        file_number     = 6,
        name            = '$Bitmap',
        parent_ref      = root_ref,
        allocated_size  = layout.bitmap_clusters * cluster_size,
        file_size       = layout.bitmap_data_bytes,
        extra_attrs     = { nrdata(layout.bitmap_lcn,
                                   layout.bitmap_clusters, 3,
                                   layout.bitmap_data_bytes) },
    }

    -- File 7: $Boot.  Logical = BYTES_IN_BOOT_AREA (8 KB), allocation
    -- = 2 × 4 KB clusters (cluster-aligned coincidence).
    records[8] = build_sysfile_record{
        file_number     = 7,
        name            = '$Boot',
        parent_ref      = root_ref,
        allocated_size  = layout.boot_clusters * cluster_size,
        file_size       = M.BYTES_IN_BOOT_AREA,
        extra_attrs     = { nrdata(0, layout.boot_clusters, 3,
                                   M.BYTES_IN_BOOT_AREA) },
    }

    -- File 8: $BadClus.  Empty file (no bad clusters tracked).  Has
    -- a resident $DATA NAMED "$Bad" of zero length.  NT 3.5's NtfsOpenSystemFile
    -- looks up the bad-cluster $DATA by attribute name "$Bad" — an unnamed
    -- $DATA causes NtfsLookupAttributeForScb to raise STATUS_FILE_CORRUPT_ERROR
    -- mid-mount, which propagates as INACCESSIBLE_BOOT_DEVICE (0x7B) when this
    -- is the boot volume.
    records[9] = build_sysfile_record{
        file_number     = 8,
        name            = '$BadClus',
        parent_ref      = root_ref,
        extra_attrs     = {
            M.attrs.resident{
                type_code = M.attrs.TYPE_DATA,
                name      = '$Bad',
                value     = '',
                instance  = 3,
            },
        },
    }

    -- File 9: $Quota -- v3.0+ feature, leave reserved.
    records[10] = build_reserved_record(9)

    -- File 10: $UpCase.
    records[11] = build_sysfile_record{
        file_number     = 10,
        name            = '$UpCase',
        parent_ref      = root_ref,
        allocated_size  = layout.upcase_clusters * cluster_size,
        file_size       = 65536 * 2,    -- 128 KB
        extra_attrs     = { nrdata(layout.upcase_lcn,
                                   layout.upcase_clusters, 3) },
    }

    -- Files 11..15: reserved.
    for i = 12, 16 do
        records[i] = build_reserved_record(i - 1)
    end

    -- User MFT records: walk node_list (excludes root, includes every
    -- file + subdir in BFS order matching file numbering).
    for i, node in ipairs(layout.node_list) do
        local parent_ref = sys_file_ref(node.parent.file_number)
        if node.is_dir then
            records[16 + i] = build_dir_record(node, parent_ref, cluster_size)
        else
            local data_attr
            if node.resident then
                data_attr = M.attrs.resident{
                    type_code = M.attrs.TYPE_DATA,
                    value     = node.data,
                    instance  = 3,
                }
            else
                data_attr = M.attrs.nonresident_single_extent{
                    type_code      = M.attrs.TYPE_DATA,
                    start_lcn      = node.start_lcn,
                    cluster_count  = node.cluster_count,
                    allocated_size = node.cluster_count * cluster_size,
                    data_size      = node.data_size,
                    instance       = 3,
                }
            end
            records[16 + i] = build_sysfile_record{
                file_number     = node.file_number,
                name            = node.name,
                file_attributes = M.attrs.FILE_ATTRIBUTE_ARCHIVE,
                parent_ref      = parent_ref,
                time_filetime   = node.time_filetime,
                allocated_size  = node.resident and 0
                                   or (node.cluster_count * cluster_size),
                file_size       = node.data_size,
                extra_attrs     = { data_attr },
            }
        end
    end

    return table.concat(records)
end

-- ----------------------------------------------------------------
-- Build the cluster-allocation bitmap.  One bit per cluster in the
-- volume; bit set = in use.  Pad to a multiple of cluster_size.
-- ----------------------------------------------------------------
local function build_cluster_bitmap(layout)
    local total_clusters = layout.total_clusters
    local bytes = math.floor((total_clusters + 7) / 8)
    -- Pad to cluster boundary (NTFS requires the on-disk bitmap to
    -- fill all the clusters its $DATA attribute claims).
    local padded = layout.bitmap_clusters * layout.cluster_size
    if bytes < padded then bytes = padded end

    local buf = ffi.new('uint8_t[?]', bytes)

    local function set(start_lcn, count)
        for c = start_lcn, start_lcn + count - 1 do
            local byte_off = math.floor(c / 8)
            local bit_idx  = c % 8
            buf[byte_off] = bit.bor(buf[byte_off], bit.lshift(1, bit_idx))
        end
    end

    set(0,                       layout.boot_clusters)        -- $Boot
    set(layout.mft_lcn,          layout.mft_clusters)         -- $MFT
    set(layout.mft_mirr_lcn,     layout.mft_mirr_clusters)    -- $MFTMirr
    set(layout.log_lcn,          layout.log_clusters)         -- $LogFile
    set(layout.attrdef_lcn,      1)                           -- $AttrDef
    set(layout.upcase_lcn,       layout.upcase_clusters)      -- $UpCase
    set(layout.bitmap_lcn,       layout.bitmap_clusters)      -- $Bitmap
    set(layout.mft_bitmap_lcn,   layout.mft_bitmap_clusters)  -- $MFT/$BITMAP

    -- User nodes: claim their clusters.
    for _, n in ipairs(layout.node_list) do
        if n.is_dir then
            if n.use_index_alloc then
                set(n.index_lcn, n.index_clusters)
            end
        else
            if not n.resident then
                set(n.start_lcn, n.cluster_count)
            end
        end
    end

    return ffi.string(buf, bytes)
end

-- Build the MFT bitmap for n_user_nodes (file count from the
-- flattened tree).  Same rule as before: bit i set if MFT record i
-- is in use.
local function build_mft_bitmap_for_layout(layout)
    return build_mft_bitmap(layout.mft_records_total, #layout.node_list)
end

-- ----------------------------------------------------------------
-- Resident $DATA size limit for a typical short-name file.  An MFT
-- record (1024 B) holds: USA (6 B) + header (0x30 incl. USA) +
-- $STANDARD_INFORMATION (72 B) + $FILE_NAME (24 + 0x42 + 2*name_chars)
-- + $DATA header (24) + value + $END (8).  For an 8-char name, $DATA
-- value caps at ~700 B.  Files larger than this go non-resident.
M.RESIDENT_DATA_THRESHOLD = 600

-- Threshold for resident vs non-resident $INDEX_ROOT.  When a
-- directory's INDEX_ENTRYs all fit in this many bytes, we use just
-- $INDEX_ROOT (resident).  Beyond this, we spill to $INDEX_ALLOCATION
-- (non-resident INDX buffer leaves) with $INDEX_ROOT acting as the
-- intermediate-node root of a B+ tree.
M.RESIDENT_INDEX_THRESHOLD = 400

-- ----------------------------------------------------------------
-- Flatten the directory tree into a list of nodes (excluding root)
-- and assign file numbers.  Root is always file 5; descendants get
-- 16, 17, ... in BFS order (so children of any node have higher
-- file numbers than the node itself, matching real NTFS layout
-- intuition).
--
-- Returns:
--   list   -- array of nodes in file-number order (root EXCLUDED)
--   total  -- total file numbers used = 16 + #list
-- ----------------------------------------------------------------
local function flatten_tree(root)
    root.file_number = M.ROOT_FILE_NAME_INDEX_NUMBER     -- 5

    local list = {}
    local next_fn = M.FIRST_USER_FILE_NUMBER             -- 16

    -- BFS so directories come before their contents in the file
    -- numbering (which keeps the MFT mostly sorted by tree depth).
    local queue = { root }
    local qi = 1
    while qi <= #queue do
        local node = queue[qi]; qi = qi + 1
        if node.is_dir then
            -- Sort children by uppercased name (matches our partial-
            -- fold UpCase + COLLATION_FILE_NAME).
            local names = {}
            for k in pairs(node.children) do
                table.insert(names, k)
            end
            table.sort(names, function(a, b) return a:upper() < b:upper() end)
            node.sorted_child_names = names
            for _, name in ipairs(names) do
                local child = node.children[name]
                child.file_number = next_fn
                next_fn = next_fn + 1
                table.insert(list, child)
                if child.is_dir then table.insert(queue, child) end
            end
        end
    end

    return list, next_fn
end

-- ----------------------------------------------------------------
-- Multi-level B+ tree bulk-load.
--
-- Mirrors NT 3.5 INDEXSUP.C runtime semantics, computed up-front
-- since at build time we know the entire sorted entry list.
--
-- Per-level packing is identical: greedy fill INDX-sized buffers
-- with entries; when an entry doesn't fit, close the buffer and
-- promote the entry to the parent level.  The promoted entry lives
-- ONLY at the parent — never duplicated in the level below — so
-- in-order traversal yields each key exactly once (this is what
-- INDEXSUP.C/InsertWithBufferSplit also enforces).
--
-- Difference between leaf and intermediate levels:
--   * intermediate entries carry an 8-byte trailing VCN pointing at
--     their LEFT subtree (subtree of keys < entry's key);
--   * intermediate buffer END entry has flags = NODE|END and a
--     trailing VCN pointing at the rightmost subtree.
--   * leaf entries have no trailing VCN; leaf END is 16 bytes.
--
-- Tree depth grows when the topmost level can't fit in the
-- $INDEX_ROOT entries budget.  Equivalent to PushIndexRoot()
-- in INDEXSUP.C, just done statically.
-- ----------------------------------------------------------------

local function leaf_entry_size(name)
    return quad_align(0x10 + 0x42 + 2 * #name)
end
local function intermediate_entry_size(name)
    return quad_align(0x10 + 0x42 + 2 * #name + 8)
end
local INTERMEDIATE_END_SIZE = 24   -- quad_align(0x10 + 0 + 8)
local LEAF_END_SIZE         = 16

-- Conservative budget for $INDEX_ROOT's entries+END area.  Real fit
-- depends on what other attrs share the directory's MFT record
-- (header, USA, $STANDARD_INFORMATION, $FILE_NAME, $INDEX_ALLOCATION,
-- $BITMAP, end-marker).  We keep this conservative so the loop in
-- compute_layout grows the tree depth before MFT packing fails.
local INDEX_ROOT_ENTRIES_BUDGET = 400

-- Total bytes if we placed `inputs` (with their trailing END) into
-- $INDEX_ROOT.  Each input is { name = "...", child_vcn = N? }; END
-- contributes 24 bytes regardless.
local function index_root_entries_bytes(inputs)
    local total = INTERMEDIATE_END_SIZE
    for _, p in ipairs(inputs) do
        total = total + intermediate_entry_size(p.name)
    end
    return total
end

-- Pack a sorted list of inputs into INDX-sized buffers.  Returns:
--   buffers[]: list of { entries=[input...], tail_vcn=?, vcn=N }
--              (vcn = the buffer's VCN within $INDEX_ALLOCATION)
--   promotes[]: list of { name=..., closed_buffer_vcn=N } — the inputs
--               that overflowed; parent level places them as entries
--               whose left-child VCN is the closed buffer.
--   last_vcn: VCN of the rightmost buffer at this level — parent uses
--             it as the END VCN of the parent buffer that holds the
--             last-promoted key.
--
-- next_vcn_ref is { N }: a 1-element table acting as a mutable VCN
-- counter so all levels share a sequential VCN allocator.
local function pack_indx_level(inputs, kind, buf_capacity_bytes,
                                tail_vcn, next_vcn_ref)
    local end_size      = (kind == 'leaf') and LEAF_END_SIZE or INTERMEDIATE_END_SIZE
    local entry_size_fn = (kind == 'leaf') and leaf_entry_size or intermediate_entry_size

    local buffers = {}
    local promotes = {}
    local cur = { entries = {}, bytes_used = 0 }

    local i = 1
    while i <= #inputs do
        local input = inputs[i]
        local esize = entry_size_fn(input.name)
        if cur.bytes_used + esize + end_size > buf_capacity_bytes
           and #cur.entries > 0 then
            -- Close current buffer.  For intermediate level: END VCN
            -- = THIS overflow input's child_vcn (the keys that didn't
            -- fit live in *that* subtree).  For leaf level: no END VCN.
            local closed_vcn = next_vcn_ref[1]
            next_vcn_ref[1]  = closed_vcn + 1
            cur.vcn          = closed_vcn
            if kind == 'intermediate' then cur.tail_vcn = input.child_vcn end
            table.insert(buffers, cur)
            -- Promote this input.
            table.insert(promotes,
                          { name = input.name, closed_buffer_vcn = closed_vcn })
            cur = { entries = {}, bytes_used = 0 }
            i = i + 1
        else
            table.insert(cur.entries, input)
            cur.bytes_used = cur.bytes_used + esize
            i = i + 1
        end
    end
    -- Close the last buffer.  END VCN = caller-supplied tail_vcn for
    -- intermediate level; nothing for leaf.
    local last_vcn = next_vcn_ref[1]
    next_vcn_ref[1] = last_vcn + 1
    cur.vcn = last_vcn
    if kind == 'intermediate' then cur.tail_vcn = tail_vcn end
    table.insert(buffers, cur)
    return buffers, promotes, last_vcn
end

-- Build the multi-level B+ tree for a directory.  Mutates `node`
-- with: index_levels, index_root_inputs, index_root_tail_vcn,
-- index_total_buffers.  Allocates VCNs 0..index_total_buffers-1.
local function build_btree_layout(node)
    local BUF_SZ        = M.INDEX_BUFFER_SIZE
    local FE_OFF        = M.INDX_FIRST_ENTRY_OFFSET
    local buf_capacity  = BUF_SZ - FE_OFF   -- entries + END all live here

    local next_vcn_ref = { 0 }

    -- Level 0: leaves.
    local leaf_inputs = {}
    for _, name in ipairs(node.sorted_child_names) do
        leaf_inputs[#leaf_inputs + 1] = { name = name }
    end
    local leaf_buffers, leaf_promotes, leaf_last_vcn =
        pack_indx_level(leaf_inputs, 'leaf', buf_capacity, nil, next_vcn_ref)

    local levels = { { kind = 'leaf', buffers = leaf_buffers } }

    -- Convert leaf-level promotes into intermediate-level inputs:
    -- each carries (name, child_vcn = closed_leaf_vcn).
    local current_inputs = {}
    for _, p in ipairs(leaf_promotes) do
        current_inputs[#current_inputs + 1] =
            { name = p.name, child_vcn = p.closed_buffer_vcn }
    end
    local current_tail_vcn = leaf_last_vcn

    -- Build intermediate levels until the topmost fits in $INDEX_ROOT.
    while index_root_entries_bytes(current_inputs) > INDEX_ROOT_ENTRIES_BUDGET do
        local int_buffers, int_promotes, int_last_vcn =
            pack_indx_level(current_inputs, 'intermediate', buf_capacity,
                             current_tail_vcn, next_vcn_ref)
        levels[#levels + 1] = { kind = 'intermediate', buffers = int_buffers }
        current_inputs = {}
        for _, p in ipairs(int_promotes) do
            current_inputs[#current_inputs + 1] =
                { name = p.name, child_vcn = p.closed_buffer_vcn }
        end
        current_tail_vcn = int_last_vcn
    end

    node.index_levels         = levels
    node.index_root_inputs    = current_inputs
    node.index_root_tail_vcn  = current_tail_vcn
    node.index_total_buffers  = next_vcn_ref[1]
end

-- ----------------------------------------------------------------
-- Compute the layout for a given partition size.
--
-- Partition total bytes = opts.size_bytes (NTFS volume size).  All
-- LCN values are relative to the partition start.  The disk-level
-- start LBA flows in via the build(ctx) call as ctx.start_lba and
-- is baked into the boot sector's HiddenSectors.
--
-- Files in self.files contribute to:
--   - MFT size (one record per file, at file_number 16+)
--   - Cluster allocation for non-resident $DATA (files > threshold)
-- ----------------------------------------------------------------
local function compute_layout(opts)
    local cluster_size       = opts.sectors_per_cluster * M.SECTOR_SIZE
    local total_sectors      = math.floor(opts._size_bytes / M.SECTOR_SIZE)
    local total_clusters     = math.floor(total_sectors / opts.sectors_per_cluster)

    -- Flatten the directory tree.  Mutates the tree to assign
    -- file_numbers; subsequent layout calls would re-flatten and
    -- re-assign (idempotent for a fixed tree).
    local node_list, total_mft_records = flatten_tree(opts.root)
    local n_files = #node_list

    local boot_clusters = math.max(1, math.floor((M.BYTES_IN_BOOT_AREA +
                                                  cluster_size - 1) /
                                                  cluster_size))

    -- MFT sized for system records + every user file's record.
    local mft_clusters     = math.max(1, math.ceil(total_mft_records *
                                                   M.MFT_RECORD_SIZE /
                                                   cluster_size))
    local mft_mirr_clusters= math.max(1, math.ceil(M.REFLECTED_MFT_SEGMENTS *
                                                   M.MFT_RECORD_SIZE /
                                                   cluster_size))
    local log_clusters     = M.LOG_FILE_CLUSTERS
    local upcase_clusters  = math.max(1, math.ceil(65536 * 2 / cluster_size))

    -- Bitmap: 1 bit per cluster, rounded up to whole bytes, then to
    -- whole clusters.
    local bitmap_data_bytes = math.floor((total_clusters + 7) / 8)
    local bitmap_clusters   = math.max(1, math.ceil(bitmap_data_bytes /
                                                    cluster_size))

    -- Sequential allocation: system structures first.
    -- $MFT/$BITMAP gets its own cluster so the attribute is nonresident
    -- from format time.  NT 4.0 onward assumes nonresident throughout
    -- the bitmap-extend code paths; the resident form would require
    -- shuffling the FRS layout on every grow, which is contention-
    -- and lifetime-hostile (cached attribute pointers go stale across
    -- the move, and the FRS-exclusive lock blocks readers on every
    -- create).  Single cluster gives us 8*cluster_size bits which is
    -- plenty of headroom — the kernel will extend nonresidently when
    -- it fills.
    local mft_bitmap_clusters = 1
    local lcn = boot_clusters
    local mft_lcn = lcn;        lcn = lcn + mft_clusters
    local mft_mirr_lcn = lcn;   lcn = lcn + mft_mirr_clusters
    local log_lcn = lcn;        lcn = lcn + log_clusters
    local attrdef_lcn = lcn;    lcn = lcn + 1
    local upcase_lcn = lcn;     lcn = lcn + upcase_clusters
    local bitmap_lcn = lcn;     lcn = lcn + bitmap_clusters
    local mft_bitmap_lcn = lcn; lcn = lcn + mft_bitmap_clusters

    -- User files / dirs: walk the flattened tree.  For files larger
    -- than the resident threshold, allocate clusters contiguously
    -- after the system area.  For directories with too many entries
    -- to fit in $INDEX_ROOT, allocate clusters for $INDEX_ALLOCATION
    -- INDX leaf buffers (Phase 2d B+ tree).
    --
    -- Each node mutates with: data_size, resident, start_lcn,
    -- cluster_count (for files); index_clusters, index_lcn,
    -- index_leaves (for directories that overflow $INDEX_ROOT).
    for _, node in ipairs(node_list) do
        if node.is_dir then
            -- Estimate index entry sizes to decide if we need
            -- $INDEX_ALLOCATION.  Each entry is 16 (header) + 66
            -- (FILE_NAME header) + 2*name_len, quad-aligned.
            local total_entry_bytes = 0
            for _, name in ipairs(node.sorted_child_names) do
                local entry_size = quad_align(16 + 66 + 2 * #name)
                total_entry_bytes = total_entry_bytes + entry_size
            end
            node.total_entry_bytes = total_entry_bytes

            if total_entry_bytes <= M.RESIDENT_INDEX_THRESHOLD then
                -- Fits in $INDEX_ROOT, no $INDEX_ALLOCATION needed.
                node.use_index_alloc = false
            else
                -- Multi-level B+ tree.  build_btree_layout computes
                -- leaves + intermediate levels + $INDEX_ROOT contents,
                -- assigning sequential VCNs starting at 0.  We then
                -- map those VCNs to the directory's $INDEX_ALLOCATION
                -- run starting at `lcn`.
                node.use_index_alloc = true
                build_btree_layout(node)
                local clusters_per_buf = math.max(1, M.INDEX_BUFFER_SIZE
                                                       / cluster_size)
                node.index_clusters = node.index_total_buffers * clusters_per_buf
                node.index_lcn      = lcn
                lcn = lcn + node.index_clusters
            end
        else
            -- Regular file.
            node.data_size = #node.data
            node.resident  = (node.data_size <= M.RESIDENT_DATA_THRESHOLD)
            if not node.resident then
                node.start_lcn     = lcn
                node.cluster_count = math.max(1,
                                              math.ceil(node.data_size /
                                                        cluster_size))
                lcn = lcn + node.cluster_count
            end
        end
    end

    -- Final overflow check: catch the case where files + system
    -- structures don't fit in the partition.  Without this, every
    -- write past the partition's tail corrupts the LuaJIT heap and
    -- the next GC pass segfaults.
    if lcn > total_clusters then
        error(string.format(
            "ntfs: layout doesn't fit — need %d clusters, partition has %d "
            .. "(%d MB requested, partition is %d MB at %d B/cluster)",
            lcn, total_clusters,
            math.floor(lcn * cluster_size / (1024 * 1024)),
            math.floor(opts._size_bytes / (1024 * 1024)),
            cluster_size), 2)
    end

    return {
        size_bytes         = opts._size_bytes,
        cluster_size       = cluster_size,
        sectors_per_cluster= opts.sectors_per_cluster,
        total_sectors      = total_sectors,
        total_clusters     = total_clusters,
        volume_label       = opts.volume_label,
        volume_serial      = opts.volume_serial,

        boot_clusters      = boot_clusters,
        mft_lcn            = mft_lcn,
        mft_clusters       = mft_clusters,
        mft_records_total  = total_mft_records,
        mft_mirr_lcn       = mft_mirr_lcn,
        mft_mirr_clusters  = mft_mirr_clusters,
        log_lcn            = log_lcn,
        log_clusters       = log_clusters,
        attrdef_lcn        = attrdef_lcn,
        upcase_lcn         = upcase_lcn,
        upcase_clusters    = upcase_clusters,
        bitmap_lcn         = bitmap_lcn,
        bitmap_clusters    = bitmap_clusters,
        bitmap_data_bytes  = bitmap_data_bytes,
        mft_bitmap_lcn     = mft_bitmap_lcn,
        mft_bitmap_clusters= mft_bitmap_clusters,

        node_list          = node_list,
        root_node          = opts.root,
        clusters_used      = lcn,
    }
end

-- ----------------------------------------------------------------
-- NtfsImage -- public API.  Mirrors ntosbe.disk's DiskImage.
-- ----------------------------------------------------------------

local NtfsImage = {}
NtfsImage.__index = NtfsImage

function M.new(opts)
    opts = opts or {}
    local size_mb = opts.size_mb or 64
    if size_mb < 8 then
        error('NTFS volume must be at least 8 MB', 2)
    end
    -- Root directory node.  Tree structure:
    --   { is_dir, children = name->node, data, name, parent, file_number,
    --     time_filetime }
    -- Root has no parent and no name (root has its own special $FILE_NAME
    -- with name="." and parent=self).
    local root = {
        is_dir   = true,
        children = {},
        parent   = nil,
        name     = nil,
    }
    return setmetatable({
        _size_bytes         = size_mb * 1024 * 1024,
        sectors_per_cluster = opts.sectors_per_cluster or 8,   -- 4 KB cluster
        volume_label        = opts.volume_label or 'NTFS',
        volume_serial       = opts.volume_serial or 0,
        -- `now` is stored as a FILETIME (100-ns ticks since 1601 UTC) so
        -- it can be assigned directly into `time_filetime` slots without
        -- re-converting at every use site.  Callers pass Unix seconds
        -- (matching platform.now()'s semantics).
        now                 = unix_to_filetime(opts.now),
        root                = root,
    }, NtfsImage)
end

-- size_bytes — committed at construction; the composer reads this
-- before calling :build() to lay out partition LBAs.
function NtfsImage:size_bytes()
    return self._size_bytes
end

-- Split a path into components.  Accepts both forward and back slash;
-- collapses repeats; rejects empty / "." / ".." (no traversal here).
local function split_path(path)
    local parts = {}
    for component in path:gmatch('[^/\\]+') do
        if component == '.' or component == '..' then
            error('ntfs: path traversal not supported (got "' ..
                  component .. '" in "' .. path .. '")', 3)
        end
        if #component == 0 then
            error('ntfs: empty path component in "' .. path .. '"', 3)
        end
        table.insert(parts, component)
    end
    return parts
end

-- Walk tree creating intermediate dirs as needed.  Returns the
-- terminal directory node.
function NtfsImage:_get_or_create_dir(parts)
    local node = self.root
    for _, name in ipairs(parts) do
        local child = node.children[name]
        if child == nil then
            child = {
                is_dir        = true,
                children      = {},
                parent        = node,
                name          = name,
                time_filetime = self.now,
            }
            node.children[name] = child
        elseif not child.is_dir then
            error('ntfs: path component "' .. name ..
                  '" is a file, not a directory', 3)
        end
        node = child
    end
    return node
end

function NtfsImage:mkdir(path)
    self:_get_or_create_dir(split_path(path))
    return self
end

function NtfsImage:add_file(dest, src_path, platform)
    local data = platform.read_file(src_path)
    if not data then
        error('ntfs.add_file: cannot read ' .. tostring(src_path), 2)
    end
    local mtime_unix = platform.mtime and platform.mtime(src_path)
    return self:add_bytes(dest, data, {
        time_filetime = mtime_unix and unix_to_filetime(mtime_unix) or self.now,
    })
end

function NtfsImage:add_bytes(dest, data, opts)
    opts = opts or {}
    local parts = split_path(dest)
    if #parts == 0 then
        error('ntfs.add_bytes: empty path', 2)
    end
    local leaf_name = table.remove(parts)
    local parent_dir = self:_get_or_create_dir(parts)
    if parent_dir.children[leaf_name] then
        error('ntfs.add_bytes: "' .. dest .. '" already exists', 2)
    end
    parent_dir.children[leaf_name] = {
        is_dir        = false,
        data          = data,
        parent        = parent_dir,
        name          = leaf_name,
        time_filetime = opts.time_filetime or self.now,
    }
    return self
end

-- ----------------------------------------------------------------
-- Build the partition payload bytes.
--
-- ctx = { start_lba, sector_size }
--   start_lba is the partition's absolute LBA on the disk; baked
--   into the NTFS boot sector's HiddenSectors field.  The composer
--   passes this in.
-- ----------------------------------------------------------------
function NtfsImage:build(platform, ctx)
    if not platform then
        error('NtfsImage:build: pass platform module as 1st arg', 2)
    end
    ctx = ctx or {}
    local start_lba = ctx.start_lba or 0

    local layout = compute_layout(self)
    local img = ffi.new('uint8_t[?]', layout.size_bytes)

    -- Bounds-checked copy.  Writing past the partition tail trashes
    -- the LuaJIT heap and crashes the next GC pass; surface the
    -- offending copy with a clear error instead.
    local size_b = layout.size_bytes
    local function bcopy(label, off, src, n)
        if off < 0 or off + n > size_b then
            error(string.format(
                "ntfs %s: out-of-bounds write off=%d n=%d (partition size=%d)",
                label, off, n, size_b), 2)
        end
        ffi.copy(img + off, src, n)
    end

    -- 1. NTFS boot sector at partition LBA 0.
    --    HiddenSectors = start_lba so the BPB matches the disk-level
    --    MBR's claim about where this partition sits.
    local boot = M.boot.encode{
        sector_size      = M.SECTOR_SIZE,
        sectors_per_clus = layout.sectors_per_cluster,
        total_sectors    = layout.total_sectors,
        mft_lcn          = layout.mft_lcn,
        mft_mirr_lcn     = layout.mft_mirr_lcn,
        serial_number    = layout.volume_serial,
        hidden_sectors   = start_lba,
    }
    bcopy("boot", 0, boot, #boot)
    -- Rest of $Boot file area (clusters 0..boot_clusters-1) stays zero.

    -- 2. MFT records at MFT LCN.
    local mft_bytes = build_mft_records(layout)
    bcopy("mft", layout.mft_lcn * layout.cluster_size,
          mft_bytes, #mft_bytes)

    -- 3. MFT2 mirror: copies of records 0..3 (the 4 first MFT records).
    local mirror_size = M.REFLECTED_MFT_SEGMENTS * M.MFT_RECORD_SIZE
    bcopy("mft_mirr", layout.mft_mirr_lcn * layout.cluster_size,
          mft_bytes, mirror_size)

    -- 4. $LogFile.  LFS expects the on-disk format for "uninitialized"
    --    pages to be `0xFFFFFFFF` (the LFS_SIGNATURE_UNINITIALIZED_ULONG
    --    constant) — NOT zero!  LfsReadRestart's walk over the log scans
    --    each 512-byte page's signature word and clears
    --    *UninitializedFile if the signature doesn't match
    --    LFS_SIGNATURE_UNINITIALIZED_ULONG (0xFFFFFFFF), record (RCRD),
    --    modified (CHKD), or restart (RSTR).  A zero-filled log fails
    --    that check immediately and the caller takes the corrupt-disk
    --    branch → STATUS_DISK_CORRUPT_ERROR → mount aborts.
    local log_bytes = layout.log_clusters * layout.cluster_size
    local log_buf = ffi.new('uint8_t[?]', log_bytes)
    ffi.fill(log_buf, log_bytes, 0xFF)
    bcopy("log", layout.log_lcn * layout.cluster_size, log_buf, log_bytes)

    -- 5. $AttrDef bytes.
    local ad = M.attrs.attrdef_table()
    bcopy("attrdef", layout.attrdef_lcn * layout.cluster_size, ad, #ad)

    -- 6. $UpCase identity table.
    local uc = M.attrs.identity_upcase()
    bcopy("upcase", layout.upcase_lcn * layout.cluster_size, uc, #uc)

    -- 7. Cluster bitmap.
    local cb = build_cluster_bitmap(layout)
    bcopy("bitmap", layout.bitmap_lcn * layout.cluster_size, cb, #cb)

    -- 7b. $MFT/$BITMAP — same bytes the FRS-resident form held in
    -- pre-NT4 layouts, now in its own cluster so bitmap-extend writes
    -- never touch FRS 0's attribute layout.
    local mft_bm = build_mft_bitmap_for_layout(layout)
    bcopy("mft_bitmap", layout.mft_bitmap_lcn * layout.cluster_size,
          mft_bm, #mft_bm)

    -- 8. User node data:
    --    - Files with non-resident $DATA: write bytes to allocated clusters.
    --    - Directories with $INDEX_ALLOCATION: write the leaves blob to
    --      the allocated index cluster range.  build_dir_record stashes
    --      the encoded leaves in node.index_alloc_blob during
    --      build_mft_records.
    for _, n in ipairs(layout.node_list) do
        if n.is_dir then
            if n.use_index_alloc and n.index_alloc_blob then
                bcopy("index_alloc(" .. (n.name or "?") .. ")",
                      n.index_lcn * layout.cluster_size,
                      n.index_alloc_blob, #n.index_alloc_blob)
            end
        else
            if not n.resident then
                bcopy("file(" .. (n.name or "?") .. ")",
                      n.start_lcn * layout.cluster_size,
                      n.data, n.data_size)
            end
        end
    end

    return {
        bytes       = ffi.string(img, layout.size_bytes),
        type_code   = M.PARTITION_TYPE_NTFS,
        label       = layout.volume_label,
        sector_size = M.SECTOR_SIZE,
        stats = {
            cluster_size   = layout.cluster_size,
            total_clusters = layout.total_clusters,
            clusters_used  = layout.clusters_used,
            mft_lcn        = layout.mft_lcn,
            mft_mirr_lcn   = layout.mft_mirr_lcn,
        },
    }
end

return M
