#!/bin/sh
#
# Boot MicroNT UEFI loader under OVMF in QEMU.
#
# Usage: boot.sh [--machine pc|q35|microvm] [--disk ide|nvme|virtio-blk]
#                [--disk-image PATH] [--ramdisk PATH] [--vmlinux PATH]
#                [--vga] [--gdb] [--trace] [--netdump] [--coverage PATH]
#                [--mem MB] [--kernel-opts STRING] [--net-harness PORT]
#                [--kvm]
#   --disk-image PATH
#               Boot this ESP image instead of the default
#               build/disk/esp.img.  The Makefile passes per-profile
#               images (build/disk-selftest/esp.img, …) through here.
#   --ramdisk PATH
#               PVH direct-boot: boot the vmlinuz loader + this MBR/FAT16
#               ramdisk via -kernel/-initrd (no OVMF, no ESP drive).
#               Kernel cmdline rides -append. Use with --machine microvm
#               for the no-firmware shape. `make -C src boot-ramdisk`.
#   --vmlinux PATH
#               PVH loader ELF (default src/boot/vmlinuz/vmlinux).
#   --machine   QEMU machine type. q35 = ICH9 (default, modern PCIe
#               topology, no legacy IDE bus); pc = i440fx + PIIX3
#               (legacy shape, classic chipset with legacy IDE);
#               microvm = minimal, no PCI (PVH/--ramdisk only).
#   --disk      Disk controller for the boot volume. nvme (default) =
#               NVMe via nvme2k.sys; ide = legacy ATA, claimed by
#               atdisk.sys (q35 + ide bolts on a piix3-ide bridge);
#               virtio-blk = virtio-blk-pci, claimed by vioblk.sys
#               (canonical for GCP Persistent Disk + generic KVM).
#   --vga       Open a VGA window (stdvga) in addition to serial.
#               Default: serial-console-only (-display none).
#   --gdb       Pause CPU at boot and listen for gdb on :1234.
#               Connect from another shell with `make -C src gdb`
#               (loads ntoskrnl + hal .dwf, sources tools/gdb.init).
#   --trace     Log int / cpu_reset / in_asm to ./qemu.log.
#               Produces a large file; opt-in for exception debugging.
#   --kvm       Run under hardware virtualization (-accel kvm -cpu host)
#               instead of TCG: interrupts + clock go through KVM's
#               in-kernel LAPIC/IOAPIC (the production cloud path). Needs
#               /dev/kvm. Mutually exclusive with --coverage (TCG-only).
#   --coverage PATH
#               Load the qcov TCG plugin and write its per-block
#               execution histogram (NT kernel space) to PATH at guest
#               exit.  Pins the TCG accelerator — plugins don't run
#               under KVM.  Build the plugin first: `make -C src qcov`.
#               cov2lcov.py turns the histogram into lcov coverage.
#   --netdump   Dump every virtio-net frame to ./vionet.pcap (override
#               with NETDUMP_FILE=...). Open with wireshark/tshark to
#               see ARP, DHCP, outbound IP exactly as it leaves the guest.
#   --net-harness PORT
#               Replace QEMU's user-mode SLIRP NIC backend with a socket
#               netdev: QEMU listens on 127.0.0.1:PORT and the host-side
#               packet harness (src/tools/netharness/) connects to it,
#               becoming the guest's entire network.  No SLIRP means no
#               built-in DHCP/DNS — the harness serves DHCP itself.
#               --netdump still captures every frame (filter-dump
#               attaches to the netdev by id, not to the backend).
#   --throttle N
#               Throttle the QEMU process to N% of one host CPU using
#               cpulimit (apt install cpulimit).  Useful for reproducing
#               timing-sensitive races that only manifest on slower
#               machines (e.g. GitHub-hosted Actions runners).
#               THROTTLE=N env var works too (e.g. THROTTLE=25 make selftest).
#   --mem MB    Guest RAM in megabytes. Default 128.
#   --kernel-opts STRING
#               LOADER_PARAMETER_BLOCK.LoadOptions string (NT-style,
#               whitespace-separated, e.g. "/NOCACHEDSECTIONS /BURNMEMORY=4").
#               Pushed to the guest via fw_cfg blob `opt/micront/loadopts`,
#               which boot-efi reads and stamps into the LPB.  Empty by
#               default — kernel runs as if no flags were set.
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
#   * + virtio-blk (vioblk; modern PCIe storage path, GCP/KVM)
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
COVERAGE_FLAGS=""
EXTRA_DRIVE_FLAGS=""
# Accelerator: unset = qemu's default (TCG here).  --kvm switches to
# hardware virtualization so interrupts/clock run through KVM's in-kernel
# LAPIC/IOAPIC (the production cloud path).  -cpu host is mandatory under
# KVM (the default qemu64 model is rejected).
ACCEL_FLAGS=""
# Default NIC backend: QEMU user-mode SLIRP (NAT + built-in DHCP/DNS).
# --net-harness swaps this for a socket netdev driven by the host-side
# packet harness.  The -device virtio-net-pci line is unchanged either
# way — only the backend feeding netdev id n0 differs.
NETDEV_BACKEND="-netdev user,id=n0"
KERNEL_OPTS=""
MEM=128
# Canonical modern shape: q35 + NVMe, true PCIe topology, no legacy
# IDE bus.  pc + ide is still supported (and exercised in the CI
# smoke matrix) but kept around as the legacy/debug fallback.
MACHINE="q35"
DISK="nvme"
# PVH ramdisk direct-boot (set by --ramdisk): boot the vmlinuz PVH loader
# + an MBR/FAT16 ramdisk via -kernel/-initrd, no OVMF and no ESP drive.
RAMDISK=""
VMLINUX="$SCRIPT_DIR/boot/vmlinuz/vmlinux"

