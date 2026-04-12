#!/bin/bash
#
# MicroNT QEMU Boot Script
#
# Usage: ./boot.sh [options]
#   --serial    Show serial output on terminal (default: to file)
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

# Check build products exist
for f in "$KERN" "$HAL" "$BOOT" "$NLS_ANSI" "$NLS_OEM" "$NLS_LANG" "$SYSTEM_HIVE"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found. Run ./build.sh all first."
        exit 1
    fi
done

# Parse options
SERIAL_OPT="-serial file:/tmp/micront_serial.log"
GDB_OPT=""
TRACE_OPT=""
DISPLAY_OPT="-display none"

for arg in "$@"; do
    case "$arg" in
        --serial)
            SERIAL_OPT="-serial stdio"
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
    esac
done

echo "Booting MicroNT..."
echo "  Kernel: $KERN"
echo "  HAL:    $HAL"
echo "  Boot:   $BOOT"
if [ "$SERIAL_OPT" = "-serial file:/tmp/micront_serial.log" ]; then
    echo "  Serial: /tmp/micront_serial.log"
fi
echo ""

eval qemu-system-i386 \
    -kernel "$BOOT" \
    -initrd "\"$KERN,$HAL,$NLS_ANSI,$NLS_OEM,$NLS_LANG,$SYSTEM_HIVE\"" \
    -m 64 \
    $DISPLAY_OPT \
    $SERIAL_OPT \
    -no-reboot \
    $GDB_OPT \
    $TRACE_OPT
