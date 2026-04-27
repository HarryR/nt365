-- generr.lua — Lua port of tools/generr.py.
--
-- Reimplements the NT generr.exe tool that emits the NTSTATUS→Win32
-- error-code translation tables (RtlpStatusTable, RtlpRunTable) used
-- by RtlNtStatusToDosError.  Inputs:
--   - PUBLIC/SDK/INC/NTSTATUS.H   (status code names → ULONG values)
--   - PUBLIC/SDK/INC/WINERROR.H   (Win32 error names → DWORD values)
--   - PRIVATE/NTOS/RTL/GENERR.C   (the symbolic CodePairs[] array)
-- Output:
--   - PRIVATE/NTOS/RTL/error.h    (run-length-encoded translation tables)
--
-- Returned as a module so build.lua can `dofile` it and call run().

local M = {}

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local data = f:read("*all")
    f:close()
    return data
end

local function file_exists(path)
    local f = io.open(path, "rb")
    if f then f:close(); return true end
    return false
end

-- ------------------------------------------------------------------
-- Header parsing.  Patterns ported from the Python regex set.
-- ------------------------------------------------------------------

local function load_status_codes(ntstatus_h)
    -- Match: #define STATUS_xxx ((NTSTATUS)0xNNNNNNNN[L])
    local vals = {}
    if not file_exists(ntstatus_h) then return vals end
    for line in io.lines(ntstatus_h) do
        local stripped = line:gsub("^%s+", ""):gsub("%s+$", "")
        local name, hex = stripped:match(
            "^#define%s+([%w_]+)%s+%(%(NTSTATUS%)0x(%x+)L?%)")
        if name and hex then
            vals[name] = tonumber(hex, 16)
        end
    end
    return vals
end

local function is_winerror_name(name)
    -- The Python regex used a name-prefix alternation; Lua patterns
    -- don't have alternation, so check each prefix explicitly.
    return name:match("^ERROR_") or name == "NO_ERROR"
        or name == "WAIT_TIMEOUT"
        or name:match("^EPT_S_") or name:match("^RPC_S_")
        or name:match("^RPC_X_") or name:match("^OR_")
end

local function load_error_codes(winerror_h)
    -- Match: #define ERROR_xxx N[L]    (decimal Win32 codes)
    local vals = {}
    if not file_exists(winerror_h) then return vals end
    for line in io.lines(winerror_h) do
        local stripped = line:gsub("^%s+", ""):gsub("%s+$", "")
        local name, dec = stripped:match("^#define%s+([%w_]+)%s+(%d+)L?")
        if name and dec and is_winerror_name(name) then
            vals[name] = tonumber(dec)
        end
    end
    -- Constants the Python tool hardcodes; mirror them so symbolic
    -- references in GENERR.C resolve.
    vals["NO_ERROR"]              = 0
    vals["ERROR_SUCCESS"]         = 0
    vals["ERROR_MR_MID_NOT_FOUND"] = 317
    return vals
end

-- ------------------------------------------------------------------
-- GENERR.C parsing — extract the CodePairs[] body and resolve every
-- token (symbol or literal) to an integer.
-- ------------------------------------------------------------------

