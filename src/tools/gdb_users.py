# gdb_users.py — userland-side helpers for kernel-mode gdb sessions.
#
# Sourced by `make gdb` after gdb.init + gdb_drivers.py.  Provides:
#
#   (gdb) loaduser <name> <runtime_base>
#       Find the binary <name> under src/ (any PE+sibling .dwf), read
#       the PE's IMAGE_OPTIONAL_HEADER.ImageBase, compute slide =
#       runtime_base - pe_base, and add-symbol-file the .dwf with that
#       offset.  Mirror of loaddrivers but for user-mode binaries
#       whose runtime base you read from a serial-log crash.
#
#   (gdb) loaduserpath <abs_path> <runtime_base>
#       Same, but the binary path is given explicitly (skip tree scan).
#
#   (gdb) findpe <runtime_addr>
#       Reverse lookup: given an address, list every PE in the tree
#       whose code section *could* contain it (assuming common
#       relocations).  Useful when a serial log gives you an eip but
#       no process name.
#
#   (gdb) decodeav [logfile]
#       Shell out to tools/decode_av.py against `logfile` (or
#       `qemu.log` next to boot.sh) and print the symbolicated
#       output inline in the gdb session.  No need to leave gdb to
#       resolve a crash.
#
# Hardware breakpoints work across CPL transitions — the same hbreak
# command stops on user-mode VAs as well as kernel-mode ones — so once
# the user-mode binary is symbol-loaded, the workflow is identical to
# kernel debugging.

import gdb
import os
import struct
import subprocess
import sys


# Repo root: this file lives at <repo>/src/tools/gdb_users.py
_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(_THIS_DIR))   # src/tools/.. → repo
SRC_ROOT  = os.path.join(REPO_ROOT, "src")
DECODE_AV = os.path.join(SRC_ROOT, "tools", "decode_av.py")


PE_EXTS = (".exe", ".dll", ".sys", ".EXE", ".DLL", ".SYS")


def _read_pe_image_base_and_code(pe_path: str):
    """Return (image_base, code_lo_rva, code_hi_rva) for a PE32 file.

    code_lo_rva / code_hi_rva delimit every executable section's RVA
    range; an address falls in this binary's code if (addr - slide -
    image_base) lies within [code_lo, code_hi).
    """
    try:
        with open(pe_path, "rb") as f:
            data = f.read()
    except (IOError, OSError):
        return None
    if data[:2] != b"MZ":
        return None
    try:
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
            return None
        n_sec    = struct.unpack_from("<H", data, e_lfanew + 6)[0]
        size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
        opt_off  = e_lfanew + 24
        if struct.unpack_from("<H", data, opt_off)[0] != 0x10B:   # PE32 only
            return None
        image_base = struct.unpack_from("<I", data, opt_off + 28)[0]
        sec_off    = opt_off + size_opt
        code_lo, code_hi = None, None
        for i in range(n_sec):
            base  = sec_off + 40 * i
            vsize = struct.unpack_from("<I", data, base + 8)[0]
            rva   = struct.unpack_from("<I", data, base + 12)[0]
            chars = struct.unpack_from("<I", data, base + 36)[0]
            # CNT_CODE | MEM_EXECUTE
            if chars & 0x60000020:
                code_lo = rva if code_lo is None else min(code_lo, rva)
                code_hi = rva + vsize if code_hi is None else max(code_hi, rva + vsize)
        if code_lo is None:
            return None
    except (struct.error, IndexError):
        return None
    return image_base, code_lo, code_hi


def _find_binary(name: str):
    """Walk src/ looking for a PE matching `name` with a sibling .dwf.

    Returns (pe_path, dwf_path) or None.  Comparison is case-insensitive
    so `loaduser link.exe` matches `LINK.EXE` on case-sensitive Linux
    when stuff/ has a different convention than src/.
    """
    name_lc = name.lower()
    for dirpath, _dirs, files in os.walk(SRC_ROOT):
        for fn in files:
            if fn.lower() != name_lc:
                continue
            if not fn.lower().endswith(PE_EXTS[:3]):   # exe/dll/sys
                continue
            pe = os.path.join(dirpath, fn)
            stem, _ext = os.path.splitext(fn)
            for dwf_cand in (stem + ".dwf", stem + ".DWF",
                             stem.lower() + ".dwf",
                             stem.upper() + ".DWF"):
                dwf = os.path.join(dirpath, dwf_cand)
                if os.path.exists(dwf):
                    return pe, dwf
    return None


def _add_symbols(pe_path: str, dwf_path: str, runtime_base: int) -> bool:
    info = _read_pe_image_base_and_code(pe_path)
    if info is None:
        print("loaduser: %s is not a PE32 we can read" % pe_path)
        return False
    pe_base, _lo, _hi = info
    slide = (runtime_base - pe_base) & 0xFFFFFFFF
    print("  binary:        %s" % pe_path)
    print("  symbols:       %s" % dwf_path)
    print("  PE base:       0x%08x" % pe_base)
    print("  runtime base:  0x%08x" % runtime_base)
    print("  slide:         0x%x" % slide)
    cmd = "add-symbol-file %s -o 0x%x" % (dwf_path, slide)
    try:
        gdb.execute(cmd, to_string=False)
    except gdb.error as e:
        print("loaduser: gdb rejected add-symbol-file (%s)" % e)
        return False
    return True


