#!/bin/bash
# agent_run.sh — bounded, scriptable harness for agentic gdb debug
# loops.  Boots a MicroNT image under qemu+gdb, optionally breaks at
# a symbol, runs an inspection script, and exits cleanly with a
# meaningful rc.  Designed to be ridden by future agents — every
# failure mode terminates the script within a bounded time.
#
# Reliability contract:
#   - hard wall-clock timeout on the whole script (--wall, default 240s)
#   - qemu + gdb run in this script's process group; trap on EXIT/INT/
#     TERM/HUP kills the entire group, so a Ctrl-C or sigint never
#     leaves zombies behind
#   - random gdb port (--port-base, default 12340 + slot in [0..15])
#     so concurrent runs don't collide on :1234
#   - explicit pre-flight: verify .dwf artifacts, prior agent_run not
#     running, gdb in PATH; fail fast with clear message
#   - QEMU receives -pidfile + -no-reboot + -monitor none + </dev/null
#     so a stuck guest can't hold the script open via stdin
#   - if gdb returns (any rc) without qemu having exited, harness
#     escalates: graceful wait (3s) → SIGTERM (2s) → SIGKILL.  Never
#     hangs on `wait $QEMU_PID`
#   - structured summary printed at exit (key=value lines + STATUS)
#
# Usage:
#   tools/agent_run.sh                                    # defaults
#   tools/agent_run.sh --break IopInitializeBootDrivers
#   tools/agent_run.sh --machine pc --disk ide
#   tools/agent_run.sh --break Phase1Initialization \
#                      --inspect 'info args' \
#                      --inspect 'bt 10'                 # repeatable
#   tools/agent_run.sh --no-break                        # boot+exit asap
#   tools/agent_run.sh --json                            # machine-readable tail
#   tools/agent_run.sh --break KiDispatchException \
#                      --break-cond 'ExceptionRecord->ExceptionCode == 0xc0000005' \
#                      --inspect 'p *ExceptionRecord' \
#                      --inspect 'nt trapframe'
#
# PVH / ramdisk (firmware-less; boots vmlinuz + initrd instead of OVMF +
# disk).  --vmlinux defaults to src/boot/vmlinuz/vmlinux:
#   tools/agent_run.sh --ramdisk build/disk-smoke-ramdisk/initrd.img \
#                      --machine microvm --break Phase1Initialization
#
# Hang hunting (--run-secs): free-run, SIGINT after N s, then dump $eip
# (>=0x80000000 = kernel/KSEG0, else user-mode) + backtrace.  No symbol
# breakpoint — for "it hangs and I don't know where":
#   tools/agent_run.sh --ramdisk build/disk-smoke-ramdisk/initrd.img \
#                      --machine microvm --run-secs 30
#
# Exit codes (precise — agents branch on these):
#   0  inspection completed, qemu exited via KiAgentExit (rc=1) or
#      reached a successful boot phase
#   1  pre-flight failure (missing artifacts, port busy, gdb missing)
#   2  argument error
#   3  TIMEOUT — gdb never hit the breakpoint within --timeout
#   4  qemu died unexpectedly during boot (before gdb attached)
#   5  BUGCHECK — qemu exited via the bugcheck path (rc=0x85=133)
#   6  WALL — outer wall-clock fence fired (--wall exceeded)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$REPO/src"

# ----- defaults ---------------------------------------------------------
MACHINE=q35
DISK=nvme
MEM=128
BREAK=Phase1Initialization
BREAK_COND=""
NO_BREAK=0
USER_BREAK=0           # 1 once --break is given (lets free-run set a bp too)
INSPECT_CMDS=()
GDB_TIMEOUT=120        # gdb-script timeout (bp must hit + script complete)
WALL_TIMEOUT=240       # outer fence (everything must terminate by now)
PORT_BASE=12340        # gdb port slot — random offset to avoid clashes
JSON=0
KEEP_LOGS=0
KERNEL_OPTS=""
RAMDISK=""             # set => PVH boot (vmlinuz + initrd), not OVMF + disk
VMLINUX="$SRC/boot/vmlinuz/vmlinux"
RUN_SECS=""            # free-run mode: continue, SIGINT after N s, then backtrace

