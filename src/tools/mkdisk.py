#!/usr/bin/env python3
"""
mkdisk.py - Build a raw disk image with MBR + FAT16 partition.

The output is suitable for `qemu-system-i386 -drive file=disk.raw,format=raw`.

High-level API:

    from mkdisk import DiskImage

    img = DiskImage(size_mb=16, signature=0x12345678)
    img.add_file("System32/ntoskrnl.exe", "path/to/ntoskrnl.exe")
    img.add_file("System32/hal.dll",      "path/to/hal.dll")
    img.add_file("System32/config/SYSTEM", "build/headless/SYSTEM")
    img.write("build/headless/esp.img")

On-disk layout (chosen for NT 3.5 compatibility):

    LBA 0           : MBR (partition table + disk signature at +0x1B8)
    LBA 1..2047     : empty (1 MB alignment — conventional)
    LBA 2048        : partition start = FAT16 boot sector (BPB)
    LBA 2049        : FAT #1
    LBA 2049+N      : FAT #2
    LBA 2049+2N     : root directory (fixed size, 512 entries = 32 sectors)
    LBA 2049+2N+32  : data area (clusters)

Only the subset of FAT16 actually exercised by fastfat during boot is
emitted: 8.3 short names, no long-filename entries, no timestamps worth
keeping, single-threaded contiguous allocation.
"""

import os
import struct
import time
from pathlib import Path


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SECTOR_SIZE          = 512
SECTORS_PER_CLUSTER  = 4          # 2 KB clusters
RESERVED_SECTORS     = 1
NUM_FATS             = 2
ROOT_DIR_ENTRIES     = 512        # FAT16 convention — fixed-size root
ROOT_DIR_SECTORS     = (ROOT_DIR_ENTRIES * 32) // SECTOR_SIZE   # 32

PARTITION_START_LBA  = 2048       # 1 MB — modern convention, QEMU-friendly
PARTITION_TYPE_FAT16 = 0x06       # FAT16 >= 32 MB (LBA semantics OK for us)

CLUSTER_FREE         = 0x0000
CLUSTER_BAD          = 0xFFF7
CLUSTER_EOC          = 0xFFFF     # any 0xFFF8..0xFFFF acts as end-of-chain

# Directory entry attributes
ATTR_READ_ONLY       = 0x01
ATTR_HIDDEN          = 0x02
ATTR_SYSTEM          = 0x04
ATTR_VOLUME_ID       = 0x08
ATTR_DIRECTORY       = 0x10
ATTR_ARCHIVE         = 0x20


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _encode_83(name: str) -> bytes:
    """Encode a filename in 8.3 format (11 bytes, space-padded).
    Accepts 'foo.bar' or 'foo' or 'FOOBAR.EXT'; case-folds to uppercase."""
    name = name.upper()
    if "." in name:
        stem, ext = name.rsplit(".", 1)
    else:
        stem, ext = name, ""
    if len(stem) > 8 or len(ext) > 3:
        raise ValueError(f"name {name!r} exceeds 8.3 limits")
    return (stem.ljust(8) + ext.ljust(3)).encode("ascii")


def _fat_time(when: float | None = None) -> tuple[int, int]:
    """Return (fat_time, fat_date) for the given UNIX timestamp (default: now)."""
    t = time.localtime(when if when is not None else time.time())
    fat_time = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    fat_date = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    return fat_time, fat_date


# ---------------------------------------------------------------------------
# File / directory tree (built in memory before layout)
# ---------------------------------------------------------------------------

class Entry:
    """One directory entry (file or subdirectory). Cluster numbers are assigned
    during layout in DiskImage.write()."""
    __slots__ = ("name", "is_dir", "data", "children", "attr",
                 "first_cluster", "mtime")

    name:          str               # 8.3 name
    is_dir:        bool
    data:          bytes              # file contents (for files)
    children:      "list[Entry]"      # (for directories)
    attr:          int
    first_cluster: int                # filled in by layout
    mtime:         float

    def __init__(self, name: str, is_dir: bool, *,
                 data: bytes = b"",
                 attr: int = 0,
                 mtime: float | None = None) -> None:
        # Validate/canonicalize 8.3 up front so errors surface at add-time
        _encode_83(name)
        self.name          = name.upper()
        self.is_dir        = is_dir
        self.data          = data
        self.children      = []
        self.attr          = attr
        self.first_cluster = 0
        self.mtime         = mtime if mtime is not None else time.time()