# Honour `NETDUMP=1` from the environment so wrapper Makefiles (e.g.
# `make selftest NETDUMP=1`) can opt into the pcap without rewriting
# their boot.sh invocation. The --netdump flag below also turns it on.
if [ -n "$NETDUMP" ] && [ "$NETDUMP" != "0" ]; then
    set -- --netdump "$@"
fi

# Same shape for THROTTLE — `THROTTLE=25 make selftest` opts into the
# cpulimit wrapper without touching the Makefile.
if [ -n "$THROTTLE" ] && [ "$THROTTLE" != "0" ]; then
    set -- --throttle "$THROTTLE" "$@"
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
        --kvm)
            # Hardware virtualization: interrupts + clock go through KVM's
            # in-kernel LAPIC/IOAPIC instead of TCG's emulated chips.
            ACCEL_FLAGS="-accel kvm -cpu host"
            echo "[boot.sh] accelerator: KVM (-cpu host, in-kernel irqchip)"
            shift
            ;;
        --coverage)
            shift
            QCOV_SO="$REPO_ROOT/build/host-tools/qcov.so"
            # TCG plugins only instrument under the TCG accelerator, so
            # pin -accel tcg — otherwise a KVM-capable host could pick
            # KVM and the histogram would come back empty.
            COVERAGE_FLAGS="-accel tcg -plugin $QCOV_SO,out=$1"
            echo "[boot.sh] coverage: qcov histogram -> $1"
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
        --throttle)
            # Wrap QEMU with cpulimit to throttle the whole process
            # (vCPU thread + iothread + helper threads) to N% of one
            # host CPU.  Reproduces the slow-host timing window that
            # GitHub Actions runners hit but local TCG/KVM don't.
            shift
            case "$1" in
                ''|*[!0-9]*)
                    echo "[boot.sh] --throttle needs a numeric percent 1..100" >&2
                    exit 2
                    ;;
            esac
            if [ "$1" -lt 1 ] || [ "$1" -gt 100 ]; then
                echo "[boot.sh] --throttle out of range: $1 (need 1..100)" >&2
                exit 2
            fi
            THROTTLE_PCT="$1"
            shift
            ;;
        --net-harness)
            shift
            case "$1" in
                ''|*[!0-9]*)
                    echo "[boot.sh] --net-harness needs a numeric PORT" >&2
                    exit 2
                    ;;
            esac
            NETDEV_BACKEND="-netdev socket,id=n0,listen=127.0.0.1:$1"
            echo "[boot.sh] net-harness: virtio-net on a socket netdev;" \
                 "QEMU listening on 127.0.0.1:$1 (SLIRP off, no built-in DHCP)"
            shift
            ;;
        --mem)
            shift
            MEM="$1"
            shift
            ;;
        --kernel-opts)
            shift
            KERNEL_OPTS="$1"
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
        --disk-image)
            # Override the boot ESP image.  ntosbe now composes
            # per-profile images into separate output dirs
            # (build/disk-selftest, build/disk-selfhost, …); the
            # Makefile passes the right one here so the lean and full
            # disks don't clobber a single build/disk/esp.img.
            shift
            ESP_IMG="$1"
            shift
            ;;
        --ramdisk)
            # PVH direct-boot mode: boot vmlinuz + this MBR/FAT16 ramdisk
            # via -kernel/-initrd (no OVMF, no ESP drive). The kernel
            # command line rides -append instead of fw_cfg.
            shift
            RAMDISK="$1"
            shift
            ;;
        --vmlinux)
            # Override the PVH loader ELF (default src/boot/vmlinuz/vmlinux).
            shift
            VMLINUX="$1"
            shift
            ;;
        --extra-disk)
            # Attach an additional virtio-blk drive at PCI position
            # after the primary.  Used for testing FS drivers (e.g.
            # NTFS) without disrupting the primary FAT16 boot disk:
            # the kernel surfaces this as a second \Device\Harddisk*
            # which whichever FS driver claims it on mount probe.
            shift
            EXTRA_DRIVE_FLAGS="$EXTRA_DRIVE_FLAGS \
                -drive file=$1,format=raw,if=none,id=extra$$ \
                -device virtio-blk-pci,drive=extra$$"
            echo "[boot.sh] extra disk: $1 (virtio-blk)"
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

