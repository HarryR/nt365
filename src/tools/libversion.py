"""Shared build-stamp logic for MicroNT / Windows NT 3.65.

Single source of truth for version numbers across the build. Both
stamp-version.py (writes NTVERP.H) and mkhive.py (consumes it when
populating the SOFTWARE hive) import this module.

NT 3.65 ships as a continuous release: at most one build per day, so
the release identity is (date, git sha). The resource VS_FIXEDFILEINFO
tuple is four USHORTs, so the date is split as YYMM / DD — both slots
stay under 65535 and the tuple sorts chronologically.

Layout of NTVERP.H after stamping:

    VER_PRODUCTBUILD        <YYMM>
    VER_PRODUCTBUILD_QFE    <DD>
    VER_PRODUCTVERSION      3,65,VER_PRODUCTBUILD,VER_PRODUCTBUILD_QFE
    VER_PRODUCTVERSION_STR  "3.65"
    VER_PRODUCTBETA_STR     "" | "<sha>" | "rc-<sha>" | "dev-<sha>"
"""

from __future__ import annotations

import datetime as _dt
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

PRODUCT_MAJOR = 3
PRODUCT_MINOR = 65
PRODUCT_NAME_STR = "Microsoft\\256 Windows NT(TM) 365 Operating System"
COMPANY_NAME_STR = "MicroNT Project"

CHANNELS = ("release", "rc", "dev")


def _repo_root() -> Path:
    # src/tools/libversion.py -> repo root is two parents up.
    return Path(__file__).resolve().parents[2]


NTVERP_PATH = _repo_root() / "src" / "NT" / "PUBLIC" / "SDK" / "INC" / "NTVERP.H"


@dataclass(frozen=True)
class BuildStamp:
    date: _dt.date
    sha: str
    channel: str

    def __post_init__(self) -> None:
        if self.channel not in CHANNELS:
            raise ValueError(f"channel must be one of {CHANNELS}, got {self.channel!r}")
        if not re.fullmatch(r"[0-9a-f]{7,40}", self.sha):
            raise ValueError(f"sha must be 7-40 lowercase hex chars, got {self.sha!r}")

    @property
    def yymm(self) -> int:
        return (self.date.year % 100) * 100 + self.date.month

    @property
    def dd(self) -> int:
        return self.date.day

    @property
    def version_str(self) -> str:
        return f"{PRODUCT_MAJOR}.{PRODUCT_MINOR}"

    @property
    def version_tuple(self) -> tuple[int, int, int, int]:
        return (PRODUCT_MAJOR, PRODUCT_MINOR, self.yymm, self.dd)

    @property
    def version_tuple_str(self) -> str:
        return ",".join(str(n) for n in self.version_tuple)

    @property
    def beta_str(self) -> str:
        # The sha anchors every build to a commit. Release drops the
        # channel prefix so the resource string stays clean.
        if self.channel == "release":
            return self.sha
        return f"{self.channel}-{self.sha}"

    @property
    def current_build_number(self) -> str:
        # Registry-facing YYYYMMDD; survives in a REG_SZ so width is free.
        return self.date.strftime("%Y%m%d")

    @property
    def legal_copyright_years(self) -> str:
        return f"1981-{self.date.year}"


def detect_from_git(root: Optional[Path] = None,
                    today: Optional[_dt.date] = None) -> BuildStamp:
    """Build a stamp from the current git HEAD.

    Channel resolution:
      - HEAD tagged `v3.65.YYYYMMDD`  -> release
      - HEAD tagged `rc-*`            -> rc
      - anything else                  -> dev
    """
    root = root or _repo_root()
    today = today or _dt.datetime.utcnow().date()

    def git(*args: str) -> str:
        return subprocess.check_output(
            ["git", *args], cwd=root, text=True,
            stderr=subprocess.DEVNULL,
        ).strip()

    sha = git("rev-parse", "--short=7", "HEAD")

    channel = "dev"
    try:
        tag = git("describe", "--exact-match", "--tags", "HEAD")
    except subprocess.CalledProcessError:
        tag = ""

    if re.fullmatch(r"v3\.65\.\d{8}", tag):
        channel = "release"
    elif tag.startswith("rc-"):
        channel = "rc"

    return BuildStamp(date=today, sha=sha, channel=channel)


_DEFINE_RE = re.compile(
    r"^(?P<prefix>#define\s+(?P<name>\w+)\s+)(?P<value>.*?)(?P<trailing>\s*)$"
)


