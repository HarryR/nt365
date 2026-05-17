-- ntosbe — the NT OS Build Environment.
--
-- The MicroNT build system as a self-sufficient Lua package, with no
-- Python dependency and no host-only assumptions.  Host entry is
-- src/build.sh → ntosbe.build.main; the in-OS entry will route through
-- the same ntosbe.build module once the platform.spawn_wait NT
-- backend lands, closing the orobouros self-build loop.
--
-- pkg/ntosbe/ is staged at \SystemRoot\lua\ntosbe\ on disk like every
-- other package, so a booted MicroNT image automatically carries its
-- own build environment.  No separate "now we copy tools into the OS"
-- step.
--
-- Subpackage layout:
--
--   platform.lua    host-vs-OS abstraction: file I/O, directory
--                   listing, timestamps, logging.
--   layers/         one .lua per capability slice.  Each layer owns
--                   its driver files, SYSTEM-hive service entries and
--                   boot-driver order.  See layers/core.lua.
--   profiles/       one .lua per disk flavour: a layer list + the
--                   init entry script.
--   compose.lua     resolves a profile's layers (globs + `requires`)
--                   and runs them into one hive + one file list.
--
-- Filesystem and disk-image format libraries (hive, mbr, fat16,
-- ntfs, drive) live under pkg/nt/fs/ — this orchestrator pulls
-- them through M.fs (= require('nt.fs')) and stays focused on
-- the build-driver job.

local M = {}

M.platform = require('ntosbe.platform')
M.fs       = require('nt.fs')

-- ----------------------------------------------------------------
-- build_image — builds SYSTEM hive + ESP boot disk for a given profile.
--
-- opts (table):
--   profile      = profile name (default "selfhost")
--   init         = { exe, args, stdio }  partial overrides ok
--   efi_binary   = absolute path to BOOTX64.EFI                (required)
--   output_dir   = where SYSTEM + esp.img land                 (required)
--   src_root     = absolute path to repo's src/ directory      (required)
--   size_mb      = ESP image size, default 64
--
-- Writes <output_dir>/SYSTEM (the hive — kept on disk for inspection)
-- and <output_dir>/esp.img (the boot image, with the same hive embedded
-- at \System32\config\SYSTEM).  Returns 0 on success.
-- ----------------------------------------------------------------

