#!/bin/bash
#
# MicroNT QEMU Boot Script
#
# Usage: ./boot.sh [options]
#   --serial    Show serial output on terminal (default: to file)
#   --kd        Use TCP serial (:4321) for kdserial.py KD packet decoder
#   --gdb       Start with GDB stub, wait for connection on :1234
#   --trace     Enable execution tracing to /tmp/qemu_trace.log
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERN="$SCRIPT_DIR/NT/PRIVATE/NTOS/INIT/UP/obj/i386/ntoskrnl.exe"
HAL="$SCRIPT_DIR/NT/PRIVATE/NTOS/NTHALS/HAL/obj/i386/hal.dll"
BOOT="$SCRIPT_DIR/boot/boot.elf"
NLS_ANSI="$SCRIPT_DIR/boot/data/C_1252.NLS"
NLS_OEM="$SCRIPT_DIR/boot/data/C_437.NLS"
NLS_LANG="$SCRIPT_DIR/boot/data/L_INTL.NLS"
SYSTEM_HIVE="$SCRIPT_DIR/boot/data/SYSTEM"
# Boot drivers — must match module indices expected by loader.c (6,7,8)
ATDISK="$SCRIPT_DIR/NT/PUBLIC/SDK/LIB/I386/atdisk.sys"
NULL_DRV="$SCRIPT_DIR/NT/PUBLIC/SDK/LIB/I386/null.sys"
FASTFAT="$SCRIPT_DIR/NT/PUBLIC/SDK/LIB/I386/fastfat.sys"

# Check build products exist
for f in "$KERN" "$HAL" "$BOOT" "$NLS_ANSI" "$NLS_OEM" "$NLS_LANG" "$SYSTEM_HIVE" \
         "$ATDISK" "$NULL_DRV" "$FASTFAT"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found. Run ./build.sh all first."
        exit 1
    fi
done

# Parse options
# COM1 = KD debugger port (binary packets)
# COM2 = HAL debug output (plain text)
SERIAL1_OPT="-serial file:/tmp/micront_kd.log"
SERIAL2_OPT="-serial file:/tmp/micront_hal.log"
GDB_OPT=""
TRACE_OPT=""
DISPLAY_OPT="-display none"

for arg in "$@"; do
    case "$arg" in
        --serial)
            SERIAL2_OPT="-serial stdio"
            ;;
        --kd)
            SERIAL1_OPT="-serial tcp:localhost:4321"
            SERIAL2_OPT="-serial stdio"
            echo "KD serial (COM1) connects to TCP :4321."
            echo "Start kdserial.py FIRST in another terminal:"
            echo "  python3 tools/kdserial.py --listen -v"
            ;;
        --gdb)
            GDB_OPT="-S -gdb tcp::1234"
            echo "GDB stub listening on :1234. Connect with:"
            echo "  gdb -ex 'target remote :1234'"
            ;;
        --trace)
            TRACE_OPT="-d exec 2>/tmp/qemu_trace.log"
            echo "Execution trace -> /tmp/qemu_trace.log"
            ;;
	--gui)
	    DISPLAY_OPT=""
	    ;;
    esac
done

echo "Booting MicroNT..."
echo "  Kernel: $KERN"
echo "  HAL:    $HAL"
echo "  Boot:   $BOOT"
if echo "$SERIAL1_OPT" | grep -q "file:"; then
    echo "  COM1 (KD):  /tmp/micront_kd.log"
fi
if echo "$SERIAL2_OPT" | grep -q "file:"; then
    echo "  COM2 (HAL): /tmp/micront_hal.log"
fi
echo ""

DISK="$SCRIPT_DIR/boot/data/disk.raw"
if [ ! -f "$DISK" ]; then
    echo "WARNING: $DISK not found — atdisk will find no disk. Run ./build.sh all to build it."
    DISK_OPT=""
else
    # Attach as a plain IDE disk (QEMU default PIIX3 controller). atdisk.sys
    # talks to the standard ATA/IDE ports 0x1F0 + IRQ 14.
    DISK_OPT="-drive file=$DISK,format=raw,if=ide,index=0,media=disk"
fi

eval qemu-system-i386 \
    -kernel "$BOOT" \
    -initrd "\"$KERN,$HAL,$NLS_ANSI,$NLS_OEM,$NLS_LANG,$SYSTEM_HIVE,$ATDISK,$NULL_DRV,$FASTFAT\"" \
    -m 64 \
    $DISPLAY_OPT \
    $SERIAL1_OPT \
    $SERIAL2_OPT \
    $DISK_OPT \
    -no-reboot \
    $GDB_OPT \
    $TRACE_OPT
