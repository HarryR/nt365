#!/usr/bin/env python3
"""
decode_av.py — parse MicroNT serial-log crashes and emit symbolicated
output plus paste-ready gdb commands.

Usage:
    decode_av.py LOGFILE
    decode_av.py < LOGFILE
    decode_av.py --addr 0x01002be0 [--binary path/to/X.exe] [--base 0xRUNTIME]

The point of this tool is to close the loop between "I see a crash in
the serial log" and "I'm ready to paste commands into gdb" with a
single command, so an agent doesn't have to re-derive the slide
formula or hunt for which .dwf to load.

What it recognises (regex-driven, easy to extend):
    UMODE EXC(...)   — userland access violation, first or second chance
    *** STOP:        — kernel bugcheck (KeBugCheckEx)
    STOP:            — short-form bugcheck banner
    *** Fatal System Error  — early-boot pre-display bugcheck

What it does:
  1. extract every crash record from the log
  2. classify each address as kernel (>= 0x80000000) or user
  3. auto-discover candidate binaries by scanning the tree for .dwf
     files and matching against the address (kernel) or known runtime
     bases (user)
  4. resolve to function:line via addr2line
  5. annotate well-known faulting-address patterns (heap fills etc.)
  6. emit the gdb commands you'd paste to set a useful breakpoint
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# Repo root: this file lives at <repo>/src/tools/decode_av.py
THIS  = Path(__file__).resolve()
REPO  = THIS.parents[2]


# -----------------------------------------------------------------------
# Heuristics for faulting-address values that have meaning beyond "this
# pointer is bad".  When p1 == one of these, the diagnosis flips from
# "random memory corruption" to "specifically <X>".
# -----------------------------------------------------------------------
HEAP_PATTERNS = {
    0xfeeefeee: ("HeapFree debug fill (CRT)",
                 "use-after-free or stale view; the value came from "
                 "inside the process, not from the kernel"),
    0xcccccccc: ("uninitialised stack (CRT debug)",
                 "stack local read before written; missing init"),
    0xbaadf00d: ("LocalAlloc/HeapAlloc uninit fill",
                 "freshly allocated buffer never written to"),
    0xfdfdfdfd: ("CRT debug guard bytes",
                 "off-by-one over/underrun touching guard region"),
    0xabababab: ("HeapAlloc tail fill",
                 "write past the end of an allocation"),
    0xdeadbeef: ("intentional sentinel",
                 "code reached a 'should not happen' path"),
    0x00000000: ("NULL",
                 "missing null check or freed-then-zeroed handle"),
    0xffffffff: ("INVALID_HANDLE_VALUE",
                 "operation against a closed/never-opened handle"),
}


# -----------------------------------------------------------------------
# Crash-line regexes.  Add to this list as new shapes turn up; the rest
# of the tool branches on the matched group dict.
# -----------------------------------------------------------------------
RX_UMODE = re.compile(
    r'UMODE EXC\((?P<chance>\w+)\):\s+'
    r'code=(?P<code>\w+)\s+'
    r'addr=(?P<addr>\w+)\s+'
    r'p0=(?P<p0>\w+)\s+'
    r'p1=(?P<p1>\w+)'
    r'(?:\s+eip=(?P<eip>\w+))?'
)

RX_STOP_LONG = re.compile(
    r'\*\*\*\s+STOP:\s+(?P<code>\w+)'
    r'\s*\((?P<a0>\w+),\s*(?P<a1>\w+),\s*(?P<a2>\w+),\s*(?P<a3>\w+)\)'
)

RX_STOP_SHORT = re.compile(
    r'(?<!\*)\bSTOP:\s+(?P<code>\w+)\s+(?P<msg>.+?)$'
)

RX_FATAL = re.compile(
    r'Fatal System Error:\s+(?P<code>\w+),'
    r'\s*\((?P<a0>\w+),\s*(?P<a1>\w+),\s*(?P<a2>\w+),\s*(?P<a3>\w+)\)'
)


# -----------------------------------------------------------------------
# Bugcheck-code → name mapping.  Just the ones likely to show up; full
# list is enormous and not worth dragging in.
# -----------------------------------------------------------------------
BUGCHECK_NAMES = {
    0x0000001E: "KMODE_EXCEPTION_NOT_HANDLED",
    0x00000050: "PAGE_FAULT_IN_NONPAGED_AREA",
    0x0000007B: "INACCESSIBLE_BOOT_DEVICE",
    0x0000007F: "UNEXPECTED_KERNEL_MODE_TRAP",
    0x000000D1: "DRIVER_IRQL_NOT_LESS_OR_EQUAL",
    0xCAFE5E1F: "KI_SEH_GUARD_BUGCHECK (MicroNT)",
    0xD0000144: "STATUS_UNHANDLED_EXCEPTION (raised, not technically a bugcheck code)",
}


# -----------------------------------------------------------------------
# PE inspection: read ImageBase and code section ranges so we can
# associate runtime addresses with on-disk binaries.
# -----------------------------------------------------------------------
@dataclass
class PEInfo:
    path:        Path
    dwf:         Optional[Path]
    image_base:  int
    code_lo:     int      # smallest .text/.code RVA
    code_hi:     int      # largest .text/.code RVA + size
    name:        str      # basename for display


def read_pe(path: Path) -> Optional[PEInfo]:
    try:
        d = path.read_bytes()
    except OSError:
        return None
    if d[:2] != b"MZ":
        return None
    try:
        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        if d[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
            return None
        n_sec = struct.unpack_from("<H", d, e_lfanew + 6)[0]
        size_opt = struct.unpack_from("<H", d, e_lfanew + 20)[0]
        opt_off = e_lfanew + 24
        magic = struct.unpack_from("<H", d, opt_off)[0]
        if magic != 0x10B:           # PE32 only — we don't ship PE32+
            return None
        image_base = struct.unpack_from("<I", d, opt_off + 28)[0]
        sec_off = opt_off + size_opt
        code_lo, code_hi = None, None
        for i in range(n_sec):
            base = sec_off + 40 * i
            name = d[base:base + 8].rstrip(b"\0").decode("latin-1", "replace")
            vsize = struct.unpack_from("<I", d, base + 8)[0]
            rva = struct.unpack_from("<I", d, base + 12)[0]
            chars = struct.unpack_from("<I", d, base + 36)[0]
            # IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE
            if chars & 0x60000020:
                if code_lo is None or rva < code_lo:
                    code_lo = rva
                end = rva + vsize
                if code_hi is None or end > code_hi:
                    code_hi = end
        if code_lo is None:
            return None
    except (struct.error, IndexError):
        return None

    dwf = path.with_suffix(".dwf")
    if not dwf.exists():
        # try uppercase / lowercase fallbacks
        for stem_case in (path.stem, path.stem.lower(), path.stem.upper()):
            for ext_case in (".dwf", ".DWF"):
                cand = path.parent / (stem_case + ext_case)
                if cand.exists():
                    dwf = cand
                    break
        if not dwf.exists():
            dwf = None

    return PEInfo(
        path=path,
        dwf=dwf,
        image_base=image_base,
        code_lo=code_lo,
        code_hi=code_hi,
        name=path.name,
    )


# -----------------------------------------------------------------------
# Tree scan.  Build a list of every PE under src/ that has a .dwf next
# to it.  We don't want PEs without symbols cluttering the candidate
# list — they can't symbolicate anything anyway.
# -----------------------------------------------------------------------
PE_EXTS = {".exe", ".dll", ".sys", ".EXE", ".DLL", ".SYS"}


def scan_tree(roots: list[Path]) -> list[PEInfo]:
    out: list[PEInfo] = []
    for root in roots:
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if p.suffix not in PE_EXTS:
                continue
            info = read_pe(p)
            if info and info.dwf:
                out.append(info)
    return out


# -----------------------------------------------------------------------
# Auto-resolve an address → which binary?  Strategy:
#
#   For each PE we know about, compute the on-disk VA range for code
#   (image_base + code_lo .. image_base + code_hi) and a couple of
#   common relocated bases.  An address that falls in any of those
#   ranges names that PE.
#
# Heuristic — not perfect, but cheap and good enough for the common
# case where a serial log gave you a runtime address and you want to
# know which binary it came from.
# -----------------------------------------------------------------------
KNOWN_RELOCATIONS = [
    0x00000000,       # not relocated
    0x00C00000,       # link.exe lands here on guest (0x00400000 → 0x01000000)
]


@dataclass
class Match:
    pe:          PEInfo
    runtime_lo:  int
    runtime_hi:  int
    slide:       int


def candidates_for(addr: int, pes: list[PEInfo]) -> list[Match]:
    matches: list[Match] = []
    for pe in pes:
        for slide in KNOWN_RELOCATIONS:
            lo = pe.image_base + pe.code_lo + slide
            hi = pe.image_base + pe.code_hi + slide
            if lo <= addr < hi:
                matches.append(Match(pe, lo, hi, slide))
    return matches


# -----------------------------------------------------------------------
# Symbolicate via addr2line against a .dwf.  The .dwf has VAs at
# image_base + RVA, so we feed (addr - slide) — i.e. what the address
# would have been at native PE base.
# -----------------------------------------------------------------------
def addr2line(dwf: Path, va: int) -> Optional[str]:
    if not shutil.which("addr2line"):
        return None
    try:
        r = subprocess.run(
            ["addr2line", "-e", str(dwf), "-f", "-C", f"0x{va:x}"],
            capture_output=True, text=True, timeout=10,
        )
    except (subprocess.SubprocessError, FileNotFoundError):
        return None
    if r.returncode != 0:
        return None
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    if len(lines) >= 2:
        return f"{lines[0]} at {lines[1]}"
    return r.stdout.strip() or None


# -----------------------------------------------------------------------
# Source-snippet helper.  addr2line tells us "<func> at <file>:<line>"
# but NT 3.5 source files are uppercase 8.3 on disk and the CodeView
# records carry mixed-case names — case-sensitive opens fail.  Try the
# uppercase variant too before giving up.  Returns a short snippet
# (3 lines before/after the offending line) for inline display.
# -----------------------------------------------------------------------
def source_snippet(addr2line_out: str, ctx_before: int = 3,
                   ctx_after: int = 2) -> Optional[str]:
    if " at " not in addr2line_out:
        return None
    _, _, where = addr2line_out.partition(" at ")
    where = where.strip()
    if ":" not in where:
        return None
    path, _, lineno_s = where.rpartition(":")
    try:
        lineno = int(lineno_s)
    except ValueError:
        return None

    # Try the path verbatim, then with the basename uppercased (NT 3.5
    # source on disk is 8.3 uppercase, CV records mixed-case).
    p = Path(path)
    candidates = [p]
    if p.name != p.name.upper():
        candidates.append(p.with_name(p.name.upper()))
    if p.name != p.name.lower():
        candidates.append(p.with_name(p.name.lower()))

    chosen = None
    for c in candidates:
        if c.exists():
            chosen = c
            break
    if chosen is None:
        return None

    try:
        lines = chosen.read_text(errors="replace").splitlines()
    except OSError:
        return None
    if not (1 <= lineno <= len(lines)):
        return None

    lo = max(1, lineno - ctx_before)
    hi = min(len(lines), lineno + ctx_after)
    out = []
    for i in range(lo, hi + 1):
        marker = " →  " if i == lineno else "    "
        out.append(f"        {i:5d}{marker}{lines[i - 1].rstrip()}")
    return "\n".join(out)


# -----------------------------------------------------------------------
# Annotation for a faulting-address value.
# -----------------------------------------------------------------------
def annotate_value(v: int) -> Optional[tuple[str, str]]:
    return HEAP_PATTERNS.get(v)


# -----------------------------------------------------------------------
# Pretty-print one resolved address.  Returns the gdb command(s)
# appropriate for breaking at this site, or None if no useful action.
# -----------------------------------------------------------------------
def report_address(label: str, addr: int, pes: list[PEInfo],
                   indent: str = "  ") -> list[str]:
    print(f"{indent}{label}: 0x{addr:08x}")
    if addr == 0:
        return []
    is_kernel = addr >= 0x80000000
    matches = candidates_for(addr, pes)
    if not matches:
        if is_kernel:
            print(f"{indent}  → kernel-mode address, no .dwf match in tree")
            print(f"{indent}    (try `make -C src gdb` then `info line *0x{addr:x}`)")
        else:
            print(f"{indent}  → user-mode address, no .dwf match — try a manual")
            print(f"{indent}    `add-symbol-file <path>.dwf -o <slide>` once you")
            print(f"{indent}    know which process this is from")
        return []

    cmds: list[str] = []
    for m in matches:
        rva = (addr - m.runtime_lo) + m.pe.code_lo
        # va in the .dwf's address space:
        dwf_va = m.pe.image_base + rva
        sym = addr2line(m.pe.dwf, dwf_va) if m.pe.dwf else None
        slide_note = (f" (relocated +0x{m.slide:x})" if m.slide else "")
        print(f"{indent}  → {m.pe.name}{slide_note}")
        if sym and "??" not in sym:
            print(f"{indent}      {sym}")
            snippet = source_snippet(sym)
            if snippet:
                print(snippet)
        else:
            print(f"{indent}      (no symbol resolved at .dwf VA 0x{dwf_va:x})")
        if m.pe.dwf:
            try:
                rel = m.pe.dwf.relative_to(REPO)
            except ValueError:
                rel = m.pe.dwf
            cmds.append(f"add-symbol-file {rel} -o 0x{m.slide:x}")
            cmds.append(f"hbreak *0x{addr:x}")
    return cmds


# -----------------------------------------------------------------------
# Format one crash record.
# -----------------------------------------------------------------------
def report_umode(m: dict, pes: list[PEInfo], gdb_cmds: list[str]) -> None:
    chance = m["chance"]
    code   = int(m["code"], 16)
    addr   = int(m["addr"], 16)
    p0     = int(m["p0"], 16)
    p1     = int(m["p1"], 16)
    eip    = int(m["eip"], 16) if m["eip"] else addr
    write  = (p0 != 0)

    print(f"\n=== UMODE EXC ({chance}-chance) ===")
    print(f"  code: 0x{code:08x}  ({'ACCESS_VIOLATION' if code == 0xc0000005 else 'see status table'})")
    print(f"  fault: {'WRITE' if write else 'READ'} to 0x{p1:08x}")
    pat = annotate_value(p1)
    if pat:
        kind, why = pat
        print(f"    → {kind}")
        print(f"      ({why})")

    cmds = report_address("eip", eip, pes)
    gdb_cmds.extend(cmds)
    if cmds:
        # Bonus: a conditional breakpoint that filters to this exact
        # fault preimage.  Useful when the address is hot and you only
        # want to stop on the bad call.
        gdb_cmds.append(f"# alternative — only stop on the actual fault preimage:")
        gdb_cmds.append(f"# delete <bp-num>")
        gdb_cmds.append(f"# hbreak *0x{eip:x} if $eax == 0x{p1:x} || $edx == 0x{p1:x} || $ecx == 0x{p1:x}")


def report_stop(m: dict, pes: list[PEInfo], gdb_cmds: list[str]) -> None:
    code = int(m["code"], 16)
    args = [int(m[k], 16) for k in ("a0", "a1", "a2", "a3")]
    name = BUGCHECK_NAMES.get(code, "?")
    print(f"\n=== STOP 0x{code:08x} ===")
    print(f"  {name}")
    print(f"  args: 0x{args[0]:08x}  0x{args[1]:08x}  0x{args[2]:08x}  0x{args[3]:08x}")

    if code == 0x1E:                          # KMODE_EXCEPTION_NOT_HANDLED
        excode, excaddr, _, _ = args
        print(f"  exception: code=0x{excode:08x} at 0x{excaddr:08x}")
        cmds = report_address("ExceptionAddress", excaddr, pes)
        gdb_cmds.extend(cmds)
    elif code == 0x50:                        # PAGE_FAULT_IN_NONPAGED_AREA
        va, write, _, _ = args
        print(f"  faulting VA: 0x{va:08x} ({'write' if write else 'read'})")
    elif code == 0xCAFE5E1F:
        _, reason, badval, exc = args
        reasons = {
            1: "chain too deep (>64)",
            2: "frame outside thread stack",
            3: "Handler not in any loaded module",
            4: "Handler points into pool",
        }
        print(f"  guard reason: {reason} ({reasons.get(reason, '?')})")
        print(f"  bad value: 0x{badval:08x}")
        print(f"  original exception: 0x{exc:08x}")
        print(f"  → see SEH-PROBLEMS.md for disambiguation rubric")
    gdb_cmds.append(f"# break before bugcheck:")
    gdb_cmds.append(f"hbreak KeBugCheckEx")


def report_short_stop(m: dict) -> None:
    code = int(m["code"], 16)
    msg  = m["msg"].strip()
    name = BUGCHECK_NAMES.get(code, "?")
    print(f"\n=== STOP 0x{code:08x} (short form) ===")
    print(f"  {name}")
    print(f"  message: {msg}")


# -----------------------------------------------------------------------
# Drive everything.
# -----------------------------------------------------------------------
def parse_log(text: str, pes: list[PEInfo]) -> list[str]:
    gdb_cmds: list[str] = []
    seen_long_stop = False
    for line in text.splitlines():
        m = RX_UMODE.search(line)
        if m:
            report_umode(m.groupdict(), pes, gdb_cmds)
            continue
        m = RX_STOP_LONG.search(line)
        if m:
            report_stop(m.groupdict(), pes, gdb_cmds)
            seen_long_stop = True
            continue
        m = RX_FATAL.search(line)
        if m:
            report_stop(m.groupdict(), pes, gdb_cmds)
            seen_long_stop = True
            continue
        m = RX_STOP_SHORT.match(line.strip())
        if m and not seen_long_stop:
            report_short_stop(m.groupdict())
            continue
    return gdb_cmds


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?",
                    help="serial-log path; reads stdin if omitted")
    ap.add_argument("--addr", type=lambda s: int(s, 0),
                    help="symbolicate one address (no log parse)")
    ap.add_argument("--binary",
                    help="binary path for --addr mode (skip auto-discover)")
    ap.add_argument("--base", type=lambda s: int(s, 0),
                    help="runtime base for --addr/--binary mode")
    ap.add_argument("--scan-root", action="append", default=[],
                    help="extra root to scan for PE+.dwf (default: src/)")
    args = ap.parse_args()

    roots = [REPO / "src"]
    roots += [Path(r) for r in args.scan_root]
    print(f"# decode_av.py — scanning {len(roots)} root(s) for .dwf...",
          file=sys.stderr)
    pes = scan_tree(roots)
    print(f"# found {len(pes)} PE files with sibling .dwf", file=sys.stderr)

    if args.addr is not None:
        gdb_cmds: list[str] = []
        if args.binary and args.base is not None:
            info = read_pe(Path(args.binary))
            if not info or not info.dwf:
                print("error: --binary has no .dwf next to it", file=sys.stderr)
                return 2
            slide = args.base - info.image_base
            dwf_va = args.addr - slide
            sym = addr2line(info.dwf, dwf_va)
            print(f"address: 0x{args.addr:08x}")
            print(f"  binary: {info.path}")
            print(f"  base:   0x{args.base:08x}  (slide 0x{slide:x})")
            print(f"  symbol: {sym or '<unresolved>'}")
            try:
                rel = info.dwf.relative_to(REPO)
            except ValueError:
                rel = info.dwf
            print()
            print("paste-into-gdb:")
            print(f"    add-symbol-file {rel} -o 0x{slide:x}")
            print(f"    hbreak *0x{args.addr:x}")
            return 0
        cmds = report_address("addr", args.addr, pes)
        gdb_cmds.extend(cmds)
        if gdb_cmds:
            print("\npaste-into-gdb:")
            for c in gdb_cmds:
                print(f"    {c}")
        return 0

    if args.log:
        text = Path(args.log).read_text(errors="replace")
    else:
        text = sys.stdin.read()

    gdb_cmds = parse_log(text, pes)
    if gdb_cmds:
        print("\npaste-into-gdb:")
        for c in gdb_cmds:
            print(f"    {c}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
