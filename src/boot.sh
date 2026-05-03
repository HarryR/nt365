#!/bin/sh
#
# Boot MicroNT UEFI loader under OVMF in QEMU.
#
# Usage: boot.sh [--machine pc|q35] [--disk ide|nvme|virtio-blk]
#                [--vga] [--gdb] [--trace] [--netdump] [--mem MB]
#   --machine   QEMU machine type. q35 = ICH9 (default, modern PCIe
#               topology, no legacy IDE bus); pc = i440fx + PIIX3
#               (legacy shape, classic chipset with legacy IDE).
#   --disk      Disk controller for the boot volume. nvme (default) =
#               NVMe via nvme2k.sys; ide = legacy ATA, claimed by
#               atdisk.sys (q35 + ide bolts on a piix3-ide bridge);
#               virtio-blk = virtio-blk-pci (no driver yet, will
#               fail to mount).
#   --vga       Open a VGA window (stdvga) in addition to serial.
#               Default: serial-console-only (-display none).
#   --gdb       Pause CPU at boot and listen for gdb on :1234.
#               Connect with `gdb -x boot-efi/gdb.init` from another shell.
#   --trace     Log int / cpu_reset / in_asm to ./qemu.log.
#               Produces a large file; opt-in for exception debugging.
#   --netdump   Dump every virtio-net frame to ./vionet.pcap (override
#               with NETDUMP_FILE=...). Open with wireshark/tshark to
#               see ARP, DHCP, outbound IP exactly as it leaves the guest.
#   --mem MB    Guest RAM in megabytes. Default 128.
#
# Same disk image works across every machine + disk combo: boot-efi
# pre-loads atdisk + scsiport + scsidisk + nvme2k unconditionally and
# whichever driver finds its hardware claims the boot volume; the
# others log "no hardware" and stay idle.  So `--machine` and `--disk`
# pick the QEMU shape, and the OS sorts itself out.
#
# Combos:
#   q35 + nvme     (default — canonical modern shape, true PCIe)
#   pc + nvme      (NVMe on i440fx — exercises nvme2k on legacy chipset)
#   pc + ide       (classic shape — atdisk wins, kept for debug/legacy)
#   q35 + ide      (q35 + a piix3-ide bridge — diagnostic only)
#   * + virtio-blk (no driver yet, will not mount)
#
# Keep a per-checkout copy of NVRAM vars so /usr/share stays pristine.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ESP_IMG="$REPO_ROOT/build/disk/esp.img"

# --- Argument parsing --------------------------------------------------------

DISPLAY_FLAGS="-display none"
GDB_FLAGS=""
TRACE_FLAGS=""
NETDUMP_FLAGS=""
MEM=128
# Canonical modern shape: q35 + NVMe, true PCIe topology, no legacy
# IDE bus.  pc + ide is still supported (and exercised in the CI
# smoke matrix) but kept around as the legacy/debug fallback.
MACHINE="q35"
DISK="nvme"

# Honour `NETDUMP=1` from the environment so wrapper Makefiles (e.g.
# `make selftest NETDUMP=1`) can opt into the pcap without rewriting
# their boot.sh invocation. The --netdump flag below also turns it on.
if [ -n "$NETDUMP" ] && [ "$NETDUMP" != "0" ]; then
    set -- --netdump "$@"
fi

