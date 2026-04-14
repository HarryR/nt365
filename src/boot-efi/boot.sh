#!/bin/sh
#
# Boot MicroNT UEFI loader under OVMF32 in QEMU.
# Expects BOOTIA32.EFI + esp.img built by `make`.
#
# OVMF needs `-machine q35` — the secboot variant's SMM/PI code targets
# the ICH9 chipset. Default i440fx machine hangs silently before firmware
# ever initializes.
#
# Keep a per-checkout copy of NVRAM vars so /usr/share stays pristine.

cp /usr/share/OVMF/OVMF32_VARS_4M.fd OVMF32_VARS_4M.fd

# GDB=1 enables QEMU's gdb-stub on :1234 and freezes CPU until gdb attaches.
# Connect with `gdb -x gdb.init` from a second shell.
GDB_FLAGS=""
if [ "${GDB:-0}" = "1" ]; then
    GDB_FLAGS="-s -S"
    echo "[boot.sh] gdb-stub on :1234, CPU frozen — attach with: gdb -x gdb.init"
fi

#
# Serial: COM1 (loader) + COM2 (kernel debug) both multiplexed to stdio.
# QEMU chardev mux-on merges both streams to the same terminal — output
# is interleaved but lets us watch everything live.
# Storage: attach esp.img to a PIIX3 IDE controller rather than q35's
# default AHCI. Reason: NT 3.5's atdisk.sys only speaks legacy IDE/ATA
# (ISA + PCI IDE), not AHCI. OVMF's built-in IdeBusDxe still finds the
# PIIX3 controller fine so firmware-side boot is unaffected.
exec qemu-system-i386 -machine q35 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF32_CODE_4M.secboot.fd \
    -drive if=pflash,format=raw,file=./OVMF32_VARS_4M.fd \
    -device piix3-ide,id=ide \
    -drive id=disk0,file=esp.img,format=raw,if=none \
    -device ide-hd,bus=ide.0,drive=disk0 \
    -chardev stdio,id=serialmux,mux=on \
    -serial chardev:serialmux \
    -serial chardev:serialmux \
    -d int,cpu_reset,in_asm -D qemu.log \
    -no-reboot \
    -display none $GDB_FLAGS