#!/usr/bin/env python3
"""
mknvmeimg.py - Create a blank MBR + FAT16 image for the NVMe data disk.

Used by boot.sh on first run to seed build/disk/nvme.img with a
formatted volume so SCSIDISK + fastfat can mount it. Re-uses the
DiskImage builder from mkdisk.py to keep one source of truth for the
MBR/BPB layout.

Usage:
    mknvmeimg.py PATH [--size-mb N] [--label LABEL]

The image persists across boots (boot.sh only creates it if missing),
so anything Lua writes to the volume survives. Delete the file by
hand to start fresh.
"""

import argparse
import sys
from pathlib import Path

# DiskImage lives next to this script.
sys.path.insert(0, str(Path(__file__).parent))
from mkdisk import DiskImage


def main() -> None:
    ap = argparse.ArgumentParser(description="Build a blank FAT16 NVMe data disk.")
    ap.add_argument("output", type=Path, help="path to write the image (e.g. build/disk/nvme.img)")
    ap.add_argument("--size-mb", type=int, default=64,
                    help="image size in MiB (default: 64)")
    ap.add_argument("--label",   type=str, default="MICRONTDAT",
                    help="FAT16 volume label, up to 11 chars (default: MICRONTDAT)")
    args = ap.parse_args()

    img = DiskImage(
        size_mb=args.size_mb,
        # 'NDAT' little-endian; arbitrary but distinct from esp.img's 'NTFS'
        # signature so the kernel can tell the two disks apart in the
        # ARC namespace.
        signature=int.from_bytes(b"NDAT", "little"),
        volume_label=args.label,
    )

    # One README so the volume isn't completely empty on first mount -
    # makes "did fastfat actually mount it?" trivially answerable.
    img.add_bytes(
        "README.TXT",
        b"MicroNT NVMe data disk.\r\n"
        b"Persisted across boots. Populated by Lua / userland.\r\n"
        b"Delete build/disk/nvme.img to start fresh.\r\n",
    )

    img.write(args.output)
    print(f"NVMe data disk: {args.output}")


if __name__ == "__main__":
    main()