while [ $# -gt 0 ]; do
    case "$1" in
        --vga)
            DISPLAY_FLAGS="-display gtk -vga std"
            shift
            ;;
        --gdb)
            GDB_FLAGS="-s -S"
            echo "[boot.sh] gdb-stub on :1234, CPU frozen — in another shell:"
            echo "          make -C $SCRIPT_DIR gdb"
            echo "          (loads ntoskrnl + hal symbols, sources gdb.init,"
            echo "           and attaches to :1234 in one step)"
            shift
            ;;
        --trace)
            TRACE_FLAGS="-d int,cpu_reset,in_asm -D qemu.log"
            echo "[boot.sh] tracing int,cpu_reset,in_asm to ./qemu.log"
            shift
            ;;
        --netdump)
            # Capture every frame on the virtio-net netdev to a pcap so
            # we can see ARP / DHCP / outbound IP exactly as it leaves
            # the guest. Open the result with wireshark / tshark.
            NETDUMP_FILE="${NETDUMP_FILE:-./vionet.pcap}"
            NETDUMP_FLAGS="-object filter-dump,id=netdump0,netdev=n0,file=$NETDUMP_FILE"
            echo "[boot.sh] capturing virtio-net traffic to $NETDUMP_FILE"
            shift
            ;;
        --mem)
            shift
            MEM="$1"
            shift
            ;;
        --machine)
            shift
            MACHINE="$1"
            shift
            ;;
        --disk)
            shift
            DISK="$1"
            shift
            ;;
        -h|--help)
            sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //; s/^#//'
            exit 0
            ;;
        *)
            echo "boot.sh: unknown flag '$1'" >&2
            echo "         try: $0 --help" >&2
            exit 1
            ;;
    esac
done

# --- Sanity --------------------------------------------------------

if [ ! -f "$ESP_IMG" ]; then
    echo "ERROR: $ESP_IMG not found. Run: src/build.sh disk" >&2
    exit 1
fi

cp /usr/share/OVMF/OVMF_VARS_4M.fd OVMF_VARS_4M.fd

# --- Hardware profile resolution ----------------------------------------
#
# Translate (MACHINE, DISK) into the QEMU args that materialise that
# shape.  Storage is always `if=none` for non-IDE controllers so we can
# attach the drive to a specific device by id.  q35 has no legacy IDE
# bus by default — for the q35+ide transitional combo we add an
# explicit piix3-ide bridge (a hangover from when atdisk was the only
# disk path; keep it working for now, but q35+nvme is the canonical
# modern shape).

MACHINE_FLAGS=""
case "$MACHINE" in
    pc)  MACHINE_FLAGS="-machine pc"  ;;
    q35) MACHINE_FLAGS="-machine q35" ;;
    *)
        echo "boot.sh: unknown --machine '$MACHINE' (try pc or q35)" >&2
        exit 1
        ;;
esac

STORAGE_FLAGS=""
DISK_DESC=""
case "$DISK" in
    ide)
        if [ "$MACHINE" = "q35" ]; then
            # q35's default chipset has no legacy IDE; bolt a piix3-ide
            # controller onto the PCI bus and hang an ide-hd device off
            # its primary channel.  -drive bus= wants a numeric index
            # (not a qbus name), so attach via -device ide-hd instead.
            STORAGE_FLAGS="-device piix3-ide,id=ide0 -drive file=$ESP_IMG,format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ide0.0,unit=0"
            DISK_DESC="legacy IDE (atdisk via piix3-ide bridge on q35)"
        else
            STORAGE_FLAGS="-drive file=$ESP_IMG,format=raw,if=ide"
            DISK_DESC="legacy IDE (atdisk)"
        fi
        ;;
    nvme)
        STORAGE_FLAGS="-drive file=$ESP_IMG,format=raw,if=none,id=d0 -device nvme,drive=d0,serial=micront"
        DISK_DESC="NVMe (nvme2k)"
        ;;
    virtio-blk)
        STORAGE_FLAGS="-drive file=$ESP_IMG,format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0"
        DISK_DESC="virtio-blk (no driver — will fail to mount)"
        ;;
    *)
        echo "boot.sh: unknown --disk '$DISK' (try ide, nvme, or virtio-blk)" >&2
        exit 1
        ;;
esac

echo "[boot.sh] hardware profile: machine=$MACHINE  disk=$DISK"
echo "[boot.sh]   $DISK_DESC"
if [ -n "$DISPLAY_FLAGS" ] && [ "$DISPLAY_FLAGS" != "-display none" ]; then
    echo "[boot.sh]   display: $DISPLAY_FLAGS"
fi
echo "[boot.sh]   memory:  ${MEM} MB"

