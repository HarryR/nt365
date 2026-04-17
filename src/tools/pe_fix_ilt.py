#!/usr/bin/env python3
"""
Patch a PE file to populate a missing Import Lookup Table (ILT) from the
Import Address Table (IAT).

Older NT 3.5-era PEs (notably MSVCRT20.DLL) ship with importLookupTable = 0
in the import directory entry, because the on-disk IAT already contains the
hint/name RVAs and the Windows loader overwrites them in place.

Stock wibo (<= 1.1.0) does not handle that case; it reads garbage from the
MZ header and never resolves the imports. Patching the ILT field to point
at the IAT makes wibo's import resolver work correctly.

Usage: pe_fix_ilt.py <file.dll>
"""
import struct
import sys
from pathlib import Path


def rva_to_offset(sections:list[tuple[int,int,int,int]], rva:int):
    for vaddr, vsize, raw_off, raw_size in sections:
        if vaddr <= rva < vaddr + max(vsize, raw_size):
            return raw_off + (rva - vaddr)
    raise ValueError(f"RVA {rva:#x} not in any section")


def patch(path: Path) -> int:
    data = bytearray(path.read_bytes())

    # DOS header -> PE header
    if data[:2] != b"MZ":
        raise ValueError("not a PE")
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\0\0":
        raise ValueError("no PE signature")

    coff = pe_off + 4
    num_sections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20

    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x10B:
        raise ValueError(f"only PE32 supported (magic {magic:#x})")

    # Data directories start at offset 96 inside the optional header for PE32.
    # Entry 1 = Import Directory: RVA, Size (8 bytes).
    import_dir_rva = struct.unpack_from("<I", data, opt + 96 + 1 * 8)[0]

    # Section table follows the optional header.
    sec_tab = opt + opt_size
    sections:list[tuple[int,int,int,int]] = []
    for i in range(num_sections):
        s = sec_tab + i * 40
        vsize:int
        vaddr:int
        raw_size:int
        raw_off:int
        vsize, vaddr, raw_size, raw_off = struct.unpack_from("<IIII", data, s + 8)
        sections.append((vaddr, vsize, raw_off, raw_size))

    import_dir_off = rva_to_offset(sections, import_dir_rva)

    # Walk 20-byte IMAGE_IMPORT_DESCRIPTOR entries until the terminating zero
    # entry. Fields: OriginalFirstThunk (ILT), TimeDateStamp, ForwarderChain,
    # Name, FirstThunk (IAT).
    patched = 0
    off = import_dir_off
    while True:
        ilt, tds, fwd, name, iat = struct.unpack_from("<IIIII", data, off)
        if ilt == tds == fwd == name == iat == 0:
            break
        if ilt == 0 and iat != 0:
            struct.pack_into("<I", data, off, iat)
            dll = data[rva_to_offset(sections, name):].split(b"\0", 1)[0].decode("latin-1")
            print(f"  patched ILT for {dll}: 0 -> {iat:#x}")
            patched += 1
        off += 20

    if patched:
        path.write_bytes(data)
    return patched


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(2)
    for arg in sys.argv[1:]:
        p = Path(arg)
        print(f"{p}:")
        n = patch(p)
        if n == 0:
            print("  no changes needed")
        else:
            print(f"  {n} entr{'y' if n == 1 else 'ies'} patched")


if __name__ == "__main__":
    main()
