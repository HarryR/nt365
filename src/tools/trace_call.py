#!/usr/bin/env python3
"""
trace_call.py - Trace return values from every `call; test; je fail` pattern
                inside an NT kernel function, in a single QEMU run.

For each `call X; <flag-test>; j? fail_label` triple found in the target
function's disassembly, sets a GDB breakpoint at the branch instruction,
captures EAX, and reports:

    +0x37   e8 00 12 00 00   call  NtOpenKey               eax=0xC0000034  [STATUS_OBJECT_NAME_NOT_FOUND]
    +0x45   e8 00 13 00 00   call  CmpInitializeRegistry   eax=0x00000000  [OK]

Usage:
    tools/trace_call.py --addr 0x801b7844 [--exe PATH]
    tools/trace_call.py --addr 0x801b7844 --boot   # auto-start QEMU

Requires:
    - objdump (from binutils) for disassembly
    - ntoskrnl.gdb (produced by tools/pe2gdb.py) for symbol names
    - An active QEMU instance with -gdb tcp::1234, OR --boot to start one
"""

import argparse
import os
import re
import shlex
import struct
import subprocess
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# NTSTATUS table (loaded from NTSTATUS.H)
# ---------------------------------------------------------------------------

def load_ntstatus(path: str = "NT/PUBLIC/SDK/INC/NTSTATUS.H") -> dict[int, str]:
    table: dict[int, str] = {}
    if not os.path.exists(path):
        return table
    pat = re.compile(
        r"#define\s+(\w+)\s+\(\(NTSTATUS\)(0x[0-9a-fA-F]+)L?\)"
    )
    with open(path) as f:
        for line in f:
            m = pat.search(line)
            if m:
                table[int(m.group(2), 16) & 0xFFFFFFFF] = m.group(1)
    return table


def decode_status(value: int, table: dict[int, str]) -> str:
    """Decode an NTSTATUS value. Returns empty string if value doesn't look like
    a status code (e.g. BOOLEAN, pointer)."""
    v = value & 0xFFFFFFFF
    if v in table:
        return table[v]
    if v == 0:
        return "STATUS_SUCCESS"
    # Only treat values with error bits set (high nibble 0xC) as unknown errors
    if 0xC0000000 <= v < 0xC1000000:
        return f"(unknown NTSTATUS 0x{v:08X})"
    return ""


# ---------------------------------------------------------------------------
# Symbol table (from ntoskrnl.gdb produced by pe2gdb.py)
# ---------------------------------------------------------------------------

def load_symbols(path: str = "ntoskrnl.gdb") -> list[tuple[int, str]]:
    syms: list[tuple[int, str]] = []
    if not os.path.exists(path):
        return syms
    pat = re.compile(r"set\s+\$(\w+)\s*=\s*(0x[0-9a-fA-F]+)")
    with open(path) as f:
        for line in f:
            m = pat.match(line)
            if m:
                syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    return syms


def symbol_at(addr: int, syms: list[tuple[int, str]]) -> str:
    """Nearest preceding exported symbol + offset."""
    best: tuple[int, str] | None = None
    for a, n in syms:
        if a <= addr:
            best = (a, n)
        else:
            break
    if best is None:
        return f"0x{addr:08X}"
    off = addr - best[0]
    if off == 0:
        return best[1]
    # Only show offset if it's small enough to be meaningful
    if off > 0x4000:
        return f"0x{addr:08X}"
    return f"{best[1]}+0x{off:x}"


# ---------------------------------------------------------------------------
# PE image helpers
# ---------------------------------------------------------------------------