usage() {
    sed -n '2,/^$/{s/^# \?//;p;}' "$0"
    exit 2
}

# ----- arg parse --------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --machine)     MACHINE="$2"; shift 2 ;;
        --disk)        DISK="$2"; shift 2 ;;
        --mem)         MEM="$2"; shift 2 ;;
        --break)       BREAK="$2"; NO_BREAK=0; USER_BREAK=1; shift 2 ;;
        --break-cond)  BREAK_COND="$2"; shift 2 ;;
        --no-break)    NO_BREAK=1; shift ;;
        --inspect)     INSPECT_CMDS+=("$2"); shift 2 ;;
        --timeout)     GDB_TIMEOUT="$2"; shift 2 ;;
        --wall)        WALL_TIMEOUT="$2"; shift 2 ;;
        --port-base)   PORT_BASE="$2"; shift 2 ;;
        --json)        JSON=1; shift ;;
        --keep-logs)   KEEP_LOGS=1; shift ;;
        --kernel-opts) KERNEL_OPTS="$2"; shift 2 ;;
        --ramdisk)     RAMDISK="$2"; shift 2 ;;
        --vmlinux)     VMLINUX="$2"; shift 2 ;;
        --run-secs)    RUN_SECS="$2"; shift 2 ;;
        -h|--help)     usage ;;
        *)             echo "agent_run: unknown arg: $1" >&2; usage ;;
    esac
done