# ---------------------------------------------------------------------------
# Disk image builder
# ---------------------------------------------------------------------------

class DiskImage:
    """Build an MBR + FAT16 raw disk image with declarative file/dir adds."""

    def __init__(self, size_mb: int = 16,
                 signature: int = 0x4E544653,   # "NTFS" — arbitrary but stable
                 volume_label: str = "NT",
                 volume_serial: int | None = None) -> None:
        if size_mb < 4:
            raise ValueError("disk must be at least 4 MB")
        self.size_bytes   = size_mb * 1024 * 1024
        self.signature    = signature & 0xFFFFFFFF
        self.volume_label = volume_label.upper()[:11]
        self.volume_serial = (volume_serial if volume_serial is not None
                              else int(time.time()) & 0xFFFFFFFF)
        self.root = Entry("", is_dir=True, attr=ATTR_DIRECTORY)

    # --- Public: path-based file/dir adds ---------------------------------

    def mkdir(self, path: str) -> Entry:
        """Get or create a directory at `path` (forward-slash separated)."""
        parts = [p for p in path.replace("\\", "/").split("/") if p]
        d = self.root
        for part in parts:
            existing = next((c for c in d.children if c.name == part.upper()), None)
            if existing is None:
                new = Entry(part, is_dir=True, attr=ATTR_DIRECTORY)
                d.children.append(new)
                d = new
            else:
                if not existing.is_dir:
                    raise ValueError(f"{path!r}: {part!r} is a file, not a dir")
                d = existing
        return d

    def add_file(self, dest_path: str, src_path: str | os.PathLike[str]) -> Entry:
        """Copy `src_path` contents into the image at `dest_path`.
        Intermediate directories in dest_path are created automatically."""
        parts = [p for p in dest_path.replace("\\", "/").split("/") if p]
        if not parts:
            raise ValueError("empty dest_path")
        parent = self.mkdir("/".join(parts[:-1])) if len(parts) > 1 else self.root

        src = Path(src_path)
        data  = src.read_bytes()
        mtime = src.stat().st_mtime

        # Reject name clash
        leaf = parts[-1]
        if any(c.name == leaf.upper() for c in parent.children):
            raise ValueError(f"{dest_path}: already exists")

        entry = Entry(leaf, is_dir=False, data=data,
                      attr=ATTR_ARCHIVE, mtime=mtime)
        parent.children.append(entry)
        return entry

    def add_bytes(self, dest_path: str, data: bytes) -> Entry:
        """Add an in-memory blob as a file on the image."""
        parts = [p for p in dest_path.replace("\\", "/").split("/") if p]
        if not parts:
            raise ValueError("empty dest_path")
        parent = self.mkdir("/".join(parts[:-1])) if len(parts) > 1 else self.root
        leaf = parts[-1]
        if any(c.name == leaf.upper() for c in parent.children):
            raise ValueError(f"{dest_path}: already exists")
        entry = Entry(leaf, is_dir=False, data=bytes(data), attr=ATTR_ARCHIVE)
        parent.children.append(entry)
        return entry

    # --- Layout + write ---------------------------------------------------

    def write(self, out_path: str | os.PathLike[str]) -> None:
        """Compute cluster layout, then write the full raw image."""
        img = bytearray(self.size_bytes)

        # ----- Partition geometry -----
        total_sectors   = self.size_bytes // SECTOR_SIZE
        part_sectors    = total_sectors - PARTITION_START_LBA

        # Pick sectors-per-FAT large enough to cover every possible cluster
        # in the data region. Solve for N in:
        #   clusters       = (part_sectors - 1 - 2N - ROOT_DIR_SECTORS) / SPC
        #   fat_bytes_need = (clusters + 2) * 2  <=  N * SECTOR_SIZE
        # Iterate until stable (FAT size affects data size affects cluster count).
        spc = SECTORS_PER_CLUSTER
        sectors_per_fat = 1
        while True:
            data_sectors = part_sectors - RESERVED_SECTORS - NUM_FATS * sectors_per_fat - ROOT_DIR_SECTORS
            clusters     = data_sectors // spc
            needed_fat_bytes  = (clusters + 2) * 2
            needed_fat_sectors = (needed_fat_bytes + SECTOR_SIZE - 1) // SECTOR_SIZE
            if needed_fat_sectors <= sectors_per_fat:
                break
            sectors_per_fat = needed_fat_sectors
        total_clusters = clusters

        fat1_lba     = PARTITION_START_LBA + RESERVED_SECTORS
        fat2_lba     = fat1_lba + sectors_per_fat
        root_lba     = fat2_lba + sectors_per_fat
        data_lba     = root_lba + ROOT_DIR_SECTORS

        # ----- Assign clusters to every file and non-root directory -----
        # Cluster numbering starts at 2 (0 = "media descriptor", 1 = "EOC").
        # Root is at a fixed LBA so it doesn't get a cluster number.
        fat_entries = {0: 0xFFF8, 1: 0xFFFF}  # media descriptor + reserved
        next_cluster = 2

        def _emit_dir_data(entry: Entry, include_dotdot: bool,
                           parent_cluster: int) -> bytes:
            """Serialize a directory's entries (excluding root).
            `.` and `..` are prepended for non-root dirs."""
            out = bytearray()
            if include_dotdot:
                # "." entry → points at this directory itself
                out += _dir_entry(b".          ", ATTR_DIRECTORY, entry.first_cluster, 0, entry.mtime)
                # ".." entry → points at parent (0 if parent is root)
                out += _dir_entry(b"..         ", ATTR_DIRECTORY, parent_cluster, 0, entry.mtime)
            for child in entry.children:
                out += _dir_entry(_encode_83(child.name), child.attr,
                                  child.first_cluster,
                                  len(child.data) if not child.is_dir else 0,
                                  child.mtime)
            return bytes(out)

        def _alloc(n_bytes: int) -> tuple[int, int]:
            """Allocate a contiguous cluster chain big enough for n_bytes.
            Returns (first_cluster, cluster_count). Leaves fat_entries updated."""
            nonlocal next_cluster
            if n_bytes == 0:
                return 0, 0
            cluster_bytes = spc * SECTOR_SIZE
            n = (n_bytes + cluster_bytes - 1) // cluster_bytes
            first = next_cluster
            for i in range(n):
                this_cl = next_cluster + i
                if this_cl + 1 >= total_clusters + 2:
                    raise RuntimeError("disk full during layout")
                fat_entries[this_cl] = (this_cl + 1) if i + 1 < n else CLUSTER_EOC
            next_cluster += n
            return first, n

        # DFS over tree, allocating. For directories, we defer data emission
        # until all children have their cluster numbers (so the dir's entries
        # can reference them). That means two passes: assign first_cluster for
        # every entry, then emit dir data.
        def _assign(entry: Entry, parent_is_root: bool) -> None:
            if entry.is_dir:
                if entry is not self.root:
                    # Non-root dirs get a cluster chain.
                    # Estimate size: 2 ("./..") + children, each 32 bytes.
                    n_entries = 2 + len(entry.children)
                    est_bytes = n_entries * 32
                    first, _ = _alloc(max(est_bytes, 32))
                    entry.first_cluster = first
                for child in entry.children:
                    _assign(child, parent_is_root=(entry is self.root))
            else:
                first, _ = _alloc(len(entry.data))
                entry.first_cluster = first

        _assign(self.root, parent_is_root=True)

        # ----- Build MBR (sector 0) -----
        mbr = bytearray(SECTOR_SIZE)
        # Leave bootstrap code as zeros (QEMU boots via -kernel, not via MBR)
        # Disk signature at 0x1B8 (little-endian DWORD)
        struct.pack_into("<I", mbr, 0x1B8, self.signature)
        # Partition entry at 0x1BE
        struct.pack_into("<B3sB3sII", mbr, 0x1BE,
            0x80,                            # active (boot indicator)
            b"\x00\x00\x00",                 # CHS start (unused w/ LBA)
            PARTITION_TYPE_FAT16,
            b"\x00\x00\x00",                 # CHS end
            PARTITION_START_LBA,
            part_sectors,
        )
        # MBR signature
        mbr[0x1FE] = 0x55
        mbr[0x1FF] = 0xAA
        img[0:SECTOR_SIZE] = mbr

        # ----- Build FAT16 boot sector (partition sector 0) -----
        bs = bytearray(SECTOR_SIZE)
        # jmp short + nop (EB XX 90) so BPB doesn't get executed
        bs[0:3] = b"\xEB\x3C\x90"
        bs[3:11] = b"MSDOS5.0"            # OEM name
        struct.pack_into("<HBH", bs, 0x0B,
            SECTOR_SIZE, spc, RESERVED_SECTORS)
        bs[0x10] = NUM_FATS
        struct.pack_into("<H", bs, 0x11, ROOT_DIR_ENTRIES)
        # Total sectors (16-bit); if > 65535 use 32-bit field at 0x20
        if part_sectors <= 0xFFFF:
            struct.pack_into("<H", bs, 0x13, part_sectors)
            big_total = 0
        else:
            big_total = part_sectors
        bs[0x15] = 0xF8                    # media descriptor (fixed disk)
        struct.pack_into("<HHH", bs, 0x16,
            sectors_per_fat,               # sectors per FAT (16-bit)
            63,                            # sectors per track (legacy)
            255)                           # heads (legacy)
        struct.pack_into("<II", bs, 0x1C,
            PARTITION_START_LBA,           # hidden sectors (LBA of boot sector)
            big_total)                     # total sectors (32-bit, if >64K)
        bs[0x24] = 0x80                    # drive number (first HDD)
        bs[0x26] = 0x29                    # extended boot signature
        struct.pack_into("<I", bs, 0x27, self.volume_serial)
        bs[0x2B:0x36] = self.volume_label.ljust(11).encode("ascii")
        bs[0x36:0x3E] = b"FAT16   "
        bs[0x1FE] = 0x55
        bs[0x1FF] = 0xAA
        img[PARTITION_START_LBA * SECTOR_SIZE:
            PARTITION_START_LBA * SECTOR_SIZE + SECTOR_SIZE] = bs

        # ----- Write FAT tables -----
        fat = bytearray(sectors_per_fat * SECTOR_SIZE)
        for cl, val in fat_entries.items():
            struct.pack_into("<H", fat, cl * 2, val)
        img[fat1_lba * SECTOR_SIZE:
            fat1_lba * SECTOR_SIZE + len(fat)] = fat
        img[fat2_lba * SECTOR_SIZE:
            fat2_lba * SECTOR_SIZE + len(fat)] = fat

        # ----- Write root directory -----
        root_dir_bytes = bytearray()
        # Optional volume label entry (real disks usually have one)
        if self.volume_label:
            root_dir_bytes += _dir_entry(
                self.volume_label.ljust(11).encode("ascii"),
                ATTR_VOLUME_ID, 0, 0, time.time())
        for child in self.root.children:
            root_dir_bytes += _dir_entry(_encode_83(child.name), child.attr,
                                         child.first_cluster,
                                         len(child.data) if not child.is_dir else 0,
                                         child.mtime)
        # Pad to full root dir region
        root_dir_bytes += b"\x00" * (ROOT_DIR_SECTORS * SECTOR_SIZE - len(root_dir_bytes))
        img[root_lba * SECTOR_SIZE:
            root_lba * SECTOR_SIZE + len(root_dir_bytes)] = root_dir_bytes

        # ----- Write file data + subdirectory clusters -----
        def _write_entry(entry: Entry, parent_cluster: int) -> None:
            if entry.is_dir:
                dir_bytes = _emit_dir_data(entry, include_dotdot=True,
                                           parent_cluster=parent_cluster)
                # Place at entry.first_cluster → data_lba + (c - 2) * spc
                lba = data_lba + (entry.first_cluster - 2) * spc
                img[lba * SECTOR_SIZE:
                    lba * SECTOR_SIZE + len(dir_bytes)] = dir_bytes
                for child in entry.children:
                    _write_entry(child, parent_cluster=entry.first_cluster)
            else:
                if entry.first_cluster == 0:   # empty file
                    return
                lba = data_lba + (entry.first_cluster - 2) * spc
                img[lba * SECTOR_SIZE:
                    lba * SECTOR_SIZE + len(entry.data)] = entry.data

        for child in self.root.children:
            _write_entry(child, parent_cluster=0)   # root: parent cluster = 0

        # ----- Write out -----
        Path(out_path).write_bytes(bytes(img))

        # Compute the MBR checksum the NT kernel will compute in
        # IopCreateArcNames: sum of 128 DWORDs of sector 0.
        mbr_sum = 0
        for i in range(128):
            mbr_sum = (mbr_sum + struct.unpack_from("<I", img, i * 4)[0]) & 0xFFFFFFFF
        # ARC_DISK_SIGNATURE.CheckSum must be the two's-complement such that
        # (stored + mbr_sum) == 0 modulo 2^32.
        arc_checksum = (-mbr_sum) & 0xFFFFFFFF
        self._mbr_checksum = arc_checksum

        # Summary
        used_clusters   = next_cluster - 2
        free_clusters   = total_clusters - used_clusters
        print(f"  Disk: {self.size_bytes // (1024*1024)} MB"
              f"  Signature: 0x{self.signature:08X}")
        print(f"  Partition: LBA {PARTITION_START_LBA}..{PARTITION_START_LBA + part_sectors - 1}"
              f"  ({part_sectors * SECTOR_SIZE // (1024*1024)} MB FAT16)")
        print(f"  Clusters: {used_clusters} used / {total_clusters} total"
              f"  ({free_clusters * spc * SECTOR_SIZE // 1024} KB free)")
        print(f"  MBR checksum for ARC_DISK_SIGNATURE.CheckSum: 0x{arc_checksum:08X}")



