-- nt.tree — unified namespace traversal.

local t    = require('test')
local tree = require('nt.tree')

t.suite("tree")

t.test("root() returns a Directory node at \\", function()
    local root = tree.root()
    t.eq(root.path, "\\")
    t.eq(root.type_name, "Directory")
end)

t.test("root iter yields known entries (Registry, Device, ObjectTypes)", function()
    local seen = {}
    for c in tree.root():iter() do
        seen[c.name:lower()] = c.type_name
    end
    t.ne(seen["registry"], nil, "Registry present under root")
    t.eq(seen["device"],    "Directory")
    t.eq(seen["objecttypes"], "Directory")
end)

t.test("resolve is case-insensitive", function()
    local node = tree.resolve("\\REGISTRY\\MACHINE\\System")
    t.eq(node.type_name, "Key")
    local node2 = tree.resolve("\\registry\\machine\\system")
    t.eq(node2.type_name, "Key")
end)

t.test("resolve on missing path raises", function()
    t.raises(function()
        tree.resolve("\\Registry\\Machine\\NoSuchThing")
    end, "no such path")
end)

t.test("resolve(\\) returns root", function()
    local r1 = tree.resolve("\\")
    local r2 = tree.root()
    t.eq(r1.path, r2.path)
    t.eq(r1.type_name, r2.type_name)
end)

t.test("SymbolicLink exposes .target", function()
    local sr = tree.resolve("\\SystemRoot")
    t.eq(sr.type_name, "SymbolicLink")
    t.ne(sr.target, nil)
    t.ok(sr.target:match("^\\Device\\Harddisk"), "target is a partition path")
end)

t.test("Processes virtual appears at root", function()
    local found = false
    for c in tree.root():iter() do
        if c.name == "Processes" and c.type_name == "ProcessList" then
            found = true
        end
    end
    t.ok(found, "\\Processes is grafted onto root")
end)

t.test("System virtual appears at root", function()
    local found = false
    for c in tree.root():iter() do
        if c.name == "System" and c.type_name == "SystemDir" then
            found = true
        end
    end
    t.ok(found, "\\System is grafted onto root")
end)

t.test("\\System\\Modules yields Module nodes", function()
    local count = 0
    for m in tree.resolve("\\System\\Modules"):iter() do
        count = count + 1
        t.eq(m.type_name, "Module")
        t.ne(m.image_path, nil)
    end
    t.ok(count > 0, "at least one loaded kernel module")
end)

t.test("Node:completions() returns {fields, methods}", function()
    local sr = tree.resolve("\\SystemRoot")
    local comp = sr:completions()
    t.ok(#comp > 0, "got some completions")
    local kinds = {}
    for _, c in ipairs(comp) do kinds[c.kind] = true end
    t.ok(kinds.field,  "has fields")
    t.ok(kinds.method, "has methods")
end)
