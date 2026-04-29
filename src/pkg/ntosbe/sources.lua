-- ntosbe.sources — SOURCES-file parser and object-staleness detection.
--
-- BUILD.EXE normally produces obj/_objects.mac at build time; we do it
-- ourselves so the build has zero dependency on the original BUILD.EXE
-- and runs identically on host and inside MicroNT.  All I/O routes
-- through ntosbe.platform, so this module is platform-neutral.
--
-- Surface:
--
--   gen_objects(comp_dir)           Parse comp_dir/SOURCES (and any sibling
--                                   I386/SOURCES), expand !include directives,
--                                   classify each source's output (.obj / .res),
--                                   and write comp_dir/obj/_objects.mac.
--                                   Returns true on success, false on error.
--
--   nuke_stale_objs(linux_dir)      Two-pass staleness scan:
--                                     pass 1: per-source mtime > matching .obj
--                                             → unlink the .obj
--                                     pass 2: any .h/.inc newer than oldest .obj
--                                             → nuke every .obj in the dir
--                                   No return value; reports via platform.log.
--
-- Pure path utilities used internally are also exported (basename,
-- dirname, stem, normpath, trim, split_lines, resolve_ci, find_iname)
-- so build.lua and the future toolchain bridge can share them.
--
-- The path helpers are pure-Lua string ops with no I/O assumptions, so
-- they work unchanged on the in-OS port.

local platform = require('ntosbe.platform')

local M = {}

-- ----------------------------------------------------------------
-- Pure path / string utilities.
-- ----------------------------------------------------------------

local function basename(path)
    return path:match("([^/]+)$") or path
end

local function stem(path)
    local b = basename(path)
    return b:gsub("%.[^.]*$", "")
end

local function dirname(path)
    local d = path:match("(.*)/[^/]*$")
    return d or "."
end