def _dir_entry(name11: bytes, attr: int, first_cluster: int,
               size: int, mtime: float) -> bytes:
    """Build one 32-byte FAT directory entry."""
    assert len(name11) == 11
    fat_t, fat_d = _fat_time(mtime)
    # 11s + 3B + 7H + I = 11 + 3 + 14 + 4 = 32 bytes
    return struct.pack("<11sBBBHHHHHHHI",
        name11,
        attr,
        0,                        # reserved
        0,                        # creation time fine
        fat_t, fat_d,             # creation time/date
        fat_d,                    # last access date
        0,                        # high cluster (FAT32 — 0 for FAT16)
        fat_t, fat_d,             # last modify time/date
        first_cluster & 0xFFFF,   # low cluster
        size,
    )


# ---------------------------------------------------------------------------
# Default MicroNT boot disk
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# MicroNT boot disk — fixed layout, no command-line flexibility
# ---------------------------------------------------------------------------
#
# Disk file lists per profile. Each profile is a strict superset of the
# previous: micront ⊂ headless ⊂ gui.
#
# Paths are relative to the repo's `src/` directory.

SRC_ROOT = Path(__file__).resolve().parent.parent    # src/
NT        = SRC_ROOT / "NT"
SDK_LIB   = NT / "PUBLIC/SDK/LIB/I386"

