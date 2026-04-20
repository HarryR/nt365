#!/bin/sh
#
# Boot MicroNT UEFI loader under OVMF32 in QEMU.
# Usage: boot.sh [profile]   (default: headless)
# Profiles: micront, headless, gui
#
# OVMF needs `-machine q35` — the secboot variant's SMM/PI code targets
# the ICH9 chipset. Default i440fx machine hangs silently before firmware
# ever initializes.
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

cp /usr/share/OVMF/OVMF32_VARS_4M.fd OVMF32_VARS_4M.fd

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
# QEMU chardev mux-on merges both streams to the same terminal — output
# is interleaved but lets us watch everything live.
# Storage: attach esp.img to a PIIX3 IDE controller rather than q35's
# default AHCI. Reason: NT 3.5's atdisk.sys only speaks legacy IDE/ATA
# (ISA + PCI IDE), not AHCI. OVMF's built-in IdeBusDxe still finds the
# PIIX3 controller fine so firmware-side boot is unaffected.
exec qemu-system-i386 -machine q35 -m "$MEM" \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF32_CODE_4M.secboot.fd \
    -drive if=pflash,format=raw,file=./OVMF32_VARS_4M.fd \
    -device piix3-ide,id=ide \
    -drive id=disk0,file="$ESP_IMG",format=raw,if=none \
    -device ide-hd,bus=ide.0,drive=disk0 \
    -chardev stdio,id=serialmux,mux=on \
    -serial chardev:serialmux \
    -serial chardev:serialmux \
    -no-reboot \
    $DISPLAY_FLAGS $GDB_FLAGS $TRACE_FLAGS