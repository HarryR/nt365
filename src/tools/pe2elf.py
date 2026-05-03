#!/usr/bin/env python3
"""
pe2elf.py — Extract PE COFF symbols into a gdb-loadable ELF.

LINK 2.50 (the NT 3.5 toolchain) stores each COFF symbol's Value field
as RVA from ImageBase (so absolute VA = ImageBase + Value).  Modern
`objcopy -I pei-i386 -O elf32-i386` interprets Value as section-relative
(absolute VA = section.VA + Value), which produces correct addresses
only for symbols in `.text` (RVA = 0x1000, near-zero offset) and
high-by-section_RVA addresses for everything in PAGE / INIT / etc.
For ntoskrnl Phase1Initialization that's a 0xac000 error — gdb
hbreaks at a never-executed address and silently misses every boot.

This script parses the COFF symbol table directly, applies the LINK
2.50 convention, generates a small assembly file with `.global name`
+ `name = absolute_va` directives, and assembles it via `as` into a
gdb-loadable ELF symbol stub.  Same output shape as the failed
objcopy approach, correct addresses.

Usage:
    pe2elf.py <pe_file> <output_elf>
"""

import os
import re
import struct
import subprocess
import sys


# `_KeBugCheckEx@20` → `KeBugCheckEx`.  cdecl underscore prefix and
# stdcall `@N` byte-count suffix come from the Windows linker but
# Linux gas rejects `@` as a symbol-name character.  Strip both — gives
# us clean gdb names too (`b KeBugCheckEx` instead of `b
# _KeBugCheckEx@20`).  Risk of collision is theoretical: NT 3.5
# doesn't overload function names by stdcall arity.
_DECORATION_RE = re.compile(r'^_(.+?)(?:@\d+)?$')


def clean_name(name):
    m = _DECORATION_RE.match(name)
    return m.group(1) if m else name


def parse_pe(data):
    if data[:2] != b'MZ':
        raise ValueError("Not a PE file (no MZ)")
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    if struct.unpack_from('<I', data, e_lfanew)[0] != 0x4550:
        raise ValueError("Bad PE signature")

    # COFF file header (immediately after the PE signature).
    coff = e_lfanew + 4
    (machine, num_sections, _ts, ptr_sym, num_sym, opt_size, _chars) = \
        struct.unpack_from('<HHIIIHH', data, coff)

    # Optional header — read ImageBase.
    opt = coff + 20
    image_base = struct.unpack_from('<I', data, opt + 28)[0]

    # COFF symbol records: 18 bytes each, immediately followed by the
    # string table for names longer than 8 bytes.
    sym_table_off = ptr_sym
    str_table_off = sym_table_off + num_sym * 18
    string_table = data[str_table_off:]

    def read_name(rec):
        if rec[:4] == b'\0\0\0\0':
            offset = struct.unpack_from('<I', rec, 4)[0]
            end = string_table.index(b'\0', offset)
            return string_table[offset:end].decode('ascii', errors='replace')
        nul = rec.find(b'\0', 0, 8)
        if nul == -1:
            return rec[:8].decode('ascii', errors='replace')
        return rec[:nul].decode('ascii', errors='replace')

    symbols = []
    i = 0
    while i < num_sym:
        rec_off = sym_table_off + i * 18
        rec = data[rec_off:rec_off + 18]
        name = read_name(rec)
        value, sec_num, _stype, scl, naux = struct.unpack_from(
            '<IhHBB', rec, 8)
        symbols.append({
            'name':    name,
            'value':   value,
            'sec_num': sec_num,
            'scl':     scl,
        })
        i += 1 + naux  # auxiliary records follow some main records

    return image_base, symbols