local function extract_code_pairs(generr_c, status_vals, error_vals)
    local src = read_file(generr_c)
    if not src then
        error("ERROR: cannot read " .. generr_c)
    end

    -- Locate the `CodePairs[] = { ... };` body.  Lua's `.-` is the
    -- non-greedy any-char (matches newlines).
    local body = src:match("CodePairs%[%]%s*=%s*%{(.-)%}%s*;")
    if not body then
        error("Could not find CodePairs array in GENERR.C")
    end

    -- Strip comments and preprocessor lines.
    body = body:gsub("//[^\n]*", "")
    body = body:gsub("/%*.-%*/", "")
    body = body:gsub("#[^\n]*", "")

    -- Build a combined symbol→int map.  Prefer status_vals for any
    -- collision (matches Python's `{**status_vals, **error_vals}`).
    local all_vals = {}
    for k, v in pairs(error_vals)  do all_vals[k] = v end
    for k, v in pairs(status_vals) do all_vals[k] = v end

    -- Tokenize: identifiers OR numeric literals.  `[%w_]+` would also
    -- swallow hex-prefix tokens like "0x1234" wholesale (since `x` is
    -- a word character), so we can keep it simple with a single
    -- character class and disambiguate by inspecting the token.
    local pairs_out = {}
    for tok in body:gmatch("[%w_]+") do
        if tok:match("^0[xX]") then
            pairs_out[#pairs_out + 1] = tonumber(tok)
        elseif tok:match("^%d+$") then
            pairs_out[#pairs_out + 1] = tonumber(tok)
        elseif all_vals[tok] then
            pairs_out[#pairs_out + 1] = all_vals[tok]
        else
            io.stderr:write(("WARNING: Unknown symbol '%s', using 0\n"):format(tok))
            pairs_out[#pairs_out + 1] = 0
        end
    end

    if (#pairs_out % 2) ~= 0 then
        io.stderr:write(("WARNING: Odd number of entries (%d), dropping last\n"):format(#pairs_out))
        pairs_out[#pairs_out] = nil
    end

    return pairs_out
end

-- ------------------------------------------------------------------
-- Run-length compression and code-size selection.
--
-- `start` is a 1-based Lua index pointing at the *status* element of
-- a (status, error) pair.  pair[k]   = pairs_in[k];   the error half
-- is pairs_in[k+1].  Consecutive runs are pairs whose status values
-- are sequentially +1 from the previous status.
-- ------------------------------------------------------------------

local function compute_run_length(pairs_in, start)
    local length = 1
    local k      = start
    while (k + 2) + 1 <= #pairs_in do
        if pairs_in[k + 2] ~= pairs_in[k] + 1 then break end
        k      = k + 2
        length = length + 1
    end
    return length
end

local function compute_code_size(pairs_in, start, run_length)
    for i = 0, run_length - 1 do
        local dos_err = pairs_in[start + i * 2 + 1]
        if dos_err > 0xFFFF then return 2 end
    end
    return 1
end

-- ------------------------------------------------------------------
-- Emit error.h.
-- ------------------------------------------------------------------

local function generate_error_h(pairs_in, out_path)
    -- Sort by NT status code (unsigned).  Lua's numeric values for the
    -- 0xC0000000-range constants are stored as positive doubles by
    -- LuaJIT, so a plain `<` comparator is unsigned.
    local pair_list = {}
    for i = 1, #pairs_in, 2 do
        pair_list[#pair_list + 1] = { pairs_in[i], pairs_in[i + 1] }
    end
    table.sort(pair_list, function(a, b) return a[1] < b[1] end)

    -- Flatten back into a 1-based linear array.
    local pairs_sorted = {}
    for _, p in ipairs(pair_list) do
        pairs_sorted[#pairs_sorted + 1] = p[1]
        pairs_sorted[#pairs_sorted + 1] = p[2]
    end

    local f = assert(io.open(out_path, "w"))

    f:write("//\n")
    f:write("// Define run length table entry structure type.\n")
    f:write("//\n")
    f:write("\n")
    f:write("typedef struct _RUN_ENTRY {\n")
    f:write("    ULONG BaseCode;\n")
    f:write("    USHORT RunLength;\n")
    f:write("    USHORT CodeSize;\n")
    f:write("} RUN_ENTRY, *PRUN_ENTRY;\n")
    f:write("\n")
    f:write("//\n")
    f:write("// Declare translation table array.\n")
    f:write("//\n")
    f:write("\n")
    f:write("USHORT RtlpStatusTable[] = {")
    f:write("\n    ")

    local run_table = {}
    local count     = 0
    local k         = 1                            -- 1-based status index
    while k <= #pairs_sorted do
        local length = compute_run_length(pairs_sorted, k)
        local size   = compute_code_size(pairs_sorted, k, length)
        run_table[#run_table + 1] = {
            base_code  = pairs_sorted[k],
            run_length = length,
            code_size  = size,
        }

        for j = 0, length - 1 do
            local dos_err = pairs_sorted[k + j * 2 + 1]
            if size == 1 then
                count = count + 1
                f:write(("0x%04x, "):format(dos_err % 0x10000))
            else
                count = count + 2
                f:write(("0x%04x, 0x%04x, "):format(
                    dos_err % 0x10000,
                    math.floor(dos_err / 0x10000) % 0x10000))
            end

            if count > 6 then
                count = 0
                f:write("\n    ")
            end
        end

        k = k + length * 2
    end

    f:write("0x0};\n")
    f:write("\n")
    f:write("//\n")
    f:write("// Declare run length table array.\n")
    f:write("//\n")
    f:write("\n")
    f:write("RUN_ENTRY RtlpRunTable[] = {\n")

    for _, e in ipairs(run_table) do
        f:write(("    {0x%08x, %d, %d},\n"):format(
            e.base_code, e.run_length, e.code_size))
    end

    f:write("    {0x0, 0x0, 0x0}};\n")
    f:close()
end

-- ------------------------------------------------------------------
-- Public entry point.
--
--   generr.run(nt_root, out_path)
--
-- Reads NTSTATUS.H, WINERROR.H, GENERR.C from the standard NT-tree
-- locations; writes error.h to out_path.  Both args are required (no
-- argv-based default — build.lua always knows both).
-- ------------------------------------------------------------------

function M.run(nt_root, out_path)
    local ntstatus_h = nt_root .. "/PUBLIC/SDK/INC/NTSTATUS.H"
    local winerror_h = nt_root .. "/PUBLIC/SDK/INC/WINERROR.H"
    local generr_c   = nt_root .. "/PRIVATE/NTOS/RTL/GENERR.C"

    io.stderr:write("GENERR: Loading status codes...\n")
    local status_vals = load_status_codes(ntstatus_h)
    local n_status = 0; for _ in pairs(status_vals) do n_status = n_status + 1 end
    io.stderr:write(("GENERR: %d NTSTATUS codes\n"):format(n_status))

    local error_vals = load_error_codes(winerror_h)
    local n_error = 0; for _ in pairs(error_vals) do n_error = n_error + 1 end
    io.stderr:write(("GENERR: %d error codes\n"):format(n_error))

    io.stderr:write(("GENERR: Extracting code pairs from %s...\n"):format(generr_c))
    local pairs_out = extract_code_pairs(generr_c, status_vals, error_vals)
    io.stderr:write(("GENERR: %d code pairs\n"):format(math.floor(#pairs_out / 2)))

    io.stderr:write(("GENERR: Writing %s...\n"):format(out_path))
    generate_error_h(pairs_out, out_path)
    io.stderr:write("GENERR: Done.\n")
end

return M
