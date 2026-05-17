-- ntosbe.compose — profile -> disk composition.
--
-- A profile (src/pkg/ntosbe/profiles/<name>.lua) names a set of layers
-- plus the init entry; this module resolves that set — expanding
-- `group.*` globs and pulling in each layer's `requires` closure — then
-- runs every layer to produce the SYSTEM-hive content and the disk
-- file list.
--
-- compose() returns a composed object:
--   .apply(h)   runs every layer's registry(h, ctx) into the hive
--   .files      the merged disk file list ({dest,src,where} entries)
--   .init       the fully-resolved {exe,args,stdio} init table
--
-- Layer modules live under ntosbe/layers/; see layers/core.lua for the
-- module contract.

local M = {}

-- Expand one layer spec.  A spec is either a concrete layer name
-- ("storage.nvme") or a group glob ("storage.*"), which expands to
-- every layers/<group>/<stem>.lua as "<group>.<stem>".
local function expand_spec(spec, paths, list_tree)
    local group = spec:match("^(.+)%.%*$")
    if not group then return { spec } end
    local dir = paths.pkg_root .. "/ntosbe/layers/" .. group:gsub("%.", "/")
    local out = {}
    for _, rel in ipairs(list_tree(dir)) do
        local stem = rel:match("^([^/]+)%.lua$")
        if stem then out[#out + 1] = group .. "." .. stem end
    end
    table.sort(out)
    if #out == 0 then
        error("compose: layer glob '" .. spec .. "' matched nothing under "
              .. dir)
    end
    return out
end

-- Resolve a profile's layer list into ordered, deduped layer modules.
-- A layer's `requires` are added ahead of it (so e.g. storage.scsi
-- precedes storage.nvme); `core` is guaranteed present and first.
local function resolve_layers(profile, paths, list_tree)
    local order = {}        -- array of layer modules, load order
    local seen  = {}        -- layer name -> true

    local function add(name)
        if seen[name] then return end
        seen[name] = true
        local ok, layer = pcall(require, 'ntosbe.layers.' .. name)
        if not ok then
            error("compose: cannot load layer '" .. name .. "': "
                  .. tostring(layer))
        end
        for _, req in ipairs(layer.requires or {}) do
            add(req)
        end
        order[#order + 1] = layer
    end

    -- core is implicit — every disk needs the OS.
    add("core")
    for _, spec in ipairs(profile.layers or {}) do
        for _, name in ipairs(expand_spec(spec, paths, list_tree)) do
            add(name)
        end
    end
    return order
end

-- Resolve the init process config.  Precedence, low to high:
--   layer `init` defaults (core -> stdio, the runtime layer -> exe)
--   profile.init          (the entry script -> args)
--   opts_init             (--init-args / --init-exe / --init-stdio)
local function resolve_init(order, profile, opts_init)
    local init = {}
    for _, layer in ipairs(order) do
        if layer.init then
            for k, v in pairs(layer.init) do init[k] = v end
        end
    end
    if profile.init then
        for k, v in pairs(profile.init) do init[k] = v end
    end
    if opts_init then
        for k, v in pairs(opts_init) do init[k] = v end
    end
    if not init.exe then
        error("compose: no layer or profile supplies init.exe — a profile "
              .. "must include an application-runtime layer (e.g. 'lua')")
    end
    if not init.args then
        error("compose: profile supplies no init.args (entry script)")
    end
    return init
end

-- compose{ profile=<name>, init=<opts init>, paths=<paths>, list_tree=fn }
function M.compose(args)
    local profile = require('ntosbe.profiles.' .. args.profile)
    local paths, list_tree = args.paths, args.list_tree

    local order = resolve_layers(profile, paths, list_tree)
    local init  = resolve_init(order, profile, args.init)

    local composed = { init = init }

    function composed.apply(h)
        local ctx = { init = init, paths = paths }
        for _, layer in ipairs(order) do
            if layer.registry then layer.registry(h, ctx) end
        end
    end

    -- Merge every layer's plain files, then its boot drivers.
    local files = {}
    for _, layer in ipairs(order) do
        if layer.files then
            for _, e in ipairs(layer.files(paths, list_tree)) do
                files[#files + 1] = e
            end
        end
    end
    for _, layer in ipairs(order) do
        if layer.boot_drivers then
            for _, bd in ipairs(layer.boot_drivers(paths)) do
                -- Boot drivers are staged under \Boot\<NN>\ on the ESP,
                -- where boot-efi's directory walk discovers them.  The
                -- 2-digit zero-padded bucket carries load order
                -- (boot-efi sorts the bucket dirs lexically); the
                -- bucket directory itself is invisible to the kernel,
                -- which sees only the bare driver filename.
                files[#files + 1] = {
                    dest  = string.format("Boot/%02d/%s",
                                          bd.bucket, bd.name),
                    where = 'esp',
                    src   = bd.src,
                }
            end
        end
    end
    composed.files = files

    return composed
end

return M