def OBJ(comp:str):
    return NT / f"PRIVATE/{comp}/obj/i386"

PROFILES = ("micront", "headless", "gui")

NLS_DATA = NT / "PRIVATE/WINDOWS/WINNLS/DATA"

# Core files present in every profile.
#
# Only the three NLS tables RtlInitNlsTables consumes (ANSI codepage, OEM
# codepage, Unicode upcase) live here — kernel + ntdll case-fold via these.
# UNICODE/LOCALE/CTYPE/SORTKEY/SORTTBLS are Win32-only (kernel32's NLS APIs)
# and move into the headless tier.
_CORE_FILES: list[tuple[str, Path]] = [
    ("System32/ntoskrnl.exe",       OBJ("NTOS/INIT/UP") / "ntoskrnl.exe"),
    ("System32/hal.dll",            OBJ("NTOS/NTHALS/HAL") / "hal.dll"),
    ("System32/c_1252.nls",         NLS_DATA / "C_1252.NLS"),
    ("System32/c_437.nls",          NLS_DATA / "C_437.NLS"),
    ("System32/l_intl.nls",         NLS_DATA / "L_INTL.NLS"),
    # SYSTEM hive — path is rewritten per-profile in get_disk_files().
    ("System32/ntdll.dll",          SDK_LIB / "ntdll.dll"),
    ("System32/Drivers/atdisk.sys", SDK_LIB / "atdisk.sys"),
    ("System32/Drivers/null.sys",   SDK_LIB / "null.sys"),
    ("System32/Drivers/fastfat.sys",SDK_LIB / "fastfat.sys"),
    ("System32/Drivers/npfs.sys",   SDK_LIB / "npfs.sys"),
    ("System32/Drivers/msfs.sys",   SDK_LIB / "msfs.sys"),
    ("System32/Drivers/serial.sys", SDK_LIB / "serial.sys"),
]