local function normpath(p)
    -- Collapse 'X/..' and '.' segments.  Doesn't follow symlinks — the
    -- only paths gen_objects sees are NT-source-tree paths with no
    -- symlinks in scope.
    local parts = {}
    for part in p:gmatch("[^/]+") do
        if part == ".." and #parts > 0 and parts[#parts] ~= ".." then
            parts[#parts] = nil
        elseif part ~= "." then
            parts[#parts + 1] = part
        end
    end
    local r = table.concat(parts, "/")
    if p:sub(1, 1) == "/" then r = "/" .. r end
    return r
end

local function trim(s)
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function split_lines(s)
    -- Split on \n / \r\n / \r.  Returns array of lines without trailing
    -- newline.  Does not produce a final empty element if input ends
    -- with a newline.
    local lines = {}
    for line in (s .. "\n"):gmatch("([^\r\n]*)\r?\n") do
        lines[#lines + 1] = line
    end
    if lines[#lines] == "" then lines[#lines] = nil end
    return lines
end

M.basename    = basename
M.stem        = stem
M.dirname     = dirname
M.normpath    = normpath
M.trim        = trim
M.split_lines = split_lines

-- ----------------------------------------------------------------
-- Filesystem-backed helpers.
-- ----------------------------------------------------------------

-- Case-insensitive single-component resolution: if `path` exists
-- exactly, return it; otherwise scan its parent directory for a name
-- matching the last component case-insensitively.  Used to honour the
-- mixed-case naming conventions in the original NT 3.5 source tree
-- (SOURCES vs sources, I386 vs i386, etc.) on a case-sensitive FS.
local function resolve_ci(path)
    if platform.file_exists(path) then return path end
    local parent = dirname(path)
    if not platform.file_exists(parent) then return nil end
    local target = basename(path):lower()
    for _, name in ipairs(platform.list_dir(parent)) do
        if name:lower() == target then
            return parent .. "/" .. name
        end
    end
    return nil
end

-- Case-insensitive single-level glob match.  Equivalent to
-- `find $dir -maxdepth 1 -iname $glob`; returns first match or nil.
-- Glob supports `*` (zero-or-more) and literal characters.
local function find_iname(dir, glob)
    local pat = "^" .. glob:gsub("[%-%.]", "%%%1"):gsub("%*", ".*") .. "$"
    local lower_pat = pat:lower()
    for _, name in ipairs(platform.list_dir(dir)) do
        if name:lower():match(lower_pat) then
            return dir .. "/" .. name
        end
    end
    return nil
end

M.resolve_ci = resolve_ci
M.find_iname = find_iname

-- ----------------------------------------------------------------
-- SOURCES file parsing.
-- ----------------------------------------------------------------

-- Read a SOURCES file, inlining any `!include ..\\foo.inc` directives.
-- Returns the logically-flattened list of lines.  Missing includes are
-- silently skipped (matches nmake's `!include if exist` semantics).
local function flatten_sources(path, seen)
    seen = seen or {}
    path = normpath(path)
    if seen[path] then return {} end
    seen[path] = true

    local raw = platform.read_file(path)
    if not raw then return {} end

    local out = {}
    for _, line in ipairs(split_lines(raw)) do
        local inc = line:match("^!%s*[Ii][Nn][Cc][Ll][Uu][Dd][Ee]%s+(.+)")
        if inc then
            inc = trim(inc):gsub('^"', ""):gsub('"$', ""):gsub("\\", "/")
            local inc_path = dirname(path) .. "/" .. inc
            local resolved = resolve_ci(inc_path)
            if resolved then
                for _, l in ipairs(flatten_sources(resolved, seen)) do
                    out[#out + 1] = l
                end
            end
        else
            out[#out + 1] = line
        end
    end
    return out
end

-- Pull every `VAR= ...` definition out of `lines` (honouring backslash
-- continuations) and return the concatenated tokens.  Strips
-- `$(VAR)` self-references.  Case-insensitive on varname (real
-- SOURCES files mix `i386_SOURCES=` and `I386_SOURCES=`).
local function extract_var(lines, varname)
    local tokens = {}
    local in_var = false
    local var_lower = varname:lower()
    local function match_var_assign(line)
        local key, body = line:match("^([%w_]+)%s*=%s*(.*)$")
        if not key or key:lower() ~= var_lower then return nil end
        return body
    end
    local self_ref_pat = "^%$%(" .. varname:gsub("(.)", function(c)
        if c:match("[%a_]") then
            return "[" .. c:lower() .. c:upper() .. "]"
        else
            return c:gsub("([%-%.%(%)%[%]%+%*%?%^%$%%])", "%%%1")
        end
    end) .. "%)%s*"

    for _, line in ipairs(lines) do
        local body
        if not in_var then
            body = match_var_assign(line)
            if not body then goto continue end
            body = body:gsub(self_ref_pat, "")
            in_var = true
        else
            body = line
        end

        local rstripped = body:gsub("%s+$", "")
        local cont = rstripped:sub(-1) == "\\"
        if cont then rstripped = rstripped:sub(1, -2) end
        for tok in rstripped:gmatch("%S+") do tokens[#tokens + 1] = tok end
        if not cont then in_var = false end

        ::continue::
    end
    return tokens
end

M.flatten_sources = flatten_sources
M.extract_var     = extract_var

-- ----------------------------------------------------------------
-- Source → obj name mapping.
-- ----------------------------------------------------------------

-- '..\\i386\\foo.c' → 'obj\\i386\\foo.obj'.
-- '.rc' → '.res' (RC inference rule produces .res).
-- '.res' / '.mc' → nil (pre-built / message-compiler input).
local function src_to_obj(src)
    local base = basename(src:gsub("\\", "/"))
    local stm, ext = base:match("^(.+)%.([^.]+)$")
    if not stm then return nil end
    ext = ext:lower()
    if ext == "res" or ext == "mc" then return nil end
    if ext == "rc" then return "obj\\i386\\" .. stm .. ".res" end
    return "obj\\i386\\" .. stm .. ".obj"
end

-- Look for arch-specific SOURCES at known spots, case-insensitively.
local function find_i386_sources_file(comp_dir)
    local candidates = {
        comp_dir .. "/I386/SOURCES",
        dirname(comp_dir) .. "/I386/SOURCES",
        comp_dir .. "/i386/SOURCES",
        dirname(comp_dir) .. "/i386/SOURCES",
    }
    for _, c in ipairs(candidates) do
        local r = resolve_ci(c)
        if r then return r end
    end
    return nil
end

M.src_to_obj             = src_to_obj
M.find_i386_sources_file = find_i386_sources_file

-- ----------------------------------------------------------------
-- gen_objects — write comp_dir/obj/_objects.mac from comp_dir/SOURCES
-- (and any sibling i386/SOURCES).  Returns true on success.
-- ----------------------------------------------------------------

function M.gen_objects(comp_dir)
    local sources = resolve_ci(comp_dir .. "/SOURCES")
    if not sources then
        platform.log("ERROR: SOURCES not found: " .. comp_dir .. "/SOURCES")
        return false
    end

    local lines = flatten_sources(sources)
    local srcs  = extract_var(lines, "SOURCES")

    -- i386_SOURCES: either in a sibling/arch SOURCES file, or inline.
    local i386_file = find_i386_sources_file(comp_dir)
    local i386_srcs
    if i386_file then
        local i386_lines = flatten_sources(i386_file)
        i386_srcs = extract_var(i386_lines, "SOURCES")
        for _, t in ipairs(extract_var(i386_lines, "i386_SOURCES")) do
            i386_srcs[#i386_srcs + 1] = t
        end
    else
        i386_srcs = extract_var(lines, "i386_SOURCES")
    end

    local all_srcs = {}
    for _, t in ipairs(srcs)      do all_srcs[#all_srcs + 1] = t end
    for _, t in ipairs(i386_srcs) do all_srcs[#all_srcs + 1] = t end

    platform.mkdir_p(comp_dir .. "/obj/i386")

    local objs = {}
    for _, s in ipairs(all_srcs) do
        local o = src_to_obj(s)
        if o then objs[#objs + 1] = o end
    end

    local out_path = comp_dir .. "/obj/_objects.mac"
    local body = { "#\n# _objects.mac - generated by build.lua gen_objects\n#\n\n" }
    if #objs > 0 then
        body[#body + 1] = "386_OBJECTS=" .. objs[1]
        for i = 2, #objs do
            body[#body + 1] = " \\\n    " .. objs[i]
        end
        body[#body + 1] = "\n"
    else
        body[#body + 1] = "386_OBJECTS=\n"
    end
    body[#body + 1] = "\n"
    platform.write_file(out_path, table.concat(body))

    platform.log(("Generated %s with %d source files (%d objects)"):format(
        out_path, #all_srcs, #objs))
    return true
end

-- ----------------------------------------------------------------
-- nuke_stale_objs — drop .obj files whose source has been edited, or
-- nuke the whole obj/i386 directory if any header has been touched.
-- ----------------------------------------------------------------

local SRC_EXTS    = { "c", "cxx", "cpp", "asm" }
local HEADER_EXTS = { "h", "hxx", "hpp", "inc" }

local function ext_in(path, exts)
    local e = path:match("%.([^.]+)$")
    if not e then return false end
    e = e:lower()
    for _, x in ipairs(exts) do if e == x then return true end end
    return false
end

function M.nuke_stale_objs(linux_dir)
    local obj_dir  = linux_dir .. "/obj/i386"
    local src_dirs = { linux_dir, linux_dir .. "/..", linux_dir .. "/i386" }

    -- Pass 1: stale .obj per source file.
    for _, d in ipairs(src_dirs) do
        if platform.file_exists(d) then
            for _, name in ipairs(platform.list_dir(d)) do
                if ext_in(name, SRC_EXTS) then
                    local src      = d .. "/" .. name
                    local obj_stem = stem(name)
                    local obj      = find_iname(obj_dir, obj_stem .. ".obj")
                    if obj then
                        local sm, om = platform.mtime(src), platform.mtime(obj)
                        if sm and om and sm > om then
                            platform.log(("  stale: %s (newer than %s)"):format(
                                name, basename(obj)))
                            platform.unlink(obj)
                        end
                    end
                end
            end
        end
    end

    -- Pass 2: header edits invalidate every .obj.
    if platform.file_exists(obj_dir) then
        local objs, oldest_obj, oldest_m = {}, nil, math.huge
        for _, name in ipairs(platform.list_dir(obj_dir)) do
            if name:lower():match("%.obj$") then
                local p = obj_dir .. "/" .. name
                local m = platform.mtime(p)
                if m then
                    objs[#objs + 1] = p
                    if m < oldest_m then oldest_m, oldest_obj = m, p end
                end
            end
        end
        if oldest_obj then
            for _, d in ipairs(src_dirs) do
                if platform.file_exists(d) then
                    for _, name in ipairs(platform.list_dir(d)) do
                        if ext_in(name, HEADER_EXTS) then
                            local src = d .. "/" .. name
                            local sm  = platform.mtime(src)
                            if sm and sm > oldest_m then
                                platform.log(("  header changed: %s (newer than %s) — nuking %s/*.obj"):format(
                                    name, basename(oldest_obj), obj_dir))
                                for _, o in ipairs(objs) do platform.unlink(o) end
                                return
                            end
                        end
                    end
                end
            end
        end
    end
end

return M