# -----------------------------------------------------------------------
class LoadUserCmd(gdb.Command):
    """loaduser <name> <runtime_base>

    Find binary <name> under src/, compute slide from PE base, and
    add-symbol-file its .dwf at runtime_base.
    """

    def __init__(self):
        super().__init__("loaduser", gdb.COMMAND_USER)

    def invoke(self, argument: str, from_tty: bool) -> None:
        parts = argument.split()
        if len(parts) != 2:
            print("usage: loaduser <name> <runtime_base>")
            print("       e.g.  loaduser link.exe 0x01000000")
            return
        name, base_s = parts
        try:
            runtime_base = int(base_s, 0)
        except ValueError:
            print("loaduser: bad runtime_base '%s' — needs hex (0x...) or decimal" % base_s)
            return
        hit = _find_binary(name)
        if hit is None:
            print("loaduser: no PE+.dwf found for '%s' under %s" % (name, SRC_ROOT))
            print("          (use `loaduserpath <abs_path> <base>` to point at a specific file)")
            return
        pe, dwf = hit
        _add_symbols(pe, dwf, runtime_base)


class LoadUserPathCmd(gdb.Command):
    """loaduserpath <pe_path> <runtime_base>

    Add symbols for <pe_path>'s sibling .dwf at runtime_base, skipping
    the tree scan.  Useful when the binary lives outside src/.
    """

    def __init__(self):
        super().__init__("loaduserpath", gdb.COMMAND_USER)

    def invoke(self, argument: str, from_tty: bool) -> None:
        parts = argument.split()
        if len(parts) != 2:
            print("usage: loaduserpath <pe_path> <runtime_base>")
            return
        pe, base_s = parts
        try:
            runtime_base = int(base_s, 0)
        except ValueError:
            print("loaduserpath: bad runtime_base '%s'" % base_s)
            return
        if not os.path.exists(pe):
            print("loaduserpath: %s does not exist" % pe)
            return
        stem, _ext = os.path.splitext(pe)
        dwf = stem + ".dwf"
        if not os.path.exists(dwf):
            dwf = stem + ".DWF"
        if not os.path.exists(dwf):
            print("loaduserpath: no .dwf next to %s" % pe)
            return
        _add_symbols(pe, dwf, runtime_base)


class FindPeCmd(gdb.Command):
    """findpe <runtime_addr>

    Scan src/ for every PE+.dwf; print binaries whose code section
    could contain `runtime_addr` (assuming common relocations).
    Diagnostic — useful when you don't know which process an
    address came from.
    """

    KNOWN_RELOCATIONS = (
        0x00000000,    # not relocated
        0x00C00000,    # link.exe pattern: PE 0x00400000 → runtime 0x01000000
    )

    def __init__(self):
        super().__init__("findpe", gdb.COMMAND_USER)

    def invoke(self, argument: str, from_tty: bool) -> None:
        try:
            addr = int(argument.strip(), 0)
        except ValueError:
            print("usage: findpe <addr>   (hex with 0x or decimal)")
            return

        hits = []
        for dirpath, _dirs, files in os.walk(SRC_ROOT):
            for fn in files:
                if not fn.lower().endswith(PE_EXTS[:3]):
                    continue
                pe = os.path.join(dirpath, fn)
                info = _read_pe_image_base_and_code(pe)
                if info is None:
                    continue
                pe_base, code_lo, code_hi = info
                stem, _ext = os.path.splitext(fn)
                dwf = os.path.join(dirpath, stem + ".dwf")
                has_dwf = os.path.exists(dwf)
                for slide in self.KNOWN_RELOCATIONS:
                    lo = pe_base + code_lo + slide
                    hi = pe_base + code_hi + slide
                    if lo <= addr < hi:
                        hits.append((fn, pe, dwf if has_dwf else None,
                                     pe_base, slide))
        if not hits:
            print("findpe: 0x%x is not in any PE's code section "
                  "(considered slides: %s)"
                  % (addr, ", ".join("0x%x" % s for s in self.KNOWN_RELOCATIONS)))
            return
        for fn, pe, dwf, pe_base, slide in hits:
            slide_note = " (relocated +0x%x)" % slide if slide else ""
            print("  %s%s" % (fn, slide_note))
            print("    PE base:  0x%08x" % pe_base)
            print("    path:     %s" % pe)
            if dwf:
                print("    symbols:  %s" % dwf)
                print("    paste:    loaduser %s 0x%08x" % (fn, pe_base + slide))
            else:
                print("    .dwf:     <missing>  (run splitsym + dbg2dwf)")


class DecodeAvCmd(gdb.Command):
    """decodeav [logfile]

    Shell out to tools/decode_av.py against `logfile` (or `qemu.log`
    next to boot.sh) and print the symbolicated crash inline in the
    gdb session.  Lets you symbolicate without leaving the debugger.
    """

    def __init__(self):
        super().__init__("decodeav", gdb.COMMAND_USER)

    def invoke(self, argument: str, from_tty: bool) -> None:
        log = argument.strip() or os.path.join(SRC_ROOT, "..", "qemu.log")
        if not os.path.exists(log):
            print("decodeav: log not found: %s" % log)
            print("          (capture with `boot.sh --trace`, or pass an explicit path)")
            return
        if not os.path.exists(DECODE_AV):
            print("decodeav: %s missing" % DECODE_AV)
            return
        try:
            r = subprocess.run([sys.executable or "python3", DECODE_AV, log],
                               capture_output=True, text=True, timeout=60)
        except (subprocess.SubprocessError, FileNotFoundError) as e:
            print("decodeav: failed to run decode_av.py (%s)" % e)
            return
        if r.stdout:
            print(r.stdout, end="")
        if r.stderr:
            sys.stderr.write(r.stderr)


LoadUserCmd()
LoadUserPathCmd()
FindPeCmd()
DecodeAvCmd()

print("gdb_users.py: registered "
      "loaduser, loaduserpath, findpe, decodeav.")
