#!/usr/bin/env python3
"""
mkhive.py - Generate a minimal NT 3.5 SYSTEM registry hive

Creates a binary hive file with the minimum structure needed to boot:

  SYSTEM (root)
  ├── Select
  │   ├── Current       = DWORD 1
  │   ├── Default       = DWORD 1
  │   ├── LastKnownGood = DWORD 1
  │   └── Failed        = DWORD 0
  └── ControlSet001
      └── Control

Usage: mkhive.py [output_file]
"""

import struct
import sys
import time

PAGE = 4096
CELL_ALIGN = 8

def align8(n:int):
    return (n + CELL_ALIGN - 1) & ~(CELL_ALIGN - 1)

def align_page(n:int):
    return (n + PAGE - 1) & ~(PAGE - 1)


class Hive:
    """Builds a registry hive in memory."""

    def __init__(self):
        # bin_data holds everything AFTER the hbin header
        self.bin_data = bytearray()

    def _alloc(self, payload_size:int):
        """Allocate a cell. Returns hive cell index (offset from hbin start,
        including the 32-byte hbin header)."""
        total = align8(4 + payload_size)  # 4-byte size prefix + payload
        bin_offset = len(self.bin_data)
        cell = struct.pack('<i', -total)  # negative = allocated
        cell += b'\x00' * (total - 4)
        self.bin_data.extend(cell)
        # Cell index = hbin header size + offset within bin_data
        return 32 + bin_offset

    def _put(self, cell_index:int, field_offset:int, data:bytes):
        """Write data into a cell. cell_index includes hbin header offset."""
        # Convert cell index to bin_data position: subtract hbin header, skip 4-byte size
        pos = (cell_index - 32) + 4 + field_offset
        self.bin_data[pos:pos + len(data)] = data

    def make_nk(self, name:str, parent:int, flags:int=0, subkey_count:int=0, subkey_list:int=0xFFFFFFFF,
                value_count:int=0, value_list:int=0xFFFFFFFF):
        """Create a CM_KEY_NODE cell. Returns cell index.
        Layout from CMP.H with #pragma pack(4):
          +0:  Signature(2) + Flags(2)
          +4:  LastWriteTime(8)
          +12: Spare(4)
          +16: Parent(4)
          +20: SubKeyCounts[0]=Stable(4), [1]=Volatile(4)
          +28: SubKeyLists[0]=Stable(4), [1]=Volatile(4)
          +36: ValueList.Count(4), ValueList.List(4)
          +44: Security(4), Class(4)
          +52: MaxNameLen(4), MaxClassLen(4), MaxValueNameLen(4), MaxValueDataLen(4)
          +68: WorkVar(4)
          +72: NameLength(2), ClassLength(2)
          +76: Name (compressed ASCII if KEY_COMP_NAME set, else WCHAR)
        NT 3.5 hive v2: names are compressed ASCII (single bytes) with KEY_COMP_NAME=0x0020."""
        name_b = name.encode('ascii')
        cell = self._alloc(76 + len(name_b))
        ts = int(time.time() * 10000000) + 116444736000000000

        nk = struct.pack('<2sH',
            b'nk',             # +0  Signature
            flags | 0x0020,    # +2  Flags (KEY_COMP_NAME = 0x0020)
        )
        nk += struct.pack('<Q', ts)         # +4  LastWriteTime
        nk += struct.pack('<I', 0)          # +12 Spare
        nk += struct.pack('<I', parent)     # +16 Parent
        nk += struct.pack('<II', subkey_count, 0)  # +20 SubKeyCounts[Stable, Volatile]
        nk += struct.pack('<II', subkey_list, 0xFFFFFFFF)  # +28 SubKeyLists[Stable, Volatile]
        nk += struct.pack('<II', value_count, value_list)  # +36 ValueList{Count, List}
        nk += struct.pack('<II', 0xFFFFFFFF, 0xFFFFFFFF)   # +44 Security, Class
        nk += struct.pack('<IIII', 0, 0, 0, 0)  # +52 MaxNameLen..MaxValueDataLen
        nk += struct.pack('<I', 0)          # +68 WorkVar
        nk += struct.pack('<HH', len(name_b), 0)  # +72 NameLength, ClassLength
        nk += name_b                        # +76 Name (compressed ASCII bytes)

        self._put(cell, 0, nk)
        return cell

    def set_parent(self, cell:int, parent:int):
        """Fix up the parent pointer in an nk cell. Parent is at field offset 16."""
        self._put(cell, 16, struct.pack('<I', parent))

    def make_vk(self, name:str, vtype:int, data:bytes):
        """Create a value node with compressed ASCII name. Returns cell offset."""
        name_b = name.encode('ascii')
        cell = self._alloc(20 + len(name_b))

        if len(data) <= 4:
            padded = (data + b'\x00' * 4)[:4]
            data_off = struct.unpack('<I', padded)[0]
            data_len = len(data) | 0x80000000
        else:
            data_cell = self._alloc(len(data))
            self._put(data_cell, 0, data)
            data_off = data_cell
            data_len = len(data)

        vk = struct.pack('<2sHIIIHH',
            b'vk',            # +0  Signature
            len(name_b),      # +2  NameLength (in bytes, compressed)
            data_len,         # +4  DataLength
            data_off,         # +8  DataOffset / inline data
            vtype,            # +12 Type
            0x0001,           # +16 Flags (VALUE_COMP_NAME = 0x0001)
            0,                # +18 Spare
        ) + name_b
        self._put(cell, 0, vk)
        return cell

    def make_value_list(self, offsets:list[int]):
        """Create a cell with an array of value cell offsets."""
        cell = self._alloc(4 * len(offsets))
        data = b''.join(struct.pack('<I', o) for o in offsets)
        self._put(cell, 0, data)
        return cell

    def make_il(self, children:list[int]):
        """Create an index leaf (CM_KEY_INDEX with 'li' signature).
        children = [cell_index, ...] — plain HCELL_INDEX values, no hash hints.
        NT 3.5 uses 'li'/'ri' index types, NOT 'lf'/'lh' (those are post-XP)."""
        cell = self._alloc(4 + 4 * len(children))
        il = struct.pack('<2sH', b'li', len(children))
        for coff in children:
            il += struct.pack('<I', coff)
        self._put(cell, 0, il)
        return cell

    def build(self):
        """Construct the hive and return complete bytes."""
        # Build cells bottom-up

        # Values for Select key
        # Use lowercase names to match kernel search strings exactly,
        # avoiding NLS case conversion dependency during early boot.
        v_cur = self.make_vk("current", 4, struct.pack('<I', 1))
        v_def = self.make_vk("default", 4, struct.pack('<I', 1))
        v_lkg = self.make_vk("lastknowngood", 4, struct.pack('<I', 1))
        v_fail = self.make_vk("failed", 4, struct.pack('<I', 0))
        vl_select = self.make_value_list([v_cur, v_def, v_lkg, v_fail])

        # Control key (child of ControlSet001)
        # "control" matches kernel's L"control" search
        k_control = self.make_nk("control", 0)

        # ControlSet001 — exact match with kernel's sprintf("ControlSet%03d", n)
        sl_cs1 = self.make_il([k_control])
        k_cs1 = self.make_nk("ControlSet001", 0, subkey_count=1, subkey_list=sl_cs1)
        self.set_parent(k_control, k_cs1)

        # "select" — kernel searches for L"select" (lowercase)
        k_select = self.make_nk("select", 0, value_count=4, value_list=vl_select)

        # Root key — children must be sorted alphabetically for binary search
        sl_root = self.make_il([k_cs1, k_select])
        k_root = self.make_nk("System", 0, flags=0x0004,
                               subkey_count=2, subkey_list=sl_root)

        # Fix parent pointers
        self.set_parent(k_select, k_root)
        self.set_parent(k_cs1, k_root)
        self.set_parent(k_root, k_root)  # root's parent = itself

        # Pad bin_data to page alignment (minus hbin header)
        hbin_hdr_size = 32
        total_bin = align_page(hbin_hdr_size + len(self.bin_data))
        pad = total_bin - hbin_hdr_size - len(self.bin_data)
        if pad >= 8:
            # Add a free cell for the remaining space
            self.bin_data.extend(struct.pack('<i', pad))  # positive = free
            self.bin_data.extend(b'\x00' * (pad - 4))
        elif pad > 0:
            self.bin_data.extend(b'\x00' * pad)

        # Build hbin header
        hbin_size = hbin_hdr_size + len(self.bin_data)
        hbin = struct.pack('<4sIIII',
            b'hbin',       # Signature
            0,             # FileOffset
            hbin_size,     # Size
            0, 0,          # Reserved, Timestamp
        )
        hbin += b'\x00' * (hbin_hdr_size - len(hbin))

        # Build base block (regf header)
        hive_bins_size = hbin_size
        ts = int(time.time() * 10000000) + 116444736000000000

        base = bytearray(PAGE)
        struct.pack_into('<4sII', base, 0, b'regf', 1, 1)  # sig, seq1, seq2
        struct.pack_into('<Q', base, 12, ts)                 # timestamp
        struct.pack_into('<II', base, 20, 1, 2)              # major=1, minor=2
        struct.pack_into('<II', base, 28, 0, 1)              # type=0, format=1
        struct.pack_into('<I', base, 36, k_root)             # root cell
        struct.pack_into('<I', base, 40, hive_bins_size)     # length
        struct.pack_into('<I', base, 44, 1)                  # cluster

        # File name at offset 48 (64 bytes UTF-16LE)
        fname = "SYSTEM\0".encode('utf-16-le')
        base[48:48 + len(fname)] = fname

        # Checksum at offset 508 = XOR of DWORDs 0..507
        cksum = 0
        for i in range(0, 508, 4):
            cksum ^= struct.unpack_from('<I', base, i)[0]
            cksum &= 0xFFFFFFFF
        struct.pack_into('<I', base, 508, cksum)

        return bytes(base) + hbin + bytes(self.bin_data)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "SYSTEM"
    hive = Hive()
    data = hive.build()
    with open(out, 'wb') as f:
        f.write(data)
    print(f"SYSTEM hive: {len(data)} bytes -> {out}")


if __name__ == '__main__':
    main()