# Headless adds the Win32 subsystem base.
_HEADLESS_FILES: list[tuple[str, Path]] = [
    # Win32-only NLS: CompareStringW / LCMapStringW / GetStringTypeW / etc.
    ("System32/unicode.nls",        NLS_DATA / "UNICODE.NLS"),
    ("System32/locale.nls",         NLS_DATA / "LOCALE.NLS"),
    ("System32/ctype.nls",          NLS_DATA / "CTYPE.NLS"),
    ("System32/sortkey.nls",        NLS_DATA / "SORTKEY.NLS"),
    ("System32/sorttbls.nls",       NLS_DATA / "SORTTBLS.NLS"),
    ("System32/smss.exe",           OBJ("SM/SERVER") / "smss.exe"),
    ("System32/CRTDLL.DLL",         SDK_LIB / "CRTDLL.DLL"),
    ("System32/kernel32.dll",       SDK_LIB / "kernel32.dll"),
    ("System32/advapi32.dll",       SDK_LIB / "advapi32.dll"),
    ("System32/rpcrt4.dll",         SDK_LIB / "rpcrt4.dll"),
    ("System32/rpclts1.dll",        SDK_LIB / "rpclts1.dll"),
    ("System32/rpcltc1.dll",        SDK_LIB / "rpcltc1.dll"),
    ("System32/csrsrv.dll",         SDK_LIB / "csrsrv.dll"),
    ("System32/basesrv.dll",        SDK_LIB / "basesrv.dll"),
    ("System32/csrss.exe",          OBJ("CSR/SERVER") / "csrss.exe"),
    ("System32/lsasrv.dll",         SDK_LIB / "lsasrv.dll"),
    ("System32/samsrv.dll",         SDK_LIB / "samsrv.dll"),
    ("System32/samlib.dll",         SDK_LIB / "samlib.dll"),
    ("System32/msv1_0.dll",         SDK_LIB / "msv1_0.dll"),
    ("System32/netapi32.dll",       SDK_LIB / "NETAPI32.DLL"),  # XXX: pre-built
    ("System32/netrap.dll",         SDK_LIB / "NETRAP.DLL"),    # XXX: pre-built
    ("System32/lsass.exe",          OBJ("LSA/SERVER") / "lsass.exe"),
]

