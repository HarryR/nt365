#!/usr/bin/env python3
"""
gen_objects.py — Generate obj/_objects.mac files from NT SOURCES.

BUILD.EXE normally produces these; we do it ourselves by parsing the
SOURCES file (and any !include'd fragments) to extract the list of
source files, then emitting an `386_OBJECTS=...` list of .obj paths.

Handles:
  - `!include ..\\sources.inc` directives (case-insensitive on Linux)
  - `SOURCES=$(SOURCES) ..\\extra.c` appends
  - Line-continuation backslashes
  - CRLF line endings
  - Arch-specific source dirs (COMP/I386/SOURCES, COMP/../I386/SOURCES)
  - `i386_SOURCES=` in the main SOURCES

Usage: gen_objects.py <component_dir>
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path


def resolve_ci(path: Path) -> Path | None:
    """Case-insensitive path resolution for Linux fs."""
    if path.exists():
        return path
    parent = path.parent
    if not parent.exists():
        return None
    name_lower = path.name.lower()
    for entry in parent.iterdir():
        if entry.name.lower() == name_lower:
            return entry
    return None


def flatten(path: Path, seen: set[Path] | None = None) -> list[str]:
    """Read `path`, inlining any `!include ..\\foo.inc` directives.
    Returns the logically-flattened list of lines."""
    seen = seen or set()
    path = path.resolve()
    if path in seen:
        return []
    seen.add(path)

    out: list[str] = []
    with path.open("rb") as f:
        raw = f.read().decode("latin-1", errors="replace")
    for line in raw.splitlines():
        m = re.match(r"^!\s*include\s+(.+?)\s*$", line)
        if m:
            inc = m.group(1).strip().strip('"').replace("\\", "/")
            inc_path = path.parent / inc
            resolved = resolve_ci(inc_path)
            if resolved:
                out.extend(flatten(resolved, seen))
            # missing include: silently skip (matches nmake's !include if exist)
            continue
        out.append(line)
    return out


def extract_var(lines: list[str], varname: str) -> list[str]:
    """Pull every `VAR= ...` definition out of `lines` (honoring
    backslash continuations) and return the concatenated list of tokens.
    Handles `VAR=$(VAR) extra` by stripping the self-reference."""
    tokens: list[str] = []
    # Case-insensitive: real NT SOURCES files mix `I386_SOURCES=` and
    # `i386_SOURCES=` across components (e.g. NTOS/VIDEO/PORT/I386 uses
    # uppercase, most drivers lowercase). nmake is insensitive; we match it.
    pattern = re.compile(rf"^{re.escape(varname)}\s*=\s*(.*)$", re.IGNORECASE)
    self_ref = re.compile(rf"^\$\({re.escape(varname)}\)\s*", re.IGNORECASE)

    in_var = False
    for line in lines:
        if not in_var:
            m = pattern.match(line)
            if not m:
                continue
            body = m.group(1)
            body = self_ref.sub("", body)
            in_var = True
        else:
            body = line

        cont = body.rstrip().endswith("\\")
        if cont:
            body = body.rstrip()[:-1]
        for tok in body.split():
            tokens.append(tok)
        if not cont:
            in_var = False
    return tokens


def src_to_obj(src: str) -> str | None:
    """`..\\i386\\foo.c` → `obj\\i386\\foo.obj`. Returns None for .rc/.res
    (resource files, handled separately by MAKEFILE.DEF)."""
    base = os.path.basename(src.replace("\\", "/"))
    stem, _, ext = base.rpartition(".")
    if ext.lower() in ("rc", "res", "mc"):
        return None
    return f"obj\\i386\\{stem}.obj"


def find_i386_sources(comp_dir: Path) -> Path | None:
    """Look for arch-specific SOURCES at known spots."""
    for cand in (
        comp_dir / "I386" / "SOURCES",
        comp_dir.parent / "I386" / "SOURCES",
        comp_dir / "i386" / "SOURCES",
        comp_dir.parent / "i386" / "SOURCES",
    ):
        resolved = resolve_ci(cand)
        if resolved and resolved.is_file():
            return resolved
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"Usage: {argv[0]} <component_dir>", file=sys.stderr)
        return 1

    comp_dir = Path(argv[1]).resolve()
    sources = resolve_ci(comp_dir / "SOURCES")
    if not sources or not sources.is_file():
        print(f"ERROR: SOURCES not found: {comp_dir / 'SOURCES'}", file=sys.stderr)
        return 1

    lines = flatten(sources)
    srcs = extract_var(lines, "SOURCES")

    # i386_SOURCES: either in a sibling/arch SOURCES file, or inline in main
    i386_sources_file = find_i386_sources(comp_dir)
    i386_srcs: list[str]
    if i386_sources_file:
        i386_lines = flatten(i386_sources_file)
        i386_srcs = extract_var(i386_lines, "SOURCES")
        i386_srcs.extend(extract_var(i386_lines, "i386_SOURCES"))
    else:
        i386_srcs = extract_var(lines, "i386_SOURCES")

    all_srcs = srcs + i386_srcs

    obj_dir = comp_dir / "obj"
    (obj_dir / "i386").mkdir(parents=True, exist_ok=True)

    objs: list[str] = []
    for s in all_srcs:
        obj = src_to_obj(s)
        if obj:
            objs.append(obj)

    out_path = obj_dir / "_objects.mac"
    with out_path.open("w") as f:
        f.write("#\n# _objects.mac - generated by gen_objects.py\n#\n\n")
        if objs:
            f.write("386_OBJECTS=" + objs[0])
            for o in objs[1:]:
                f.write(f" \\\n    {o}")
            f.write("\n")
        else:
            f.write("386_OBJECTS=\n")
        f.write("\n")

    print(f"Generated {out_path} with {len(all_srcs)} source files "
          f"({len(objs)} objects)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