class PeImage:
    def __init__(self, path: str):
        self.path = path
        self.data = Path(path).read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        self.image_base = struct.unpack_from("<I", self.data, pe + 0x34)[0]
        num_secs = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", self.data, pe + 0x14)[0]
        sec_start = pe + 0x18 + opt_size
        self.sections: list[tuple[int, int, int]] = []  # (vaddr, vsize, raddr)
        for i in range(num_secs):
            so = sec_start + i * 40
            vaddr = struct.unpack_from("<I", self.data, so + 12)[0]
            vsize = struct.unpack_from("<I", self.data, so + 8)[0]
            raddr = struct.unpack_from("<I", self.data, so + 20)[0]
            self.sections.append((vaddr, vsize, raddr))

    def va_to_foff(self, va: int) -> int | None:
        rva = va - self.image_base
        for vaddr, vsize, raddr in self.sections:
            if vaddr <= rva < vaddr + vsize:
                return raddr + (rva - vaddr)
        return None

    def read_va(self, va: int, length: int) -> bytes:
        foff = self.va_to_foff(va)
        if foff is None:
            raise ValueError(f"VA 0x{va:08X} not in any section")
        return self.data[foff : foff + length]


# ---------------------------------------------------------------------------
# Disassembly (via objdump) + call-branch pattern matching
# ---------------------------------------------------------------------------

ObjdumpLine = tuple[int, str, str]  # (addr, mnemonic, operands)


def disassemble(pe: PeImage, start_va: int, length: int = 0x1000) -> list[ObjdumpLine]:
    """Disassemble `length` bytes starting at `start_va` using objdump."""
    blob = pe.read_va(start_va, length)
    with open("/tmp/tc_blob.bin", "wb") as f:
        f.write(blob)
    out = subprocess.check_output(
        ["objdump", "-D", "-b", "binary", "-m", "i386", "-M", "intel",
         f"--adjust-vma={hex(start_va)}", "/tmp/tc_blob.bin"],
        stderr=subprocess.DEVNULL,
    ).decode()

    lines: list[ObjdumpLine] = []
    line_pat = re.compile(r"^\s*([0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2}\s+)+\s+(\w+)\s*(.*)")
    for raw in out.splitlines():
        m = line_pat.match(raw)
        if m:
            addr = int(m.group(1), 16)
            lines.append((addr, m.group(2), m.group(3).strip()))
    return lines


def find_function_end(lines: list[ObjdumpLine]) -> int:
    """Return index of last instruction in the function (a `ret` followed by
    padding/nops or int3)."""
    for i, (_addr, mnem, _ops) in enumerate(lines):
        if mnem in ("ret", "retf") and i + 1 < len(lines):
            nxt_mnem = lines[i + 1][1]
            if nxt_mnem in ("int3", "nop"):
                return i
    # Fallback: last ret in buffer
    for i in range(len(lines) - 1, -1, -1):
        if lines[i][1] in ("ret", "retf"):
            return i
    return len(lines) - 1


class CallSite:
    __slots__ = ("branch_addr", "branch_mnem", "branch_target",
                 "call_addr", "call_target", "test_kind")

    def __init__(self, branch_addr: int, branch_mnem: str, branch_target: int,
                 call_addr: int, call_target: int, test_kind: str):
        self.branch_addr = branch_addr
        self.branch_mnem = branch_mnem
        self.branch_target = branch_target
        self.call_addr = call_addr
        self.call_target = call_target
        self.test_kind = test_kind


# Conditional branches that typically gate "call failed" paths
COND_BRANCHES = {"je", "jne", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe",
                 "js", "jns", "jz", "jnz"}