# --- QEMU --------------------------------------------------------------------
#
# Serial: COM1 (loader) + COM2 (kernel debug) both multiplexed to stdio.
# Single COM1 channel to stdio — everything (loader, HAL, kernel) writes
# here. COM2 was used historically for HAL debug while COM1 served the
# KD (WinDbg) protocol; we don't use KD, so HAL now writes to COM1 too.
#
# Storage: $STORAGE_FLAGS materialised from --machine + --disk above.
# All four candidate disk drivers (atdisk + scsiport + scsidisk +
# nvme2k) are pre-loaded by boot-efi unconditionally; the kernel's
# IopInitializeBootDrivers calls each DriverEntry, the one whose
# hardware is present claims the boot volume, the others log "no
# hardware" and stay idle.  Same disk image works on every combo.
#
# Virtio devices: our virtio.lib speaks the modern transport (PCI
# capabilities + MMIO common-config + INTx interrupts), no MSI-X. We
# accept both modern (0x1040+) and transitional (0x1000-0x103F) PCI
# device IDs in the drivers; QEMU's default for the classic device
# classes (rng, console, blk, etc.) is transitional, which exposes
# both interfaces simultaneously and our drivers drive via modern
# transport regardless. Modern-only classes (input, gpu, vsock, fs)
# get their 0x1040+ IDs unconditionally.
#
#   virtio-rng-pci          ->  1AF4:1005 (transitional default)  ->  viorng.sys
#   virtio-serial-pci       ->  1AF4:1003 (transitional default)  ->  vioser.sys
#   virtio-keyboard-pci     ->  1AF4:1052 (modern only)            ->  vioinput.sys
#   virtio-mouse-pci        ->  1AF4:1052 (modern only)            ->  vioinput.sys
#   virtio-net-pci          ->  1AF4:1000 (transitional default)   ->  vionet.sys
#                                                                       (NDIS 3.0 miniport; tcpip.sys binds on top)
#
# Networking: -netdev user gives QEMU's built-in user-mode NAT +
# DHCP server (10.0.2.2). No host bridge configuration required.
# Guest gets 10.0.2.15 from QEMU's DHCP, gateway 10.0.2.2, DNS 10.0.2.3.
#
# Storage profiles (atdisk / nvme / virtio-scsi) are coming as a
# follow-up; for now boot.sh ships only the IDE path.  Same boot disk
# image must mount cleanly across all three — that's what the profile
# matrix exists to verify.
#
# virtio-serial: the PCI device hosts ports; we attach a single
# virtconsole port to a pty chardev. QEMU prints the pty path on stdout
# at boot; cat that pty (e.g. `cat /dev/pts/N`) to see what the guest
# wrote and `echo foo > /dev/pts/N` to send to the guest.
# PCI BAR window: NT 3.5 is 32-bit non-PAE, so it can only address
# physical memory below 4 GiB. OVMF on qemu-system-x86_64 + i440fx
# defaults to a 64-bit PCI MMIO window above 4 GiB and happily places
# device BARs there (e.g. virtio at paddr=0x800000000) which the
# guest can't reach. The HAL handles this in HalpRelocateHighPciBars
# (see src/NT/PRIVATE/NTOS/NTHALS/HAL/I386/ixpcibus.c) - it walks
# every device at boot and rewrites any BAR placed above 4 GiB into
# the low 32-bit MMIO window before drivers see it. We deliberately
# do NOT pass -global i440FX-pcihost.pci-hole64-size=0 here so this
# path is exercised end-to-end, matching cloud / non-QEMU firmware
# that may not honour such tweaks.
exec qemu-system-x86_64 $MACHINE_FLAGS -m "$MEM" \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=./OVMF_VARS_4M.fd \
    $STORAGE_FLAGS \
    -chardev stdio,id=serialmux,mux=on \
    -serial chardev:serialmux \
    -object rng-random,id=rng0,filename=/dev/urandom \
    -device virtio-rng-pci,rng=rng0 \
    -device virtio-serial-pci,id=vser0 \
    -chardev pty,id=vcon0 \
    -device virtconsole,chardev=vcon0 \
    -device virtio-keyboard-pci \
    -device virtio-mouse-pci \
    -netdev user,id=n0 \
    -device virtio-net-pci,netdev=n0 \
    -no-reboot \
    $DISPLAY_FLAGS $GDB_FLAGS $TRACE_FLAGS $NETDUMP_FLAGS
