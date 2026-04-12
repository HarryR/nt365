#!/usr/bin/env python3
"""Dump the structure of a registry hive for debugging."""
import struct, sys

data = open(sys.argv[1] if len(sys.argv) > 1 else "boot/data/SYSTEM", "rb").read()

print("=== regf header ===")
print(f"Signature: {data[0:4]}")
root_cell = struct.unpack_from("<I", data, 36)[0]
print(f"Root cell offset: 0x{root_cell:X}")

hbin = data[4096:]
print(f"\n=== hbin header ===")
print(f"Signature: {hbin[0:4]}")
print(f"Size: {struct.unpack_from('<I', hbin, 8)[0]}")

pos = 32
while pos < len(hbin) - 4:
    size = struct.unpack_from("<i", hbin, pos)[0]
    abs_size = abs(size)
    if abs_size < 8 or abs_size > 4096:
        break
    alloc = "ALLOC" if size < 0 else "FREE"
    sig = hbin[pos+4:pos+6]
    detail = ""
    if sig == b"nk":
        flags = struct.unpack_from("<H", hbin, pos+6)[0]
        parent = struct.unpack_from("<I", hbin, pos+20)[0]
        sk_count = struct.unpack_from("<I", hbin, pos+24)[0]
        sk_list = struct.unpack_from("<I", hbin, pos+32)[0]
        vc = struct.unpack_from("<I", hbin, pos+40)[0]
        vl = struct.unpack_from("<I", hbin, pos+44)[0]
        namelen = struct.unpack_from("<H", hbin, pos+76)[0]
        if flags & 0x0020:
            name = hbin[pos+80:pos+80+namelen].decode("ascii", errors="replace")
            comp = "COMP"
        else:
            name = hbin[pos+80:pos+80+namelen].decode("utf-16-le", errors="replace")
            comp = "UTF16"
        detail = (f'  [{comp}] "{name}" parent=0x{parent:X} '
                  f'subkeys={sk_count}@0x{sk_list:X} values={vc}@0x{vl:X} flags=0x{flags:04X}')
    elif sig == b"vk":
        namelen = struct.unpack_from("<H", hbin, pos+6)[0]
        datalen = struct.unpack_from("<I", hbin, pos+8)[0]
        dataoff = struct.unpack_from("<I", hbin, pos+12)[0]
        vtype = struct.unpack_from("<I", hbin, pos+16)[0]
        vflags = struct.unpack_from("<H", hbin, pos+20)[0]
        if vflags & 0x0001:
            name = hbin[pos+24:pos+24+namelen].decode("ascii", errors="replace")
            comp = "COMP"
        else:
            name = hbin[pos+24:pos+24+namelen].decode("utf-16-le", errors="replace")
            comp = "UTF16"
        inline = " (inline)" if datalen & 0x80000000 else ""
        detail = f'  [{comp}] "{name}" type={vtype} datalen=0x{datalen:X} dataoff=0x{dataoff:X}{inline}'
    elif sig in (b"li", b"ri", b"lf", b"lh"):
        count = struct.unpack_from("<H", hbin, pos+6)[0]
        stride = 4 if sig in (b"li", b"ri") else 8
        entries = []
        for i in range(count):
            e = struct.unpack_from("<I", hbin, pos+8+stride*i)[0]
            entries.append(f"0x{e:X}")
        detail = f"  count={count} entries=[{', '.join(entries)}]"
    print(f"  cell@0x{pos:04X} [{alloc}] size={abs_size:4d}  sig={sig.decode('ascii', errors='replace')}{detail}")
    pos += abs_size
