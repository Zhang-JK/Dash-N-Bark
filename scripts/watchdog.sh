#!/usr/bin/env bash
# Watchdog: keep Dash-N-Bark running. Restarts the binary if it exits for any reason.
#
# Usage: ./scripts/watchdog.sh
# Run from the deploy root (the directory that contains ./bin and ./lib).
# Writes its own pid to logs/watchdog.pid so the deploy step can stop it cleanly.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BIN="./bin/Dash-N-Bark"
LOG_DIR="logs"
PIDFILE="$LOG_DIR/watchdog.pid"
MIN_UPTIME=30        # seconds; runs shorter than this count as a fast-fail
BACKOFF_START=2      # seconds
BACKOFF_MAX=300      # seconds

mkdir -p "$LOG_DIR"
echo $$ > "$PIDFILE"

export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

child_pid=0
shutdown() {
    if [ "$child_pid" -gt 0 ] && kill -0 "$child_pid" 2>/dev/null; then
        kill -TERM "$child_pid" 2>/dev/null || true
        for _ in $(seq 1 20); do
            kill -0 "$child_pid" 2>/dev/null || break
            sleep 0.5
        done
        kill -KILL "$child_pid" 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
    exit 0
}
trap shutdown TERM INT HUP

backoff="$BACKOFF_START"
while true; do
    log="$LOG_DIR/output_$(date +%Y%m%d_%H%M%S).log"
    echo "[watchdog] $(date -Is) starting $BIN -> $log"
    started=$(date +%s)

    "$BIN" >> "$log" 2>&1 &
    child_pid=$!
    wait "$child_pid"
    rc=$?
    child_pid=0

    ended=$(date +%s)
    uptime=$(( ended - started ))
    echo "[watchdog] $(date -Is) exited rc=$rc after ${uptime}s" | tee -a "$log"

    if [ "$uptime" -ge "$MIN_UPTIME" ]; then
        backoff="$BACKOFF_START"
    fi
    echo "[watchdog] restarting in ${backoff}s"
    sleep "$backoff"
    backoff=$(( backoff * 2 ))
    [ "$backoff" -gt "$BACKOFF_MAX" ] && backoff="$BACKOFF_MAX"
done
