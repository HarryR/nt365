#!/bin/bash
#
# MicroNT Clean Script
# Removes all build artifacts so build.sh produces a fully clean build
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NT_ROOT="$SCRIPT_DIR/NT"
NTOS="$NT_ROOT/PRIVATE/NTOS"

echo "Cleaning build artifacts..."

# Component obj/ directories (where .obj, .lib, .res, _objects.mac go)
COMP_DIRS=(
    "$NTOS/KE/UP"
    "$NTOS/RTL/UP"
    "$NTOS/EX/UP"
    "$NTOS/OB/UP"
    "$NTOS/SE/UP"
    "$NTOS/PS/UP"
    "$NTOS/MM/UP"
    "$NTOS/CACHE/UP"
    "$NTOS/CONFIG/UP"
    "$NTOS/LPC/UP"
    "$NTOS/DBGK/UP"
    "$NTOS/IO/UP"
    "$NTOS/KD/UP"
    "$NTOS/FSRTL/UP"
    "$NTOS/RAW/UP"
    "$NTOS/VDM/UP"
    "$NTOS/INIT/UP"
    "$NTOS/NTHALS/HAL"
    "$NTOS/DD/HARDDISK"
    "$NTOS/DD/NULL"
    "$NTOS/FASTFAT"
)

for dir in "${COMP_DIRS[@]}"; do
    if [ -d "$dir/obj" ]; then
        rm -rf "$dir/obj"
        echo "  cleaned $dir/obj/"
    fi
done

# Shared NTOS obj/ directory (where component .lib files are collected)
if [ -d "$NTOS/obj" ]; then
    rm -rf "$NTOS/obj"
    echo "  cleaned $NTOS/obj/"
fi

# RC temp files that may be left behind
find "$NTOS" -maxdepth 3 \( -name "RC*00*" -o -name "RD*00*" \) -delete 2>/dev/null

# Build products in PUBLIC/SDK/LIB that we generate
for f in ntoskrnl.lib ntoskrnl.exp hal.lib hal.exp tmp.lib tmp.exp \
         atdisk.sys null.sys fastfat.sys; do
    if [ -f "$NT_ROOT/PUBLIC/SDK/LIB/I386/$f" ]; then
        rm -f "$NT_ROOT/PUBLIC/SDK/LIB/I386/$f"
        echo "  cleaned PUBLIC/SDK/LIB/I386/$f"
    fi
done

echo "Clean complete."