FONTS = NT / "PRIVATE/WINDOWS/GDI/FONTS"

# GUI adds the window/drawing stack.
_GUI_FILES: list[tuple[str, Path]] = [
    # Win32 subsystem DLLs
    ("System32/user32.dll",         SDK_LIB / "user32.dll"),
    ("System32/gdi32.dll",          SDK_LIB / "gdi32.dll"),
    ("System32/winsrv.dll",         SDK_LIB / "winsrv.dll"),
    ("System32/WINSPOOL.DRV",       SDK_LIB / "WINSPOOL.DRV"),  # XXX: pre-built
    # Video: port framework + Bochs VGA miniport + framebuffer display driver
    ("System32/Drivers/videoprt.sys", SDK_LIB / "videoprt.sys"),
    ("System32/Drivers/bochsvga.sys", SDK_LIB / "bochsvga.sys"),
    ("System32/framebuf.dll",       SDK_LIB / "framebuf.dll"),
    # Input drivers
    ("System32/Drivers/i8042prt.sys", SDK_LIB / "i8042prt.sys"),
    ("System32/Drivers/kbdclass.sys", SDK_LIB / "kbdclass.sys"),
    ("System32/Drivers/mouclass.sys", SDK_LIB / "mouclass.sys"),
    # Login
    ("System32/winlogon.exe",       OBJ("WINDOWS/USER/WINLOGON/DAYTONA") / "winlogon.exe"),
    ("System32/userinit.exe",       OBJ("WINDOWS/USER/USERINIT") / "userinit.exe"),
    # Dialog bitmap fonts — referenced by SOFTWARE hive GRE_Initialize.
    # The "E" = English (CP1252) variants, bigger than the stripped-down
    # VGA*.FON (only 5-7 KB each, too sparse for dialog text).
    ("System32/sserife.fon",        FONTS / "SSERIFE.FON"),   # MS Sans Serif
    ("System32/coure.fon",          FONTS / "COURE.FON"),     # Courier (fixed)
    ("System32/smalle.fon",         FONTS / "SMALLE.FON"),    # Small Fonts
]