def parse_ntverp(path: Optional[Path] = None) -> BuildStamp:
    """Reverse of `write_ntverp`: read back the stamp currently on disk."""
    path = path or NTVERP_PATH
    text = path.read_text()

    defines: dict[str, str] = {}
    for line in text.splitlines():
        m = _DEFINE_RE.match(line)
        if m:
            defines[m["name"]] = m["value"].strip()

    try:
        yymm = int(defines["VER_PRODUCTBUILD"])
        dd = int(defines["VER_PRODUCTBUILD_QFE"])
        beta = defines["VER_PRODUCTBETA_STR"].strip('"')
    except KeyError as e:
        raise RuntimeError(f"{path} missing {e.args[0]} — has it been stamped?")

    year = 2000 + yymm // 100
    month = yymm % 100
    date = _dt.date(year, month, dd)

    if beta.startswith("rc-"):
        channel, sha = "rc", beta[3:]
    elif beta.startswith("dev-"):
        channel, sha = "dev", beta[4:]
    else:
        channel, sha = "release", beta

    return BuildStamp(date=date, sha=sha, channel=channel)


# Lines in NTVERP.H driven by the stamp. Each entry: (macro name, formatter).
#
# MAJOR/MINOR are split out as separate scalars so plain C consumers can
# read them — VER_PRODUCTVERSION's comma-tuple is RC-only, can't be
# decomposed at the C preprocessor without variadic macros (and MSVC 8.x
# doesn't have those). BASE/CLIENT/BASEINIT.C consumes both for
# BaseWindowsMajorVersion / BaseWindowsMinorVersion in the kernel32
# DLL_PROCESS_ATTACH path.
_STAMPED_DEFINES: list[tuple[str, str]] = [
    # (macro, format string with {s} == BuildStamp)
    ("VER_PRODUCTMAJORVERSION", str(PRODUCT_MAJOR)),
    ("VER_PRODUCTMINORVERSION", str(PRODUCT_MINOR)),
    ("VER_PRODUCTBUILD",        "{s.yymm}"),
    ("VER_PRODUCTBUILD_QFE",    "{s.dd}"),
    ("VER_PRODUCTVERSION_STR",  '"{s.version_str}"'),
    ("VER_PRODUCTVERSION",      "{s.version_tuple_str}"),
    ("VER_PRODUCTBETA_STR",     '"{s.beta_str}"'),
    ("VER_LEGALCOPYRIGHT_YEARS", '"{s.legal_copyright_years}"'),
]


def write_ntverp(stamp: BuildStamp, path: Optional[Path] = None) -> bool:
    """Rewrite NTVERP.H in place. Returns True iff contents changed.

    Idempotent: running twice with the same stamp is a no-op.
    VER_PRODUCTBUILD_QFE is inserted immediately after VER_PRODUCTBUILD
    if it doesn't already exist (first-time migration from NT 3.5 layout).
    Line-ending style (CRLF vs LF) is preserved — the vintage NT source
    tree is CRLF and we don't want whole-file churn on every stamp.
    """
    path = path or NTVERP_PATH
    original_bytes = path.read_bytes()
    # Detect newline style from the file itself.
    newline = "\r\n" if b"\r\n" in original_bytes else "\n"
    original = original_bytes.decode()
    lines = original.splitlines(keepends=True)

    # Migrate legacy layout. Vintage NTVERP.H ships with some of the
    # macros we stamp (e.g. VER_PRODUCTBUILD_QFE, VER_LEGALCOPYRIGHT_YEARS
    # — that last one lives in COMMON.VER upstream). Seed any missing
    # macro with a placeholder so the regex-based replace below finds it
    # on the first stamping run. Anchor after VER_PRODUCTBUILD so the
    # stamped block stays contiguous.
    present = {
        m["name"] for m in (_DEFINE_RE.match(ln) for ln in lines) if m
    }
    missing_to_seed = [name for name, _ in _STAMPED_DEFINES
                       if name not in present]
    if missing_to_seed:
        for i, ln in enumerate(lines):
            if re.match(r"^#define\s+VER_PRODUCTBUILD\s", ln):
                for j, name in enumerate(missing_to_seed, start=1):
                    lines.insert(i + j, f"#define {name} 0{newline}")
                break

    wanted = {
        name: fmt.format(s=stamp) for name, fmt in _STAMPED_DEFINES
    }

    seen: set[str] = set()
    for i, ln in enumerate(lines):
        m = _DEFINE_RE.match(ln.rstrip("\n"))
        if not m or m["name"] not in wanted:
            continue
        name = m["name"]
        new_value = wanted[name]
        if m["value"].strip() == new_value:
            seen.add(name)
            continue
        if ln.endswith("\r\n"):
            nl = "\r\n"
        elif ln.endswith("\n"):
            nl = "\n"
        else:
            nl = ""
        lines[i] = f"{m['prefix']}{new_value}{nl}"
        seen.add(name)

    missing = set(wanted) - seen
    if missing:
        raise RuntimeError(
            f"{path}: expected macros not found (cannot stamp): "
            f"{sorted(missing)}"
        )

    new_text = "".join(lines)
    if new_text == original:
        return False
    path.write_bytes(new_text.encode())
    return True


__all__ = [
    "PRODUCT_MAJOR", "PRODUCT_MINOR", "CHANNELS",
    "BuildStamp", "detect_from_git", "parse_ntverp", "write_ntverp",
    "NTVERP_PATH",
]