if [ -z "$RAMDISK" ] && [ ! -f "$ESP_IMG" ]; then
    echo "ERROR: $ESP_IMG not found. Run: src/build.sh disk" >&2
    exit 1
fi

if [ -n "$COVERAGE_FLAGS" ] && [ ! -f "$QCOV_SO" ]; then
    echo "ERROR: qcov plugin $QCOV_SO not found. Run: make -C src qcov" >&2
    exit 1
fi

if [ -n "$THROTTLE_PCT" ] && ! command -v cpulimit >/dev/null 2>&1; then
    echo "ERROR: --throttle needs cpulimit (apt install cpulimit)" >&2
    exit 1
fi

# run_qemu — exec qemu directly, OR background it under cpulimit when
# --throttle is set.  cpulimit attaches via PID and limits the whole
# process tree (every QEMU thread shares the cpu accounting window),
# so we capture qemu's PID, launch cpulimit, and wait.  Trap INT/TERM
# so Ctrl-C in interactive runs cleans up both children.
run_qemu() {
    if [ -z "$THROTTLE_PCT" ]; then
        exec "$@"
    fi
    echo "[boot.sh] throttling QEMU to ${THROTTLE_PCT}% of one CPU (cpulimit)"
    "$@" &
    qpid=$!
    cpulimit --pid="$qpid" --limit="$THROTTLE_PCT" >/dev/null 2>&1 &
    cpid=$!
    trap 'kill "$cpid" 2>/dev/null; kill "$qpid" 2>/dev/null' INT TERM
    wait "$qpid"
    qstatus=$?
    kill "$cpid" 2>/dev/null
    exit "$qstatus"
}

if [ -z "$RAMDISK" ]; then
    cp /usr/share/OVMF/OVMF_VARS_4M.fd OVMF_VARS_4M.fd
fi

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
    microvm) MACHINE_FLAGS="-machine microvm" ;;
    *)
        echo "boot.sh: unknown --machine '$MACHINE' (try pc, q35, or microvm)" >&2
        exit 1
        ;;
esac

