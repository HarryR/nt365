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
NO_BREAK=0
INSPECT_CMDS=()
GDB_TIMEOUT=120        # gdb-script timeout (bp must hit + script complete)
WALL_TIMEOUT=240       # outer fence (everything must terminate by now)
PORT_BASE=12340        # gdb port slot — random offset to avoid clashes
JSON=0
KEEP_LOGS=0

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
        --break)       BREAK="$2"; NO_BREAK=0; shift 2 ;;
        --no-break)    NO_BREAK=1; shift ;;
        --inspect)     INSPECT_CMDS+=("$2"); shift 2 ;;
        --timeout)     GDB_TIMEOUT="$2"; shift 2 ;;
        --wall)        WALL_TIMEOUT="$2"; shift 2 ;;
        --port-base)   PORT_BASE="$2"; shift 2 ;;
        --json)        JSON=1; shift ;;
        --keep-logs)   KEEP_LOGS=1; shift ;;
        -h|--help)     usage ;;
        *)             echo "agent_run: unknown arg: $1" >&2; usage ;;
    esac
done

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

# Verify KiAgentExit symbol resolves — without it the whole exit-path
# strategy is broken.  Cheap check, < 100 ms.
if ! gdb -batch -nx "$NTOSKRNL_DWF" -ex 'info address KiAgentExit' 2>/dev/null \
        | grep -q 'is a function at address'; then
    preflight_fail "KiAgentExit symbol not resolvable in $NTOSKRNL_DWF (rebuild ntoskrnl)"
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

case "$DISK" in
    nvme)        STORAGE="-drive file=$REPO/build/disk/esp.img,format=raw,if=none,id=d0 -device nvme,drive=d0,serial=micront" ;;
    ide)
        if [[ "$MACHINE" = q35 ]]; then
            STORAGE="-device piix3-ide,id=ide0 -drive file=$REPO/build/disk/esp.img,format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ide0.0,unit=0"
        else
            STORAGE="-drive file=$REPO/build/disk/esp.img,format=raw,if=ide"
        fi
        ;;
    virtio-blk)  STORAGE="-drive file=$REPO/build/disk/esp.img,format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0" ;;
    *)           preflight_fail "unsupported --disk $DISK (want nvme/ide/virtio-blk)" ;;
esac

[[ -f "$REPO/build/disk/esp.img" ]] || preflight_fail "missing $REPO/build/disk/esp.img (run src/build.sh)"

OVMF_VARS="$WORK/OVMF_VARS_4M.fd"
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$OVMF_VARS"

echo ">>> qemu: machine=$MACHINE disk=$DISK port=$GDB_PORT" >&2

setsid qemu-system-x86_64 \
    -machine "$MACHINE" \
    -m "$MEM" \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    $STORAGE \
    -serial file:"$QEMU_LOG" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -no-reboot \
    -display none \
    -monitor none \
    -gdb "tcp::$GDB_PORT" \
    -S \
    -pidfile "$QEMU_PIDFILE" \
    < /dev/null > "$WORK/qemu.stdout" 2>&1 &

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
    echo "source $SRC/tools/gdb.init"
    echo "source $SRC/tools/gdb_drivers.py"
    echo "source $SRC/tools/gdb_users.py"
    echo "target remote :$GDB_PORT"
    if [[ $NO_BREAK = 0 ]]; then
        echo "echo \\n=== hbreak $BREAK ===\\n"
        echo "hbreak $BREAK"
        echo "echo === continuing ===\\n"
        echo "continue"
        echo "echo \\n=== inspection commands ===\\n"
        for cmd in "${INSPECT_CMDS[@]}"; do
            echo "$cmd"
        done
    fi
    echo "echo \\n=== jump KiAgentExit (qemu will terminate, rc=1) ===\\n"
    echo "set \$pc = (unsigned long)KiAgentExit"
    echo "continue"
    echo "echo === gdb script complete ===\\n"
    echo "quit"
} > "$GDB_SCRIPT"

# ----- run gdb under timeout --------------------------------------------
START=$SECONDS
# `timeout` returns 124 if it had to kill the wrapped command.  We want
# to capture that rc — but `set -e` would make the script bail before
# we read $?, so wrap in a conditional.
GDB_RC=0
timeout --kill-after=5 "$GDB_TIMEOUT" \
    gdb -batch -nx -x "$GDB_SCRIPT" > "$GDB_LOG" 2>&1 || GDB_RC=$?
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
    echo "break=$BREAK"
    echo "machine=$MACHINE  disk=$DISK  port=$GDB_PORT"
    echo "qemu_log=$QEMU_LOG"
    echo "gdb_log=$GDB_LOG"
    echo ""
fi

exit "$EXIT_RC"
