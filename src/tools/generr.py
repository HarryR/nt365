#!/usr/bin/env python3
"""
generr.py - Generate error.h from GENERR.C
Reimplements the NT generr.exe tool that generates the NTSTATUS->DOS error
code translation tables used by RtlNtStatusToDosError.
"""

import re
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
NT_ROOT = os.path.join(SCRIPT_DIR, '..', 'NT')


def parse_defines(filepath, pattern):
    """Extract #define name value pairs matching a regex pattern."""
    vals = {}
    if not os.path.exists(filepath):
        return vals
    with open(filepath, 'r', errors='replace') as f:
        for line in f:
            m = re.match(pattern, line.strip())
            if m:
                name = m.group(1)
                val_str = m.group(2)
                try:
                    vals[name] = int(val_str, 0)
                except ValueError:
                    pass
    return vals


def load_status_codes():
    """Load NTSTATUS values from ntstatus.h"""
    vals = {}
    for p in [
        os.path.join(NT_ROOT, 'PUBLIC/SDK/INC/NTSTATUS.H'),
    ]:
        with open(p, 'r', errors='replace') as f:
            for line in f:
                # #define STATUS_xxx  ((NTSTATUS)0xNNNNNNNNL)
                m = re.match(
                    r'#define\s+(\w+)\s+\(\(NTSTATUS\)0x([0-9a-fA-F]+)L?\)',
                    line.strip())
                if m:
                    vals[m.group(1)] = int(m.group(2), 16)
    return vals


def load_error_codes():
    """Load Win32 error codes from winerror.h"""
    vals = {}
    for p in [
        os.path.join(NT_ROOT, 'PUBLIC/SDK/INC/WINERROR.H'),
    ]:
        if not os.path.exists(p):
            continue
        with open(p, 'r', errors='replace') as f:
            for line in f:
                m = re.match(
                    r'#define\s+(ERROR_\w+|NO_ERROR|WAIT_TIMEOUT|EPT_S_\w+|RPC_S_\w+|RPC_X_\w+|OR_\w+)\s+(\d+)L?\s*(?://.*)?$',
                    line.strip())
                if m:
                    vals[m.group(1)] = int(m.group(2))
    vals['NO_ERROR'] = 0
    vals['ERROR_SUCCESS'] = 0
    vals['ERROR_MR_MID_NOT_FOUND'] = 317
    return vals


def extract_code_pairs(generr_path, status_vals, error_vals):
    """Extract the CodePairs array from GENERR.C and resolve to integer values."""
    with open(generr_path, 'r') as f:
        src = f.read()

    # Find the CodePairs array
    m = re.search(r'CodePairs\[\]\s*=\s*\{(.*?)\}\s*;', src, re.DOTALL)
    if not m:
        raise RuntimeError("Could not find CodePairs array in GENERR.C")

    body = m.group(1)
    # Remove comments
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.DOTALL)
    # Remove preprocessor lines
    body = re.sub(r'#[^\n]*', '', body)

    # Extract tokens (symbolic names or hex literals)
    tokens = re.findall(r'[A-Za-z_]\w+|0x[0-9a-fA-F]+|\d+', body)

    # Resolve to integers
    all_vals = {**status_vals, **error_vals}
    pairs = []
    for tok in tokens:
        if tok.startswith('0x') or tok.startswith('0X'):
            pairs.append(int(tok, 16))
        elif tok.isdigit():
            pairs.append(int(tok))
        elif tok in all_vals:
            pairs.append(all_vals[tok])
        else:
            print(f"WARNING: Unknown symbol '{tok}', using 0", file=sys.stderr)
            pairs.append(0)

    # Pairs should be even (status, error, status, error, ...)
    if len(pairs) % 2 != 0:
        print(f"WARNING: Odd number of entries ({len(pairs)}), dropping last",
              file=sys.stderr)
        pairs = pairs[:-1]

    return pairs