def parse_call_sites(lines: list[ObjdumpLine]) -> list[CallSite]:
    """Walk the disassembly; for each conditional branch, look back for the
    nearest `test`/`cmp` on eax/al and, before that, the nearest `call`."""

    # Only parse bare `call ADDR` (not `call [mem]`); target is a hex literal
    call_re = re.compile(r"0x([0-9a-fA-F]+)\s*$")

    sites: list[CallSite] = []
    for i, (addr, mnem, ops) in enumerate(lines):
        if mnem not in COND_BRANCHES:
            continue

        # Branch target
        m = call_re.search(ops)
        if not m:
            continue
        branch_target = int(m.group(1), 16)

        # Walk back up to ~8 instructions for the nearest test/cmp
        test_idx = None
        for j in range(i - 1, max(i - 10, -1), -1):
            _a, jm, jo = lines[j]
            if jm in ("test", "cmp") and ("eax" in jo or "al," in jo or ",al" in jo):
                test_idx = j
                break
            if jm == "call":
                # Call without an intervening test — unusual, abort
                break
        if test_idx is None:
            continue

        # Walk back further for the nearest call
        call_idx = None
        for j in range(test_idx - 1, max(test_idx - 20, -1), -1):
            _a, jm, jo = lines[j]
            if jm == "call":
                call_idx = j
                break
            # Stop if we cross another branch (new basic block)
            if jm in COND_BRANCHES or jm in ("jmp", "ret", "retf"):
                break
        if call_idx is None:
            continue

        # Extract call target
        call_addr, _, call_ops = lines[call_idx]
        m = call_re.search(call_ops)
        if not m:
            continue  # Indirect call (`call [mem]`) — can't resolve statically
        call_target = int(m.group(1), 16)

        sites.append(CallSite(
            branch_addr=addr,
            branch_mnem=mnem,
            branch_target=branch_target,
            call_addr=call_addr,
            call_target=call_target,
            test_kind=lines[test_idx][1] + " " + lines[test_idx][2],
        ))

    return sites


# ---------------------------------------------------------------------------
# GDB driver
# ---------------------------------------------------------------------------

def run_gdb_trace(sites: list[CallSite], gdb_port: int = 1234,
                  settle: float = 1.0, total_timeout: float = 25.0) -> list[int | None]:
    """Attach GDB to a running QEMU, set breakpoints at each branch, run until
    each fires. Returns a list of EAX values parallel to `sites`."""

    # Build the GDB script
    lines = [
        f"target remote :{gdb_port}",
    ]
    for s in sites:
        # Use software breakpoints (`break *ADDR`) to avoid the 4-hbreak limit
        lines.append(f"break *0x{s.branch_addr:x}")

    # Continue + capture in a loop
    lines += [
        f"set $remaining = {len(sites)}",
        "while ($remaining > 0)",
        "  c",
        "  printf \"__HIT pc=%#x eax=%#x\\n\", $pc, $eax",
        "  set $remaining = $remaining - 1",
        "end",
        "quit",
    ]
    script = "\n".join(lines)
    Path("/tmp/tc_gdb.gdb").write_text(script)

    try:
        proc = subprocess.run(
            ["gdb", "-batch", "-nx", "-x", "/tmp/tc_gdb.gdb"],
            capture_output=True, text=True, timeout=total_timeout,
        )
        output = proc.stdout + proc.stderr
    except subprocess.TimeoutExpired as e:
        output = (e.stdout or b"").decode() + (e.stderr or b"").decode()

    # Parse "__HIT pc=0x... eax=0x..."
    hits: dict[int, int] = {}
    hit_pat = re.compile(r"__HIT pc=(0x[0-9a-fA-F]+) eax=(0x[0-9a-fA-F]+)")
    for line in output.splitlines():
        m = hit_pat.search(line)
        if m:
            pc = int(m.group(1), 16)
            eax = int(m.group(2), 16)
            hits[pc] = eax  # last hit wins if breakpoint fires multiple times

    return [hits.get(s.branch_addr) for s in sites]


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def report(sites: list[CallSite], eaxes: list[int | None],
           syms: list[tuple[int, str]], status_table: dict[int, str],
           func_start: int) -> None:
    """Print one row per traced call site. The branch direction is ambiguous
    without more context (could be "if success jump forward" or "if fail
    jump to handler"), so we just report the raw return value + whether the
    branch was taken, and let the reader interpret."""
    for s, eax in zip(sites, eaxes):
        off = s.branch_addr - func_start
        target_name = symbol_at(s.call_target, syms)
        target_off = s.branch_target - func_start
        if eax is None:
            eax_str = "not hit"
            meaning = ""
            taken = ""
        else:
            eax_str = f"0x{eax:08X}"
            decoded = decode_status(eax, status_table)
            meaning = f"  [{decoded}]" if decoded else ""
            if branch_taken(s, eax):
                # Show where the branch goes to: forward = skip, backward = loop
                direction = "→+0x%04x" % target_off if target_off > 0 else "→-0x%04x" % -target_off
                taken = f"  branch taken ({direction})"
            else:
                taken = "  fall-through"

        print(f"  +0x{off:04x}  {s.branch_mnem:4s} after call "
              f"{target_name:38s}  eax={eax_str}{meaning}{taken}")