USER_INSPECT=${#INSPECT_CMDS[@]}   # >0 => operator supplied --inspect
if [[ ${#INSPECT_CMDS[@]} -eq 0 ]]; then
    INSPECT_CMDS=(
        'echo === breakpoint hit ===\n'
        'info args'
        'bt 5'
    )
fi

# ----- pre-flight -------------------------------------------------------
NTOSKRNL_DWF="$REPO/src/NT/PRIVATE/NTOS/INIT/UP/obj/i386/ntoskrnl.dwf"
HAL_DWF="$REPO/src/NT/PRIVATE/NTOS/NTHALS/HAL/obj/i386/hal.dwf"

preflight_fail() {
    echo "agent_run: pre-flight: $1" >&2
    exit 1
}

[[ -f "$NTOSKRNL_DWF" ]] || preflight_fail "missing $NTOSKRNL_DWF (run 'src/build.sh init')"
[[ -f "$HAL_DWF"      ]] || preflight_fail "missing $HAL_DWF (run 'src/build.sh hal')"
command -v gdb >/dev/null      || preflight_fail "gdb not in PATH"
command -v qemu-system-x86_64 >/dev/null || preflight_fail "qemu-system-x86_64 not in PATH"
command -v timeout >/dev/null  || preflight_fail "GNU coreutils 'timeout' not in PATH"

# Verify KiAgentExit symbol resolves — without it the break-then-exit
# strategy is broken.  Cheap check, < 100 ms.  Skipped in --run-secs
# (free-run) mode, which detaches and lets the harness stop qemu instead
# of jumping to KiAgentExit.
if [[ -z "$RUN_SECS" ]]; then
    if ! gdb -batch -nx "$NTOSKRNL_DWF" -ex 'info address KiAgentExit' 2>/dev/null \
            | grep -q 'is a function at address'; then
        preflight_fail "KiAgentExit symbol not resolvable in $NTOSKRNL_DWF (rebuild ntoskrnl)"
    fi
fi

# Pick a free gdb port.  Tries PORT_BASE..PORT_BASE+15.
GDB_PORT=""
for off in $(seq 0 15); do
    cand=$((PORT_BASE + off))
    if ! (echo > "/dev/tcp/127.0.0.1/$cand") 2>/dev/null; then
        GDB_PORT=$cand
        break
    fi
done
[[ -n "$GDB_PORT" ]] || preflight_fail "no free port in $PORT_BASE..$((PORT_BASE+15))"

# ----- workspace --------------------------------------------------------
WORK="$(mktemp -d -t agent_run.XXXXXX)"
QEMU_LOG="$WORK/qemu.log"
GDB_LOG="$WORK/gdb.log"
GDB_SCRIPT="$WORK/gdb.cmd"
QEMU_PIDFILE="$WORK/qemu.pid"

# ----- cleanup trap -----------------------------------------------------
# Put qemu in its own process group via setsid so we can kill -PGID
# without race conditions.  Trap kills the whole group on any exit
# path (normal completion, signal, error).
QEMU_PGID=""
cleanup() {
    local rc=$?
    if [[ -n "${QEMU_PGID:-}" ]]; then
        # Negative PID = kill the process group.
        kill -TERM -- "-$QEMU_PGID" 2>/dev/null || true
        sleep 0.5
        kill -KILL -- "-$QEMU_PGID" 2>/dev/null || true
    fi
    if [[ "$KEEP_LOGS" = 0 && -d "$WORK" ]]; then
        rm -rf "$WORK"
    fi
    return $rc
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

# ----- spawn qemu -------------------------------------------------------
# We can't use boot.sh directly because we need to:
#   - put qemu in its own process group (setsid)
#   - choose our own gdb port
#   - get a pidfile for reliable killing
#   - close stdin so a stuck guest never blocks the harness
#
# So we build the qemu cmdline using the same flags boot.sh would,
# but without going through the wrapper.  This duplicates a small
# amount of logic but keeps the harness self-contained.

# Common tail shared by both boot modes: serial -> log file, debug-exit
# (so KiAgentExit/bugcheck terminate qemu), gdb stub frozen at reset
# (-S), pidfile + no stdin/monitor/display so a hung guest can't block us.
COMMON_TAIL=( -serial file:"$QEMU_LOG"
    -device isa-debug-exit,iobase=0xf4,iosize=0x04
    -no-reboot -display none -monitor none
    -gdb "tcp::$GDB_PORT" -S -pidfile "$QEMU_PIDFILE" )

if [[ -n "$RAMDISK" ]]; then
    # PVH path: vmlinuz + RAM-disk initrd via -kernel/-initrd, no firmware
    # and no disk controller (mirrors `boot.sh --ramdisk`).  microvm adds
    # the legacy PIC/PIT/RTC it omits by default; pc/q35 take their chipset.
    # No virtio device set here — the RAM disk (ramscsi) is the boot volume,
    # so the chipset doesn't matter for what we're debugging.
    [[ -f "$RAMDISK" ]] || preflight_fail "missing ramdisk $RAMDISK (run 'make -C src smoke-ramdisk-disk')"
    [[ -f "$VMLINUX" ]] || preflight_fail "missing vmlinux $VMLINUX (run 'src/build.sh vmlinux')"
    case "$MACHINE" in
        microvm) MFLAGS=(-machine microvm,pic=on,pit=on,rtc=on) ;;
        pc|q35)  MFLAGS=(-machine "$MACHINE") ;;
        *)       preflight_fail "unsupported --machine $MACHINE for --ramdisk (pc/q35/microvm)" ;;
    esac
    echo ">>> qemu: PVH ramdisk machine=$MACHINE port=$GDB_PORT" >&2
    setsid qemu-system-x86_64 \
        "${MFLAGS[@]}" -m "$MEM" \
        -kernel "$VMLINUX" -initrd "$RAMDISK" -append "$KERNEL_OPTS" \
        "${COMMON_TAIL[@]}" \
        < /dev/null > "$WORK/qemu.stdout" 2>&1 &
else
    # UEFI path: OVMF firmware + esp.img on the chosen disk controller.
    case "$DISK" in
        nvme)        STORAGE=(-drive file="$REPO/build/disk/esp.img",format=raw,if=none,id=d0 -device nvme,drive=d0,serial=micront) ;;
        ide)
            if [[ "$MACHINE" = q35 ]]; then
                STORAGE=(-device piix3-ide,id=ide0 -drive file="$REPO/build/disk/esp.img",format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ide0.0,unit=0)
            else
                STORAGE=(-drive file="$REPO/build/disk/esp.img",format=raw,if=ide)
            fi
            ;;
        virtio-blk)  STORAGE=(-drive file="$REPO/build/disk/esp.img",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0) ;;
        *)           preflight_fail "unsupported --disk $DISK (want nvme/ide/virtio-blk)" ;;
    esac

    [[ -f "$REPO/build/disk/esp.img" ]] || preflight_fail "missing $REPO/build/disk/esp.img (run src/build.sh)"

    OVMF_VARS="$WORK/OVMF_VARS_4M.fd"
    cp /usr/share/OVMF/OVMF_VARS_4M.fd "$OVMF_VARS"

    # LoadOptions plumbing: only attach the fw_cfg blob when --kernel-opts
    # was supplied.  qemu rejects `string=` (empty), so an absent flag is
    # represented by omitting the option entirely — the loader's reader
    # treats a missing file the same as an empty one.
    KOPTS_FLAG=()
    if [[ -n "$KERNEL_OPTS" ]]; then
        KOPTS_FLAG=(-fw_cfg "name=opt/micront/loadopts,string=$KERNEL_OPTS")
    fi

    echo ">>> qemu: UEFI machine=$MACHINE disk=$DISK port=$GDB_PORT" >&2
    setsid qemu-system-x86_64 \
        -machine "$MACHINE" -m "$MEM" \
        -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
        -drive if=pflash,format=raw,file="$OVMF_VARS" \
        "${STORAGE[@]}" \
        "${KOPTS_FLAG[@]}" \
        "${COMMON_TAIL[@]}" \
        < /dev/null > "$WORK/qemu.stdout" 2>&1 &