def emit_assembly(image_base, symbols):
    """Return (asm_bytes, symbol_count, dotted_count).

    Generates a `.text` section containing one byte (0xCC) per symbol,
    positioned via `.org` at (sym.value).  After ld -Ttext=ImageBase
    the section's VA = ImageBase, so each symbol lands at its absolute
    kernel address.

    Why not just `.equ name = absolute_addr`?  Because gas always
    classifies `.equ` as ABS, and gdb treats ABS symbols as
    non-breakpointable minsyms ("Function not defined").  Putting them
    in a real .text section with @function type makes gdb treat them as
    callable code — `b Phase1Initialization` works, `info functions`
    lists them, disassembly works.

    The 0xCC placeholder byte is INT3 — irrelevant since the section is
    NOT loaded into the running kernel; it just exists in our ELF stub
    so the section has nonzero size and the labels are anchored.
    """
    # First pass: filter + clean + collect addr->[names].
    by_addr = {}  # offset -> list of names (preserves first-seen order)
    skipped_dotted = 0
    seen_names = set()
    for s in symbols:
        if s['sec_num'] <= 0:
            continue
        if s['scl'] not in (2, 3):
            continue
        name = s['name']
        if not name:
            continue
        if name.startswith('.'):
            skipped_dotted += 1
            continue
        if any(c in name for c in '?\0\x7f'):
            continue
        name = clean_name(name)
        if not name or '@' in name:
            continue
        if name in seen_names:
            continue
        seen_names.add(name)
        offset = s['value'] & 0xFFFFFFFF
        by_addr.setdefault(offset, []).append(name)

    # Sort by offset so .org goes monotonically forward (gas requires it).
    sorted_offsets = sorted(by_addr.keys())

    lines = [
        '# Auto-generated by tools/pe2elf.py - do not edit.',
        f'# ImageBase = 0x{image_base:08x}',
        '.section .text, "ax", @progbits',
        '',
    ]
    for off in sorted_offsets:
        lines.append(f'.org 0x{off:x}')
        for name in by_addr[off]:
            lines.append(f'.global {name}')
            lines.append(f'.type {name},@function')
            lines.append(f'{name}:')
        # One placeholder byte per address group; alias labels share it.
        lines.append('.byte 0xCC')
    # Trailing newline so gas doesn't warn "end of file not at end of line".
    lines.append('')
    return ('\n'.join(lines).encode('utf-8'),
            len(seen_names), skipped_dotted)


def find_tool(candidates):
    for cand in candidates:
        try:
            subprocess.run([cand, '--version'],
                           capture_output=True, check=True)
            return cand
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    return None


def find_assembler():
    """Linux gas — i686-w64-mingw32-as is PE/COFF only, breaks ELF load."""
    return find_tool(('as', 'i686-linux-gnu-as'))


def find_linker():
    return find_tool(('ld', 'i686-linux-gnu-ld'))


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    pe_path, out_path = sys.argv[1], sys.argv[2]

    with open(pe_path, 'rb') as f:
        data = f.read()
    image_base, symbols = parse_pe(data)
    asm, count, _skipped = emit_assembly(image_base, symbols)

    asm_tool = find_assembler()
    ld_tool  = find_linker()
    if not asm_tool or not ld_tool:
        print("ERROR: need both `as` and `ld` from binutils", file=sys.stderr)
        sys.exit(1)

    # Two-stage: `as` produces a relocatable .o with section-relative
    # symbols, `ld -Ttext=ImageBase` rebases .text so each symbol lands
    # at its absolute kernel VA in the resulting executable ELF.
    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.o', delete=False) as obj:
        obj_path = obj.name
    try:
        subprocess.run([asm_tool, '--32', '-o', obj_path, '-'],
                       input=asm, check=True)
        subprocess.run([ld_tool, '-m', 'elf_i386',
                        '-Ttext', f'0x{image_base:x}',
                        '--no-warn-mismatch',
                        '-o', out_path, obj_path],
                       check=True,
                       # ld warns "cannot find entry symbol _start" — harmless,
                       # we don't have one because the ELF is symbols-only.
                       stderr=subprocess.DEVNULL)
    finally:
        os.unlink(obj_path)

    name = os.path.basename(pe_path)
    print(f"{name}: ImageBase=0x{image_base:08x} "
          f"{count} symbols -> {out_path}")


if __name__ == '__main__':
    main()
