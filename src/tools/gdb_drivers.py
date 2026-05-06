# gdb_drivers.py — walk PsLoadedModuleList and add-symbol-file each
# loaded driver's .dwf at its runtime DllBase.
#
# Sourced by `make gdb` after gdb.init.  Provides:
#
#   (gdb) loaddrivers
#       Walks the kernel's loaded-module list, looks up <name>.dwf
#       under PUBLIC/SDK/LIB/I386, and add-symbol-file's each one with
#       offset = (runtime DllBase) - (PE-encoded ImageBase).
#
# Without this, drivers built with --syms have correct RVAs in their
# .dwf but the absolute addresses are baked at the *original* PE base
# (typically 0x10000) — at runtime the loader allocates a different
# kernel-mode VA and gdb can't find the symbols there.  Walking
# PsLoadedModuleList gets us the real DllBase per driver.
#
# We hard-code NT 3.5's LDR_DATA_TABLE_ENTRY layout (NTLDR.H) rather
# than depending on gdb's DWARF type lookup — dbg2dwf currently emits
# struct types in a synthetic types-only CU that gdb's `lookup_type`
# doesn't always see.  The layout is fixed for this OS version
# anyway (we're not targeting other NT releases).
#
# Requires: kernel past IoInitSystem (else PsLoadedModuleList is empty).

import gdb
import os
import struct

# Search path for <name>.dwf — set via env var or default to the
# canonical PUBLIC/SDK/LIB/I386 staging dir under the source tree.
_THIS_DIR  = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(os.path.dirname(_THIS_DIR))   # src/tools/.. → repo
_DEFAULT   = os.path.join(_REPO_ROOT, "src", "NT", "PUBLIC", "SDK", "LIB", "I386")
DWF_DIR    = os.environ.get("MICRONT_DWF_DIR", _DEFAULT)

# NT 3.5 _LDR_DATA_TABLE_ENTRY field offsets (NTLDR.H).  All pointers
# are 32-bit (kernel is i386 even though gdb sees x86-64 long mode).
LDR_OFF_DLLBASE             = 0x18
LDR_OFF_BASEDLLNAME_LENGTH  = 0x2c
LDR_OFF_BASEDLLNAME_BUFFER  = 0x30


def _read_u32(addr:int) -> int:
    raw = gdb.selected_inferior().read_memory(addr, 4).tobytes()
    return struct.unpack("<I", raw)[0]


def _read_u16(addr:int) -> int:
    raw = gdb.selected_inferior().read_memory(addr, 2).tobytes()
    return struct.unpack("<H", raw)[0]


def _read_unicode_string(buf_va:int, length:int) -> str:
    if buf_va == 0 or length == 0:
        return ""
    raw = gdb.selected_inferior().read_memory(buf_va, length).tobytes()
    try:
        return raw.decode("utf-16-le")
    except UnicodeDecodeError:
        return raw.hex()


def _read_pe_image_base(pe_path:str):
    """Return the IMAGE_OPTIONAL_HEADER.ImageBase from a PE file."""
    try:
        with open(pe_path, "rb") as f:
            if f.read(2) != b"MZ":
                return None
            f.seek(0x3c)
            e_lfanew = struct.unpack("<I", f.read(4))[0]
            f.seek(e_lfanew)
            if f.read(4) != b"PE\x00\x00":
                return None
            f.seek(e_lfanew + 4 + 20 + 0x1c)
            return struct.unpack("<I", f.read(4))[0]
    except (IOError, OSError, struct.error):
        return None


class LoadDriversCmd(gdb.Command):
    """Walk PsLoadedModuleList; add-symbol-file each entry's .dwf."""

    def __init__(self):
        super().__init__("loaddrivers", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            head_addr = int(gdb.parse_and_eval("&PsLoadedModuleList"))
        except gdb.error as e:
            print("loaddrivers: PsLoadedModuleList unresolved (%s)" % e)
            return

        # PsLoadedModuleList is a LIST_ENTRY; its Flink points at the
        # first LDR_DATA_TABLE_ENTRY's InLoadOrderLinks (which is the
        # first field of LDR_DATA_TABLE_ENTRY, so cast-equivalent).
        cur     = _read_u32(head_addr)
        loaded  = 0
        skipped: list[str] = []
        guard   = 0

        while cur != head_addr and guard < 256:
            try:
                base       = _read_u32(cur + LDR_OFF_DLLBASE)
                name_len   = _read_u16(cur + LDR_OFF_BASEDLLNAME_LENGTH)
                name_buf   = _read_u32(cur + LDR_OFF_BASEDLLNAME_BUFFER)
                name       = _read_unicode_string(name_buf, name_len)
                next_flink = _read_u32(cur)        # InLoadOrderLinks.Flink
            except gdb.error as e:
                print("  warn: bad LDR entry at 0x%x (%s)" % (cur, e))
                break

            if name and base:
                if name.lower() not in ("ntoskrnl.exe", "hal.dll"):
                    loaded += 1
                self._add_driver(name, base, skipped)

            cur = next_flink
            guard += 1

        print("loaddrivers: walked %d driver entries, added symbols for %d"
              % (loaded, loaded - len(skipped)))
        for s in skipped:
            print("  skipped: %s" % s)

    def _add_driver(self, name:str, runtime_base:int, skipped: list[str]):
        # ntoskrnl + hal are linked at canonical bases (no slide) and
        # `make gdb` already symbol-files them — no work needed here.
        # Their .dwf doesn't live under PUBLIC/SDK/LIB/I386 either; it
        # ships next to each PE under PRIVATE/.../obj/i386.
        if name.lower() in ("ntoskrnl.exe", "hal.dll"):
            return
        # name like "null.sys" — find <stem>.dwf next to <name>.
        stem = os.path.splitext(name)[0]
        pe   = os.path.join(DWF_DIR, name)
        dwf  = os.path.join(DWF_DIR, stem + ".dwf")
        if not os.path.exists(dwf):
            skipped.append("%s (no .dwf)" % name)
            return
        pe_base = _read_pe_image_base(pe) if os.path.exists(pe) else None
        if pe_base is None:
            skipped.append("%s (no PE base)" % name)
            return
        slide = (runtime_base - pe_base) & 0xffffffff
        # add-symbol-file -o <slide> applies a uniform delta to every
        # absolute address in the loaded ELF.  Our .dwf was emitted
        # with absolute VAs based on pe_base, so this lands them at
        # runtime_base + RVA, which is what the kernel's loader chose.
        cmd = "add-symbol-file %s -o 0x%x" % (dwf, slide)
        print("  %s @ DllBase=0x%x  pe_base=0x%x  slide=0x%x"
              % (name, runtime_base, pe_base, slide))
        gdb.execute(cmd, to_string=True)


LoadDriversCmd()
print("gdb_drivers.py: 'loaddrivers' command registered "
      "(walks PsLoadedModuleList, adds .dwf symbols at runtime DllBase).")