fi

# Get the PGID (which equals the leading PID under setsid).
QEMU_LEADER=$!
# Give qemu a moment to fork and write the pidfile.
for _ in $(seq 1 20); do
    if [[ -s "$QEMU_PIDFILE" ]]; then break; fi
    sleep 0.1
done
QEMU_PGID=$QEMU_LEADER

# ----- wait for gdb stub to listen --------------------------------------
ATTACHED=0
for _ in $(seq 1 60); do
    if (echo > "/dev/tcp/127.0.0.1/$GDB_PORT") 2>/dev/null; then
        ATTACHED=1
        break
    fi
    if ! kill -0 "$QEMU_LEADER" 2>/dev/null; then
        echo "agent_run: qemu died before opening :$GDB_PORT" >&2
        echo "  --- qemu stdout ---"  >&2
        cat "$WORK/qemu.stdout" >&2
        echo "  --- qemu serial log ---" >&2
        tail -40 "$QEMU_LOG" >&2 2>/dev/null || true
        exit 4
    fi
    sleep 0.5
done
[[ $ATTACHED = 1 ]] || { echo "agent_run: gdb stub never listened on :$GDB_PORT" >&2; exit 4; }

# ----- build gdb script -------------------------------------------------
{
    echo "set confirm off"
    echo "set pagination off"
    echo "set width 0"
    echo "set height 0"
    echo "set print pretty on"
    echo "symbol-file $NTOSKRNL_DWF"
    echo "add-symbol-file $HAL_DWF"
    if [[ -n "$RAMDISK" && -f "$VMLINUX" ]]; then
        echo "add-symbol-file $VMLINUX"     # PVH loader (built with -g DWARF)
    fi
    echo "source $SRC/tools/gdb.init"
    echo "source $SRC/tools/gdb_nt.py"
    echo "target remote :$GDB_PORT"
    if [[ -n "$RUN_SECS" ]]; then
        # Free-run for a hang of unknown location: let the guest run, an
        # external timer SIGINTs gdb after RUN_SECS, then we snapshot the
        # spin point.  $eip >= 0x80000000 => kernel/KSEG0, else user-mode.
        # If --break was given, arm it: the free-run then stops either at
        # the breakpoint (it fired) or via the SIGINT timer (it didn't) —
        # a clean "does this ISR ever run?" probe.
        if [[ $USER_BREAK = 1 ]]; then
            echo "echo \\n=== hbreak $BREAK (free-run; bp-or-${RUN_SECS}s) ===\\n"
            echo "hbreak $BREAK"
        fi
        echo "echo \\n=== free-run; SIGINT after ${RUN_SECS}s, then snapshot ===\\n"
        echo "continue"
        echo "echo \\n=== interrupted — spin point ===\\n"
        # $pc/$sp are gdb's arch-generic convenience regs — valid whether
        # the stub presents 32- or 64-bit names (qemu-system-x86_64's stub
        # uses rip/rsp even for a 32-bit guest, so \$eip is invalid).
        echo "p/x \$pc"
        echo "p/x \$sp"
        echo "x/i \$pc"
        echo "bt 30"
        if [[ $USER_INSPECT -gt 0 ]]; then
            for cmd in "${INSPECT_CMDS[@]}"; do
                echo "$cmd"
            done
        fi
    elif [[ $NO_BREAK = 0 ]]; then
        # Two flows:
        #
        #   no --break-cond   ->  hbreak then tbreak (advance-past-prologue
        #                         dance so args/locals are visible on the
        #                         second stop)
        #   with --break-cond ->  hbreak with the operator's expression as
        #                         a condition; we don't try to advance past
        #                         the prologue (the tbreak would discard
        #                         the condition + the operator's expression
        #                         is usually phrased so it can be evaluated
        #                         at offset 0 — e.g. trap-frame fields read
        #                         from $rsp). args may show <optimised out>
        #                         at this stop; that's expected.
        #
        # hbreak in both cases because software bps don't always arm before
        # the kernel is fully mapped.
        echo "echo \\n=== hbreak $BREAK ===\\n"
        echo "hbreak $BREAK"
        if [[ -n "$BREAK_COND" ]]; then
            echo "condition \$bpnum $BREAK_COND"
            echo "echo condition: $BREAK_COND\\n"
            echo "continue"
            echo "echo \\n=== inspection commands (conditional bp hit) ===\\n"
        else
            echo "continue"
            echo "echo === advancing past prologue ===\\n"
            echo "tbreak $BREAK"
            echo "continue"
            echo "echo \\n=== inspection commands (post-prologue) ===\\n"
        fi
        for cmd in "${INSPECT_CMDS[@]}"; do
            echo "$cmd"
        done
    fi
    if [[ -n "$RUN_SECS" ]]; then
        # In free-run mode we may be stopped in user space, where the
        # KiAgentExit jump (a kernel VA) wouldn't take — just detach and
        # let the harness's terminate-qemu logic stop the guest.
        echo "echo \\n=== detaching; harness will stop qemu ===\\n"
        echo "quit"
    else
        echo "echo \\n=== jump KiAgentExit (qemu will terminate, rc=1) ===\\n"
        echo "set \$pc = (unsigned long)KiAgentExit"
        echo "continue"
        echo "echo === gdb script complete ===\\n"
        echo "quit"
    fi
} > "$GDB_SCRIPT"