# --- PVH ramdisk direct-boot --------------------------------------------
# With --ramdisk, boot the vmlinuz PVH loader + the ramdisk via
# -kernel/-initrd: no firmware (OVMF), no ESP drive. The kernel command
# line rides -append (the PVH hvm_start_info cmdline -> vmlinuz cmdline()).
# pc / q35 get the same PCI virtio device set as the OVMF path; microvm
# has no PCI by default, so it's ISA serial + debug-exit only.
if [ -n "$RAMDISK" ]; then
    if [ ! -f "$VMLINUX" ]; then
        echo "ERROR: vmlinux '$VMLINUX' not found. Run: make -C src/boot/vmlinuz" >&2
        exit 1
    fi
    if [ ! -f "$RAMDISK" ]; then
        echo "ERROR: ramdisk '$RAMDISK' not found. Run: make -C src ramdisk" >&2
        exit 1
    fi
    echo "[boot.sh] PVH ramdisk boot: machine=$MACHINE"
    echo "[boot.sh]   kernel: $VMLINUX"
    echo "[boot.sh]   initrd: $RAMDISK"
    echo "[boot.sh]   memory: ${MEM} MB"
    [ -n "$KERNEL_OPTS" ] && echo "[boot.sh]   cmdline (-append): $KERNEL_OPTS"

    SERIAL_FLAGS="-chardev stdio,id=serialmux,mux=on -serial chardev:serialmux"
    DEBUG_EXIT="-device isa-debug-exit,iobase=0xf4,iosize=0x04"

    if [ "$MACHINE" = "microvm" ]; then
        run_qemu qemu-system-x86_64 $MACHINE_FLAGS $ACCEL_FLAGS -m "$MEM" \
            -kernel "$VMLINUX" -initrd "$RAMDISK" -append "$KERNEL_OPTS" \
            $SERIAL_FLAGS $DEBUG_EXIT -no-reboot \
            $DISPLAY_FLAGS $GDB_FLAGS $TRACE_FLAGS $COVERAGE_FLAGS
    else
        run_qemu qemu-system-x86_64 $MACHINE_FLAGS $ACCEL_FLAGS -m "$MEM" \
            -kernel "$VMLINUX" -initrd "$RAMDISK" -append "$KERNEL_OPTS" \
            $SERIAL_FLAGS \
            -object rng-random,id=rng0,filename=/dev/urandom \
            -device virtio-rng-pci,rng=rng0 \
            -device virtio-serial-pci,id=vser0 \
            -chardev pty,id=vcon0 -device virtconsole,chardev=vcon0 \
            -device virtio-keyboard-pci -device virtio-mouse-pci \
            $NETDEV_BACKEND -device virtio-net-pci,netdev=n0 \
            $DEBUG_EXIT -no-reboot \
            $EXTRA_DRIVE_FLAGS \
            $DISPLAY_FLAGS $GDB_FLAGS $TRACE_FLAGS $NETDUMP_FLAGS $COVERAGE_FLAGS
    fi
fi

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
        DISK_DESC="virtio-blk (vioblk; canonical for GCP Persistent Disk + KVM)"
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
if [ -n "$KERNEL_OPTS" ]; then
    echo "[boot.sh]   kernel-opts: $KERNEL_OPTS"
fi

# fw_cfg: pass kernel-opts string as a named blob.  boot-efi reads it
# at LPB build time and stamps it into LOADER_PARAMETER_BLOCK.LoadOptions.
# qemu rejects an empty `string=`, so omit the option entirely when
# nothing was supplied — boot-efi treats a missing file the same as
# an empty one (LoadOptions ends up "").
KERNEL_OPTS_FLAGS=""
if [ -n "$KERNEL_OPTS" ]; then
    KERNEL_OPTS_FLAGS="-fw_cfg name=opt/micront/loadopts,string=$KERNEL_OPTS"
fi

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
run_qemu qemu-system-x86_64 $MACHINE_FLAGS $ACCEL_FLAGS -m "$MEM" \
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
    $NETDEV_BACKEND \
    -device virtio-net-pci,netdev=n0 \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -no-reboot \
    $EXTRA_DRIVE_FLAGS \
    $KERNEL_OPTS_FLAGS \
    $DISPLAY_FLAGS $GDB_FLAGS $TRACE_FLAGS $NETDUMP_FLAGS $COVERAGE_FLAGS