def get_disk_files(profile: str, output_dir: Path) -> list[tuple[str, Path]]:
    """Return the file list for *profile*, with the SYSTEM hive path
    pointing into *output_dir*."""
    files = list(_CORE_FILES)
    # Insert the profile-specific hives. SOFTWARE is only needed once a
    # Win32 userland is present (winlogon / basesrv / GDI consumers) — the
    # kernel itself reads nothing from it.
    files.append(("System32/config/SYSTEM", output_dir / "SYSTEM"))
    if profile in ("headless", "gui"):
        files.append(("System32/config/SOFTWARE", output_dir / "SOFTWARE"))
        files.extend(_HEADLESS_FILES)
    if profile == "gui":
        # DEFAULT hive holds pre-logon HKCU state (Control Panel\Colors,
        # WindowMetrics, Desktop). Kernel's CmpMachineHiveList entry
        # { L"DEFAULT", L"USER\\.DEFAULT", ... } mounts this at
        # \Registry\User\.Default — absence yields silent fallbacks to
        # whatever literals sit in the USERSRV source tree.
        files.append(("System32/config/DEFAULT", output_dir / "DEFAULT"))
        files.extend(_GUI_FILES)
    return files


def _build_image(disk_files: list[tuple[str, Path]], size_mb: int = 16,
                  signature: int = 0x4E544653) -> DiskImage:
    """Create a DiskImage, verify all sources exist, populate it."""
    img = DiskImage(size_mb=size_mb, signature=signature, volume_label="NT")
    missing = [str(src) for _, src in disk_files if not src.exists()]
    if missing:
        import sys
        print("ERROR: required disk inputs are missing:", file=sys.stderr)
        for m in missing:
            print(f"  - {m}", file=sys.stderr)
        print("Run the appropriate build.sh targets first.", file=sys.stderr)
        raise SystemExit(1)
    for dest, src in disk_files:
        sz = src.stat().st_size
        print(f"  {dest:<45s} {sz:>8,d}  ({src.name})")
        img.add_file(dest, src)
    return img


def main() -> None:
    import argparse
    ap = argparse.ArgumentParser(description="Build a profile-specific UEFI boot disk (ESP image).")
    ap.add_argument("--profile", choices=PROFILES, default="headless",
                    help="which file set to include (default: headless)")
    ap.add_argument("--output-dir", type=Path, default=None,
                    help="directory for esp.img (default: build/<profile>)")
    ap.add_argument("--efi-binary", type=Path, required=True,
                    help="path to BOOTX64.EFI")
    ap.add_argument("-x", "--extra", action="append", default=[],
                    metavar="HOST:DEST",
                    help="extra file or directory to stage on the disk. "
                         "If HOST is a directory its contents are copied "
                         "recursively under DEST. Repeatable. "
                         "e.g. -x build/foo.exe:System32/foo.exe, "
                         "-x src/cr/lua:lua")
    args = ap.parse_args()

    output_dir = args.output_dir or (SRC_ROOT.parent / "build" / args.profile)
    output_dir.mkdir(parents=True, exist_ok=True)

    disk_files = get_disk_files(args.profile, output_dir)

    # Extras: appended after the profile core so iteration-specific files
    # (e.g. a fresh hello-native.exe from src/cr) can ride on top without
    # editing the profile lists. If HOST is a directory, its contents are
    # staged recursively under DEST (preserving the subtree layout).
    for spec in args.extra:
        if ":" not in spec:
            raise SystemExit(f"-x {spec!r}: expected HOST:DEST")
        host, dest = spec.split(":", 1)
        host_path = Path(host)
        if host_path.is_dir():
            for file in sorted(host_path.rglob("*")):
                if file.is_file():
                    rel = file.relative_to(host_path).as_posix()
                    disk_files.append((f"{dest}/{rel}", file))
        else:
            disk_files.append((dest, host_path))

    esp_files = [("EFI/BOOT/BOOTX64.EFI", args.efi_binary)] + disk_files

    esp_out = output_dir / "esp.img"
    esp = _build_image(esp_files, size_mb=64)
    esp.write(esp_out)
    print(f"ESP image ({args.profile}): {esp_out}")


if __name__ == "__main__":
    main()