# ----- run gdb under timeout --------------------------------------------
START=$SECONDS
# `timeout` returns 124 if it had to kill the wrapped command.  We want
# to capture that rc — but `set -e` would make the script bail before
# we read $?, so wrap in a conditional.
GDB_RC=0
if [[ -n "$RUN_SECS" ]]; then
    # Free-run: the script's `continue` blocks; a background timer SIGINTs
    # gdb after RUN_SECS so it stops the (hung) guest and runs the
    # snapshot commands.  A second timer hard-stops gdb at GDB_TIMEOUT.
    gdb -batch -nx -x "$GDB_SCRIPT" > "$GDB_LOG" 2>&1 &
    GDB_PID=$!
    ( sleep "$RUN_SECS";    kill -INT  "$GDB_PID" 2>/dev/null ) & INT_PID=$!
    ( sleep "$GDB_TIMEOUT"; kill -TERM "$GDB_PID" 2>/dev/null ) & WALL_PID=$!
    wait "$GDB_PID" || GDB_RC=$?
    kill "$INT_PID" "$WALL_PID" 2>/dev/null || true
else
    timeout --kill-after=5 "$GDB_TIMEOUT" \
        gdb -batch -nx -x "$GDB_SCRIPT" > "$GDB_LOG" 2>&1 || GDB_RC=$?
