-- Module — synthetic Node under \System\Modules. Pure leaf (no
-- children, no open — modules aren't openable by name from user mode).
-- Fields forward to the __mod snapshot table filled by modlist.lua.

local function from_mod(key)
    return function(n) return n.__mod[key] end
end

local M = {}

M.fields = {
    image_path  = from_mod("image_path"),
    image_base  = from_mod("image_base"),
    mapped_base = from_mod("mapped_base"),
    image_size  = from_mod("image_size"),
    flags       = from_mod("flags"),
    load_order  = from_mod("load_order"),
    init_order  = from_mod("init_order"),
    load_count  = from_mod("load_count"),
}

M.descriptions = {
    image_path  = "Full NT path of the module image (e.g. \\SystemRoot\\System32\\ntoskrnl.exe).",
    image_base  = "Base virtual address of the loaded image.",
    mapped_base = "Address the module's sections are mapped at (may equal image_base).",
    image_size  = "Module size in bytes.",
    flags       = "Loader flags bitmask.",
    load_order  = "Index in the kernel's load-order list.",
    init_order  = "Index in the kernel's init-order list.",
    load_count  = "Reference count (drivers can be loaded/unloaded multiple times).",
}

return M