function M.build_image(opts)
    local platform = M.platform
    local now      = platform.now()

    -- Resolve the paths the profile asks for.
    local nt    = opts.src_root .. "/NT"
    local paths = {
        nt       = nt,
        sdk_lib  = nt .. "/PUBLIC/SDK/LIB/I386",
        cr_dir   = opts.src_root .. "/cr",
        pkg_root = opts.src_root .. "/pkg",
        obj      = function(comp)
                       return nt .. "/PRIVATE/" .. comp .. "/obj/i386"
                   end,
    }

    local fs = M.fs

    -- ---- Compose the disk from the profile's layers ----
    -- The profile names a layer set; compose resolves it (globs +
    -- `requires`) and runs each layer to yield the hive content and
    -- the disk file list.  Default "selfhost" = the full disk, so a
    -- bare `ntosbe` invocation behaves as it did pre-layers.
    local composed = require('ntosbe.compose').compose {
        profile   = opts.profile or "selfhost",
        init      = opts.init,
        paths     = paths,
        list_tree = platform.list_tree,
    }

    -- ---- Build hive ----
    local h = fs.hive.new("SYSTEM")
    composed.apply(h)
    local hive_bytes = h:build(now)

    -- ---- Resolve disk file list + check sources exist ----
    local files = composed.files
    -- BOOTX64.EFI lives on the ESP for UEFI to find it.  Tag
    -- where='esp' so the partitioning loop below routes it.
    table.insert(files, 1,
                 { dest = "EFI/BOOT/BOOTX64.EFI", where = 'esp',
                   src = opts.efi_binary })

    local missing = {}
    for _, f in ipairs(files) do
        if not platform.file_exists(f.src) then
            missing[#missing + 1] = f.src
        end
    end
    if #missing > 0 then
        platform.log("ERROR: required disk inputs are missing:")
        for _, m in ipairs(missing) do platform.log("  - " .. m) end
        platform.log("Run the appropriate build targets first.")
        return 1
    end

    -- Emit a manifest so the verify harness can check every file
    -- read back from the built disk against its source bytes.  Format:
    -- one tab-separated line per file: `<where>\t<dest>\t<src_path>`.
    -- The verify script (tools/verify_disk.sh) computes sha256 of
    -- both sides and reports any mismatch.

    -- ---- Build disk image ----
    -- Layout selection:
    --   'single'     — one FAT16 partition, type 0xEF, hosts everything.
    --                  ESP and \SystemRoot share the same volume; the
    --                  per-file `where` tag is ignored.
    --   'split-fat'  — partition 1: FAT16 ESP (type 0xEF, ~64 MB);
    --                  partition 2: FAT16 system (type 0x06, rest).
    --                  CANONICAL — most common layout.  Files routed
    --                  by `where`: 'esp' → ESP only, 'root' → system,
    --                  'both' → both partitions.
    --   'split-ntfs' — partition 1: FAT16 ESP (type 0xEF, ~64 MB);
    --                  partition 2: NTFS system (type 0x07, rest).
    --                  Same routing rules as split-fat; system volume
    --                  is NTFS so ntfs.sys mounts it.
    --
    -- Default 512 MiB total — enough for OS + toolchain + the staged
    -- NT source tree (~144 MB used today) with comfortable headroom.
    local layout = opts.layout or 'split-fat'
    if layout ~= 'single' and layout ~= 'split-fat'
       and layout ~= 'split-ntfs' then
        platform.die("Unknown layout '" .. tostring(layout)
                     .. "' (want 'single' | 'split-fat' | 'split-ntfs')")
    end
    local total_mb = opts.size_mb or 512
    local PRE_PARTITION_GAP_MB = 1
    local ESP_SIZE_MB          = 64

    local esp_path = opts.output_dir .. "/esp.img"
    local d = fs.drive.new {
        table     = 'mbr',
        signature = 0x4E544653,
    }

    if layout == 'single' then
        local vol = fs.fat16.new {
            size_mb      = total_mb - PRE_PARTITION_GAP_MB,
            volume_label = "NT",
            now          = now,
        }
        for _, f in ipairs(files) do
            local sz = platform.file_size(f.src) or 0
            local basename = f.src:match("([^/]+)$") or f.src
            platform.log(string.format("  all  %-40s %8d  (%s)",
                                       f.dest, sz, basename))
            vol:add_file(f.dest, f.src, platform)
        end
        vol:mkdir("tmp")
        vol:add_bytes("System32/config/SYSTEM", hive_bytes)
        d:add(vol, { active = true, type_code = 0xEF })
    else
        -- split-fat / split-ntfs: ESP + system, routed by `where`.
        local sys_size_mb = total_mb - PRE_PARTITION_GAP_MB - ESP_SIZE_MB
        local sys_min     = (layout == 'split-ntfs') and 8 or 4
        if sys_size_mb < sys_min then
            platform.die(string.format(
                "size_mb=%d too small for ESP=%d + system partition "
                .. "(need ≥%d for layout=%s)",
                total_mb, ESP_SIZE_MB,
                ESP_SIZE_MB + PRE_PARTITION_GAP_MB + sys_min, layout))
        end

        local esp_vol = fs.fat16.new {
            size_mb      = ESP_SIZE_MB,
            volume_label = "ESP",
            now          = now,
        }
        local sys_vol
        if layout == 'split-fat' then
            sys_vol = fs.fat16.new {
                size_mb      = sys_size_mb,
                volume_label = "NT",
                now          = now,
            }
        else  -- split-ntfs
            local bit = require('bit')
            sys_vol = fs.ntfs.new {
                size_mb       = sys_size_mb,
                volume_label  = "NT",
                volume_serial = bit.band(now or 0, 0xFFFFFFFF),
                now           = now,
            }
        end

        -- Route each entry by f.where:
        --   'esp'  → ESP only
        --   'root' → system only (default if .where omitted)
        --   'both' → both partitions
        for _, f in ipairs(files) do
            local sz = platform.file_size(f.src) or 0
            local basename = f.src:match("([^/]+)$") or f.src
            local where = f.where or 'root'
            if where ~= 'esp' and where ~= 'root' and where ~= 'both' then
                platform.die(string.format(
                    "disk file %s: invalid where=%q (want 'esp'|'root'|'both')",
                    f.dest, tostring(where)))
            end
            local tag
            if     where == 'esp'  then tag = "esp "
            elseif where == 'root' then tag = "sys "
            else                        tag = "both"
            end
            platform.log(string.format("  %s %-40s %8d  (%s)",
                                       tag, f.dest, sz, basename))
            if where == 'esp' or where == 'both' then
                esp_vol:add_file(f.dest, f.src, platform)
            end
            if where == 'root' or where == 'both' then
                sys_vol:add_file(f.dest, f.src, platform)
            end
        end
        sys_vol:mkdir("tmp")
        -- SYSTEM hive lives on the ESP for boot-efi pre-handoff.
        -- Runtime hive flushes target \SystemRoot\System32\config\SYSTEM
        -- (system partition); they fail silently today.
        esp_vol:add_bytes("System32/config/SYSTEM", hive_bytes)

        d:add(esp_vol, { active = true, type_code = 0xEF })
        d:add(sys_vol, {})  -- type from volume:build() (0x06 fat / 0x07 ntfs)
    end

    -- ---- Write outputs ----
    platform.mkdir_p(opts.output_dir)
    platform.write_file(opts.output_dir .. "/SYSTEM", hive_bytes)
    platform.log("SYSTEM hive: " .. #hive_bytes .. " bytes -> "
                 .. opts.output_dir .. "/SYSTEM")

    -- Emit the manifest after we know files were all routed.
    do
        local lines = {}
        for _, f in ipairs(files) do
            lines[#lines + 1] = string.format("%s\t%s\t%s",
                f.where or 'root', f.dest, f.src)
        end
        platform.write_file(opts.output_dir .. "/manifest.tsv",
                             table.concat(lines, "\n") .. "\n")
    end

    local stats = d:build(platform, esp_path)

    platform.log(string.format(
        "  Disk: %d MB  Signature: 0x%08X  Layout: %s",
        stats.size_mb, stats.signature, layout))
    for i, p in ipairs(stats.partitions) do
        local fs_kind
        if     p.type_code == 0xEF then fs_kind = "FAT16/ESP"
        elseif p.type_code == 0x06 then fs_kind = "FAT16/system"
        elseif p.type_code == 0x07 then fs_kind = "NTFS/system"
        else                            fs_kind = string.format("type-0x%02X",
                                                                p.type_code)
        end
        platform.log(string.format(
            "  Partition %d (%s): LBA %d..%d  type 0x%02X  (%d MB %s)",
            i, p.label, p.lba, p.lba + p.sectors - 1, p.type_code,
            math.floor(p.sectors * 512 / (1024 * 1024)), fs_kind))
        if p.stats.used_clusters then
            platform.log(string.format(
                "             clusters: %d used / %d total  (%d KB free)",
                p.stats.used_clusters, p.stats.total_clusters,
                p.stats.free_kb))
        elseif p.stats.clusters_used then
            platform.log(string.format(
                "             clusters: %d used / %d total  (cluster=%d B)",
                p.stats.clusters_used, p.stats.total_clusters,
                p.stats.cluster_size))
        end
    end
    platform.log(string.format(
        "  MBR checksum for ARC_DISK_SIGNATURE.CheckSum: 0x%08X",
        stats.mbr_checksum))
    platform.log("ESP image: " .. esp_path)

    return 0
end

-- ----------------------------------------------------------------
-- main — command-line entry.  Parses --flag=value / --flag value, then
-- dispatches to build_image.  Currently a single command (the disk
-- builder); subcommands (e.g. "ntosbe hive ..." for hive-only output)
-- can land later if a caller actually needs them.
-- ----------------------------------------------------------------

local function parse_argv(argv)
    local opts = {}
    local i = 1
    while i <= #argv do
        local a = argv[i]
        local key, value = a:match("^%-%-([^=]+)=(.*)$")
        if not key and a:sub(1, 2) == "--" then
            key   = a:sub(3)
            value = argv[i + 1]
            i = i + 1
        end
        if key then
            -- Normalise --init-args to opts.init_args etc.
            opts[(key:gsub("%-", "_"))] = value
        end
        i = i + 1
    end
    return opts
end

function M.main(argv)
    local platform = M.platform
    local opts     = parse_argv(argv or {})

    if not opts.efi_binary then platform.die("--efi-binary required") end
    if not opts.output_dir then platform.die("--output-dir required") end
    if not opts.src_root   then platform.die("--src-root required")   end

    return M.build_image {
        profile    = opts.profile,
        init = {
            exe   = opts.init_exe,
            args  = opts.init_args,
            stdio = opts.init_stdio,
        },
        efi_binary = opts.efi_binary,
        output_dir = opts.output_dir,
        src_root   = opts.src_root,
        size_mb    = tonumber(opts.size_mb) or 2000,
        layout     = opts.layout,
    }
end

return M