def compute_run_length(pairs, start):
    """Compute run length starting at index (step 2)."""
    length = 1
    idx = start
    while (idx + 2) < len(pairs):
        if pairs[idx + 2] != pairs[idx] + 1:
            break
        idx += 2
        length += 1
    return length


def compute_code_size(pairs, start, run_length):
    """Compute code size (1 if all DOS codes fit in 16 bits, else 2)."""
    for i in range(run_length):
        if pairs[start + i * 2 + 1] > 0xFFFF:
            return 2
    return 1


def generate_error_h(pairs, outfile):
    """Generate error.h from sorted code pairs."""
    # Sort by NT status code (first element of each pair)
    pair_list = [(pairs[i], pairs[i + 1]) for i in range(0, len(pairs), 2)]
    # Use unsigned comparison (treat as uint32)
    pair_list.sort(key=lambda x: x[0] & 0xFFFFFFFF)
    # Flatten back
    pairs = []
    for s, e in pair_list:
        pairs.append(s)
        pairs.append(e)

    f = outfile
    f.write("//\n")
    f.write("// Define run length table entry structure type.\n")
    f.write("//\n")
    f.write("\n")
    f.write("typedef struct _RUN_ENTRY {\n")
    f.write("    ULONG BaseCode;\n")
    f.write("    USHORT RunLength;\n")
    f.write("    USHORT CodeSize;\n")
    f.write("} RUN_ENTRY, *PRUN_ENTRY;\n")
    f.write("\n")
    f.write("//\n")
    f.write("// Declare translation table array.\n")
    f.write("//\n")
    f.write("\n")
    f.write("USHORT RtlpStatusTable[] = {")
    f.write("\n    ")

    # Build run table and status table
    run_table = []
    count = 0
    idx = 0
    while idx < len(pairs):
        length = compute_run_length(pairs, idx)
        size = compute_code_size(pairs, idx, length)
        run_table.append((pairs[idx] & 0xFFFFFFFF, length, size))

        for j in range(length):
            dos_err = pairs[idx + j * 2 + 1]
            if size == 1:
                count += 1
                f.write("0x%04x, " % (dos_err & 0xFFFF))
            else:
                count += 2
                f.write("0x%04x, 0x%04x, " % (dos_err & 0xFFFF, (dos_err >> 16) & 0xFFFF))

            if count > 6:
                count = 0
                f.write("\n    ")

        idx += length * 2

    f.write("0x0};\n")

    f.write("\n")
    f.write("//\n")
    f.write("// Declare run length table array.\n")
    f.write("//\n")
    f.write("\n")
    f.write("RUN_ENTRY RtlpRunTable[] = {\n")

    for base_code, run_length, code_size in run_table:
        f.write("    {0x%08x, %d, %d},\n" % (base_code, run_length, code_size))

    f.write("    {0x0, 0x0, 0x0}};\n")


def main():
    generr_c = os.path.join(NT_ROOT, 'PRIVATE/NTOS/RTL/GENERR.C')
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    else:
        output_path = os.path.join(NT_ROOT, 'PRIVATE/NTOS/RTL/error.h')

    print(f"GENERR: Loading status codes...", file=sys.stderr)
    status_vals = load_status_codes()
    print(f"GENERR: {len(status_vals)} NTSTATUS codes", file=sys.stderr)

    error_vals = load_error_codes()
    print(f"GENERR: {len(error_vals)} error codes", file=sys.stderr)

    print(f"GENERR: Extracting code pairs from {generr_c}...", file=sys.stderr)
    pairs = extract_code_pairs(generr_c, status_vals, error_vals)
    print(f"GENERR: {len(pairs) // 2} code pairs", file=sys.stderr)

    print(f"GENERR: Writing {output_path}...", file=sys.stderr)
    with open(output_path, 'w') as f:
        generate_error_h(pairs, f)

    print(f"GENERR: Done.", file=sys.stderr)


if __name__ == '__main__':
    main()
