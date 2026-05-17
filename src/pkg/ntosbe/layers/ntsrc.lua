-- ntosbe layer: ntsrc
--
-- The NT source tree, staged at \SystemRoot\src\NT\….  Self-host
-- enabler: the booted guest needs the SOURCES files, C/asm/inc inputs,
-- MAKEFILEs, and PUBLIC/{SDK,OAK}/{INC,LIB} on disk so ntosbe.build can
-- drive NMAKE.EXE in-process.
--
-- Files only — no drivers, no services.  Needed by the selfhost
-- profile; omitted by selftest (the source tree dominates disk-compose
-- time and the kernel + fuzz suites never touch \SystemRoot\src).

local M = {}

M.name = "ntsrc"
M.description = "NT source tree -> \\SystemRoot\\src\\NT (self-host inputs)"

-- Filtered staging — drops obj/ outputs, source tarballs, and build
-- sidecars (.dbg / .dwf / .pdb) that are produced by the build rather
-- than consumed by it.  Host-only top-level dirs (cr/, boot-efi/,
-- wibo-tools/, tools/, *.sh) are excluded by only walking paths.nt.
--
-- This layer is filesystem-agnostic.  It does NOT police FAT16's 8.3
-- name limit — that is the FAT16 writer's job: nt/fs/fat16.lua's
-- encode_83 hard-errors on a name that won't fit when the disk uses a
-- FAT layout.  On a split-ntfs disk (the selfhost default) long names
-- are fine and no check should fire at all.
function M.files(paths, list_tree)
    local nt_skip_dirs = {
        ["obj"] = true, ["Obj"] = true,
    }

    -- Pre-walk filter.  list_tree doesn't expose intermediate dir
    -- names, so we test path segments for obj/.  Also drops source
    -- tarballs (.tgz / .tar.gz / .zip / .bak) and build outputs that
    -- land in the tree (splitsym .dbg / dbg2dwf .dwf sidecars, pdbs).
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

    -- A directory holding both `SERLOG.h` (mc.exe output) and
    -- `serlog.h` (the lower-case copy ensure_serlog stages alongside
    -- it) is two source paths but one file on a case-insensitive
    -- volume.  Keep the lower-case form when both exist; the
    -- codegen-normalised name is the canonical one the include path
    -- looks for.
    local files = {}
    local seen = {}                 -- key = upper(rel) -> idx in files
    for _, rel in ipairs(list_tree(paths.nt)) do
        if not nt_path_excluded(rel) then
            local base     = rel:match("([^/]+)$") or rel
            local key      = rel:upper()
            local existing = seen[key]
            local entry = {
                dest = "src/NT/" .. rel,
                src  = paths.nt .. "/" .. rel,
            }
            if existing then
                -- Prefer the all-lowercase basename on a clash.
                if base == base:lower() then
                    files[existing] = entry
                end
            else
                files[#files + 1] = entry
                seen[key] = #files
            end
        end
    end

    return files
end

return M