fi
GDB_DURATION=$((SECONDS - START))

# ----- ensure qemu actually terminates ----------------------------------
# Whatever gdb did (succeeded, timed out, errored) — qemu MUST exit
# within a bounded window or we kill it.
KILLED=""
for _ in $(seq 1 6); do                          # 3 s graceful
    if ! kill -0 "$QEMU_LEADER" 2>/dev/null; then break; fi
    sleep 0.5
done
if kill -0 "$QEMU_LEADER" 2>/dev/null; then
    KILLED="TERM"
    kill -TERM -- "-$QEMU_PGID" 2>/dev/null || true
    for _ in $(seq 1 4); do                       # 2 s after TERM
        if ! kill -0 "$QEMU_LEADER" 2>/dev/null; then break; fi
        sleep 0.5
    done
    if kill -0 "$QEMU_LEADER" 2>/dev/null; then
        KILLED="KILL"
        kill -KILL -- "-$QEMU_PGID" 2>/dev/null || true
    fi
fi
QEMU_RC=0
wait "$QEMU_LEADER" 2>/dev/null || QEMU_RC=$?

# ----- decide STATUS ----------------------------------------------------
# Map (gdb_rc, qemu_rc, killed) → exit code
case "$QEMU_RC:$GDB_RC:$KILLED" in
    1:*:*)        STATUS=PASS;     EXIT_RC=0 ;;   # KiAgentExit clean (qemu rc 1)
    133:*:*)      STATUS=BUGCHECK; EXIT_RC=5 ;;   # bugcheck path (qemu rc 0x85=133)
    *:124:*)      STATUS=TIMEOUT;  EXIT_RC=3 ;;   # gdb script timed out
    *:*:KILL|*:*:TERM)
                  STATUS=KILLED;   EXIT_RC=4 ;;   # had to escalate-kill qemu
    0:0:*)        STATUS=PASS;     EXIT_RC=0 ;;   # rare: qemu shut down via something else
    *)            STATUS=FAIL;     EXIT_RC=4 ;;
esac

# ----- emit report ------------------------------------------------------
if [[ $JSON = 1 ]]; then
    # Single-line JSON for agent consumption.  Logs paths included so
    # the caller can read them if KEEP_LOGS=1.
    printf '{"status":"%s","exit_rc":%d,"qemu_rc":%d,"gdb_rc":%d,"killed":"%s","gdb_duration_s":%d,"break":"%s","machine":"%s","disk":"%s","port":%d,"qemu_log":"%s","gdb_log":"%s"}\n' \
        "$STATUS" "$EXIT_RC" "$QEMU_RC" "$GDB_RC" "$KILLED" \
        "$GDB_DURATION" "$BREAK" "$MACHINE" "$DISK" "$GDB_PORT" \
        "$QEMU_LOG" "$GDB_LOG"
else
    echo ""
    echo "=========================================================="
    echo "=== gdb session"
    echo "=========================================================="
    cat "$GDB_LOG"
    echo ""
    echo "=========================================================="
    echo "=== qemu serial log (tail 60)"
    echo "=========================================================="
    tail -60 "$QEMU_LOG" 2>/dev/null || echo "(no serial log captured)"
    echo ""
    echo "=========================================================="
    echo "=== summary"
    echo "=========================================================="
    echo "status=$STATUS"
    echo "exit_rc=$EXIT_RC"
    echo "qemu_rc=$QEMU_RC"
    echo "gdb_rc=$GDB_RC  ${GDB_RC:+(124=timeout)}"
    echo "killed=${KILLED:-none}"
    echo "gdb_duration_s=$GDB_DURATION"
    echo "mode=$([[ -n "$RAMDISK" ]] && echo ramdisk || echo uefi)  run_secs=${RUN_SECS:-none}"
    echo "break=$BREAK"
    echo "machine=$MACHINE  disk=$DISK  port=$GDB_PORT"
    echo "qemu_log=$QEMU_LOG"
    echo "gdb_log=$GDB_LOG"
    echo ""
fi

exit "$EXIT_RC"
