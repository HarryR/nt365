#!/bin/sh
#
# Boot MicroNT UEFI loader under OVMF64 in QEMU.
# Usage: boot.sh [profile]   (default: headless)
# Profiles: micront, headless, gui
#
# Runs under the default -machine pc (i440fx + PIIX3); OVMF64 works on
# both i440fx and q35, and our NT 3.5 atdisk.sys only speaks legacy IDE
# anyway, so staying on PIIX3 saves us the q35-→-AHCI impedance mismatch.
#
# Keep a per-checkout copy of NVRAM vars so /usr/share stays pristine.

PROFILE="${1:-headless}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ESP_IMG="$REPO_ROOT/build/$PROFILE/esp.img"

if [ ! -f "$ESP_IMG" ]; then
    echo "ERROR: $ESP_IMG not found. Run: build.sh $PROFILE" >&2
    exit 1
fi

# Display: GUI profile gets a window, others run headless.
if [ "$PROFILE" = "gui" ]; then
    DISPLAY_FLAGS="-display gtk -vga std"
else
    DISPLAY_FLAGS="-display none"
fi

cp /usr/share/OVMF/OVMF_VARS_4M.fd OVMF_VARS_4M.fd

# Guest RAM size. Override via env: MEM=512 boot.sh micront
# Default keeps parity with the old behaviour (qemu-system-i386 default
# is ~128 MB). The loader's identity map scales with registered ranges,
# not a blanket constant, so any size UEFI's allocator can place our
# image at should work.
MEM="${MEM:-128}"

# GDB=1 enables QEMU's gdb-stub on :1234 and freezes CPU until gdb attaches.
# Connect with `gdb -x gdb.init` from a second shell.
GDB_FLAGS=""
if [ "${GDB:-0}" = "1" ]; then
    GDB_FLAGS="-s -S"
    echo "[boot.sh] gdb-stub on :1234, CPU frozen — attach with: gdb -x gdb.init"
fi

# TRACE=1 enables QEMU's instruction + interrupt log to ./qemu.log.
# Off by default — `in_asm` produces a huge file per boot. Opt-in only
# when actually debugging a bad-instruction / exception.
TRACE_FLAGS=""
if [ "${TRACE:-0}" = "1" ]; then
    TRACE_FLAGS="-d int,cpu_reset,in_asm -D qemu.log"
    echo "[boot.sh] tracing int,cpu_reset,in_asm to ./qemu.log"
fi

#
# Serial: COM1 (loader) + COM2 (kernel debug) both multiplexed to stdio.
# Single COM1 channel to stdio — everything (loader, HAL, kernel) writes
# here. COM2 was used historically for HAL debug while COM1 served the
# KD (WinDbg) protocol; we don't use KD, so HAL now writes to COM1 too.
# Storage: legacy IDE is the default on -machine pc — no explicit
# -device piix3-ide needed. NT 3.5's atdisk.sys speaks IDE/ATA and
# OVMF64's IdeBusDxe handles the firmware-side enumeration fine.
exec qemu-system-x86_64 -m "$MEM" \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=./OVMF_VARS_4M.fd \
    -drive file="$ESP_IMG",format=raw,if=ide \
    -chardev stdio,id=serialmux,mux=on \
    -serial chardev:serialmux \
    -no-reboot \
    $DISPLAY_FLAGS $GDB_FLAGS $TRACE_FLAGS