def branch_taken(s: CallSite, eax: int) -> bool:
    """Approximate whether the branch fires given EAX as the compare operand.
    Only correct for the common `test eax,eax; j?` and `cmp eax,0; j?`."""
    sign = bool(eax & 0x80000000)
    zero = eax == 0
    m = s.branch_mnem
    if m in ("je", "jz"):  return zero
    if m in ("jne", "jnz"): return not zero
    if m == "js":  return sign
    if m == "jns": return not sign
    if m == "jl":  return sign  # signed less: SF != OF, OF=0 after test → SF=1
    if m == "jge": return not sign
    if m == "jg":  return not sign and not zero
    if m == "jle": return sign or zero
    return False


# ---------------------------------------------------------------------------
# Optional: auto-launch QEMU via boot.sh --gdb
# ---------------------------------------------------------------------------

def launch_boot() -> subprocess.Popen:
    return subprocess.Popen(
        ["timeout", "60", "bash", "boot.sh", "--gdb"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--addr", required=True,
                    help="Function entry address, e.g. 0x801b7844")
    ap.add_argument("--exe", default="NT/PRIVATE/NTOS/INIT/UP/obj/i386/ntoskrnl.exe",
                    help="PE image to analyze (default: ntoskrnl.exe)")
    ap.add_argument("--symbols", default="ntoskrnl.gdb",
                    help="Symbol file from pe2gdb.py (default: ntoskrnl.gdb)")
    ap.add_argument("--ntstatus", default="NT/PUBLIC/SDK/INC/NTSTATUS.H",
                    help="Path to NTSTATUS.H for status decoding")
    ap.add_argument("--boot", action="store_true",
                    help="Start QEMU via boot.sh --gdb before tracing")
    ap.add_argument("--port", type=int, default=1234,
                    help="GDB stub TCP port (default: 1234)")
    ap.add_argument("--disasm-size", type=lambda x: int(x, 0), default=0x1000,
                    help="Bytes to disassemble from --addr (default: 0x1000)")
    args = ap.parse_args()

    func_start = int(args.addr, 0)

    pe = PeImage(args.exe)
    syms = load_symbols(args.symbols)
    status_table = load_ntstatus(args.ntstatus)

    print(f"Disassembling {symbol_at(func_start, syms)} at 0x{func_start:08X}...")
    lines = disassemble(pe, func_start, args.disasm_size)
    end_idx = find_function_end(lines)
    func_lines = lines[: end_idx + 1]
    print(f"  {len(func_lines)} instructions spanning "
          f"0x{func_lines[0][0]:08X} .. 0x{func_lines[-1][0]:08X}")

    sites = parse_call_sites(func_lines)
    if not sites:
        print("No `call; test/cmp; j?` patterns found.")
        return

    print(f"\nFound {len(sites)} conditional call-return branches:")
    for s in sites:
        off = s.branch_addr - func_start
        tgt = symbol_at(s.call_target, syms)
        print(f"  +0x{off:04x}  {s.branch_mnem} after call {tgt}")

    # Start QEMU if requested
    qemu = None
    if args.boot:
        print("\nStarting QEMU via boot.sh --gdb ...")
        qemu = launch_boot()
        time.sleep(2.0)

    try:
        print(f"\nAttaching GDB to :{args.port} and tracing ...")
        eaxes = run_gdb_trace(sites, gdb_port=args.port)
    finally:
        if qemu is not None:
            qemu.terminate()
            try:
                qemu.wait(timeout=3)
            except subprocess.TimeoutExpired:
                qemu.kill()

    print("\n=== Trace report ===")
    report(sites, eaxes, syms, status_table, func_start)


if __name__ == "__main__":
    main()
