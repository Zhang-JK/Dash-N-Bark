#!/usr/bin/env bash
# Watchdog: keep Dash-N-Bark running. Restarts the binary if it exits OR if it
# stops emitting liveness heartbeats (process alive but stuck — e.g. event
# loop deadlocked, gateway frozen, worker pool wedged).
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

# Liveness heartbeat tuning. The bot writes two files:
#   logs/heartbeat.gateway  — touched on DPP's IO thread every 10s
#   logs/heartbeat.pool     — touched from the worker-pool ticker every ~5s
# If either file's mtime falls behind HEARTBEAT_STALE_SECS, we conclude the
# bot is stuck and kill it; the outer restart loop then takes over.
HEARTBEAT_GATEWAY="$LOG_DIR/heartbeat.gateway"
HEARTBEAT_POOL="$LOG_DIR/heartbeat.pool"
HEARTBEAT_STALE_SECS=60
HEARTBEAT_CHECK_INTERVAL=15      # seconds between staleness polls
HEARTBEAT_GRACE_SECS=45          # don't enforce until this long after start
                                 # (gives the bot time to register commands etc.)

mkdir -p "$LOG_DIR"
echo $$ > "$PIDFILE"

export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

child_pid=0
monitor_pid=0

# Returns 0 if the file exists and is fresher than HEARTBEAT_STALE_SECS, 1 otherwise.
heartbeat_fresh() {
    local f="$1"
    [ -f "$f" ] || return 1
    local mtime
    mtime=$(stat -c %Y "$f" 2>/dev/null) || return 1
    local now
    now=$(date +%s)
    [ $(( now - mtime )) -lt "$HEARTBEAT_STALE_SECS" ]
}

# Background monitor: polls heartbeat files while the bot is running and kills
# the bot if either heartbeat goes stale. Runs as long as the child PID it was
# launched with is still alive.
monitor_heartbeats() {
    local target_pid="$1"
    local started_at="$2"
    while kill -0 "$target_pid" 2>/dev/null; do
        sleep "$HEARTBEAT_CHECK_INTERVAL"
        # Grace period after startup — files may not exist yet during the first
        # few seconds while DPP is connecting.
        local now
        now=$(date +%s)
        if [ $(( now - started_at )) -lt "$HEARTBEAT_GRACE_SECS" ]; then
            continue
        fi
        local stale=""
        if ! heartbeat_fresh "$HEARTBEAT_GATEWAY"; then
            stale="gateway"
        fi
        if ! heartbeat_fresh "$HEARTBEAT_POOL"; then
            stale="${stale:+$stale,}pool"
        fi
        if [ -n "$stale" ]; then
            echo "[watchdog] $(date -Is) heartbeat stale ($stale), killing pid=$target_pid" \
                | tee -a "$LOG_DIR/watchdog.log" >&2
            # SIGTERM first so spdlog can flush; SIGKILL after a short grace.
            kill -TERM "$target_pid" 2>/dev/null || true
            for _ in $(seq 1 10); do
                kill -0 "$target_pid" 2>/dev/null || return 0
                sleep 0.5
            done
            kill -KILL "$target_pid" 2>/dev/null || true
            return 0
        fi
    done
}

shutdown() {
    if [ "$monitor_pid" -gt 0 ] && kill -0 "$monitor_pid" 2>/dev/null; then
        kill -TERM "$monitor_pid" 2>/dev/null || true
    fi
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

    # Wipe any leftover heartbeat files from a previous run so the freshness
    # check can't pass on stale data while the new bot is still starting up.
    rm -f "$HEARTBEAT_GATEWAY" "$HEARTBEAT_POOL"

    "$BIN" >> "$log" 2>&1 &
    child_pid=$!

    # Spawn the heartbeat monitor in the background. It self-exits when the
    # child dies (whether by its own hand or because the monitor killed it).
    monitor_heartbeats "$child_pid" "$started" &
    monitor_pid=$!

    wait "$child_pid"
    rc=$?
    child_pid=0

    # Reap the monitor in case it's still polling.
    if [ "$monitor_pid" -gt 0 ] && kill -0 "$monitor_pid" 2>/dev/null; then
        kill -TERM "$monitor_pid" 2>/dev/null || true
        wait "$monitor_pid" 2>/dev/null || true
    fi
    monitor_pid=0

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
