#!/usr/bin/env bash
# Test driver for CrashHandler. Builds (or assumes built) the crash_handler_test
# binary, then runs each scenario in an isolated workdir and verifies the
# expected logs/ artifact contents.
#
# Usage:
#   tests/crash_handler/run_tests.sh             # auto-locates the binary
#   tests/crash_handler/run_tests.sh /path/to/crash_handler_test

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RED=$'\033[31m'
GREEN=$'\033[32m'
YELLOW=$'\033[33m'
RESET=$'\033[0m'

# Resolve the test binary path. Prefer an explicit arg, fall back to the
# common cmake build directories.
locate_binary() {
    if [ $# -ge 1 ] && [ -x "$1" ]; then
        echo "$1"
        return
    fi
    local candidates=(
        "$ROOT/cmake-build-debug/tests/crash_handler/crash_handler_test"
        "$ROOT/cmake-build-release/tests/crash_handler/crash_handler_test"
        "$ROOT/build/tests/crash_handler/crash_handler_test"
    )
    for c in "${candidates[@]}"; do
        if [ -x "$c" ]; then
            echo "$c"
            return
        fi
    done
    echo ""
}

BIN="$(locate_binary "${1:-}")"
if [ -z "$BIN" ]; then
    echo "${RED}error:${RESET} could not find crash_handler_test binary."
    echo "build first with:  cmake --build cmake-build-debug --target crash_handler_test"
    exit 2
fi
echo "${YELLOW}using binary:${RESET} $BIN"
echo

PASS=0
FAIL=0
FAILED_NAMES=()

# Run one scenario in a fresh tmp workdir and emit PASS/FAIL.
#   $1: scenario label (printed in the summary)
#   $2: argv[1] passed to the binary
#   $3: expected file glob (e.g. logs/crash_*.trace), evaluated inside the tmp workdir
#   $4: regex that must appear somewhere in the matched file's content
#   $5: extra mode — "expect_signal" (binary should die from a signal),
#                    "expect_clean" (binary should exit 0),
#                    "expect_kill"  (driver SIGUSR1s then SIGTERMs the binary)
run_case() {
    local label="$1" scenario="$2" glob="$3" content_re="$4" mode="$5"
    local workdir
    workdir="$(mktemp -d -t crash_handler_test.XXXXXX)"
    local rc=0
    local trace_file=""
    local fail_reason=""

    pushd "$workdir" >/dev/null

    case "$mode" in
        expect_signal)
            # We expect the binary to die. Don't treat its non-zero exit as a
            # script failure; we only care about the trace file landing.
            set +e
            "$BIN" "$scenario" >stdout.log 2>stderr.log
            rc=$?
            set -e
            if [ "$rc" -lt 128 ]; then
                fail_reason="expected death-by-signal, exit code was $rc"
            fi
            ;;
        expect_clean)
            set +e
            "$BIN" "$scenario" >stdout.log 2>stderr.log
            rc=$?
            set -e
            if [ "$rc" -ne 0 ]; then
                fail_reason="expected clean exit 0, got $rc; stderr=$(head -c 200 stderr.log)"
            fi
            ;;
        expect_kill)
            # Start the binary in the background, wait for the deadlock state,
            # then signal it the same way the watchdog would.
            "$BIN" "$scenario" >stdout.log 2>stderr.log &
            local child=$!
            sleep 0.6  # let worker threads enter parked_worker
            kill -USR1 "$child" 2>/dev/null || true
            sleep 1.5  # let the dispatcher collect per-thread acks
            kill -TERM "$child" 2>/dev/null || true
            # Wait briefly for graceful exit; SIGKILL fallback if needed.
            local waited=0
            while kill -0 "$child" 2>/dev/null && [ $waited -lt 20 ]; do
                sleep 0.25
                waited=$((waited + 1))
            done
            if kill -0 "$child" 2>/dev/null; then
                kill -KILL "$child" 2>/dev/null || true
                fail_reason="binary refused to exit after SIGTERM"
            fi
            wait "$child" 2>/dev/null || true
            ;;
        *)
            fail_reason="unknown mode $mode"
            ;;
    esac

    # Locate the expected trace file. Glob is resolved against the tmp workdir.
    if [ -z "$fail_reason" ]; then
        shopt -s nullglob
        local matches=($glob)
        shopt -u nullglob
        if [ ${#matches[@]} -eq 0 ]; then
            fail_reason="no file matched glob '$glob' (workdir contents: $(ls -1 logs/ 2>/dev/null | tr '\n' ' '))"
        else
            trace_file="${matches[0]}"
            if ! grep -qE "$content_re" "$trace_file"; then
                fail_reason="trace file '$trace_file' missing pattern: $content_re; first 30 lines:
$(head -30 "$trace_file")"
            fi
        fi
    fi

    popd >/dev/null

    if [ -z "$fail_reason" ]; then
        echo "${GREEN}PASS${RESET}  $label  ($trace_file)"
        PASS=$((PASS + 1))
        rm -rf "$workdir"
    else
        echo "${RED}FAIL${RESET}  $label"
        echo "      $fail_reason"
        echo "      workdir kept for inspection: $workdir"
        FAIL=$((FAIL + 1))
        FAILED_NAMES+=("$label")
    fi
}

# ---- scenarios ----------------------------------------------------------
# 1) SIGSEGV from null deref -> crash_*.trace with "CRASH" + "Segmentation"
run_case "SIGSEGV (null deref)" \
    "segv" \
    "logs/crash_*.trace" \
    "=== CRASH signal=11" \
    "expect_signal"

# 2) abort() -> crash_*.trace with "CRASH" + "Aborted"
run_case "abort()" \
    "abort" \
    "logs/crash_*.trace" \
    "=== CRASH signal=6" \
    "expect_signal"

# 3) Uncaught C++ exception -> cpptrace::register_terminate_handler dumps to
# stderr, then process aborts. Our crash handler also catches the resulting
# SIGABRT and writes a crash_*.trace. Verify the trace exists.
run_case "uncaught std::exception (terminate)" \
    "terminate" \
    "logs/crash_*.trace" \
    "=== CRASH signal=6" \
    "expect_signal"

# 4) Self-issued SIGUSR1 -> deadlock_*.trace listing all 4 threads.
# The bot keeps running after, so exit should be clean.
run_case "deadlock dump (self SIGUSR1)" \
    "deadlock" \
    "logs/deadlock_*.trace" \
    "DEADLOCK DUMP" \
    "expect_clean"

# 5) Same scenario, driver-issued SIGUSR1 then SIGTERM — mimics the watchdog
# escalation. Bot should write deadlock_*.trace AND exit cleanly on TERM.
run_case "watchdog-style SIGUSR1 + SIGTERM" \
    "hang" \
    "logs/deadlock_*.trace" \
    "DEADLOCK DUMP" \
    "expect_kill"

# 6) Verify the deadlock trace actually contains per-thread blocks. We rerun
# scenario 4 and assert the file has multiple "--- thread tid=" lines (the
# dispatcher + at least one peer).
echo
echo "${YELLOW}--- bonus check: deadlock dump contains per-thread frames ---${RESET}"
workdir="$(mktemp -d -t crash_handler_test.XXXXXX)"
pushd "$workdir" >/dev/null
"$BIN" deadlock >/dev/null 2>&1 || true
shopt -s nullglob
matches=(logs/deadlock_*.trace)
shopt -u nullglob
if [ ${#matches[@]} -eq 0 ]; then
    echo "${RED}FAIL${RESET}  no deadlock trace file produced"
    FAIL=$((FAIL + 1))
    FAILED_NAMES+=("per-thread frames")
else
    count=$(grep -c -- "--- thread tid=" "${matches[0]}" || echo 0)
    if [ "$count" -ge 2 ]; then
        echo "${GREEN}PASS${RESET}  per-thread frames ($count thread headers found in ${matches[0]})"
        PASS=$((PASS + 1))
        rm -rf "$workdir"
    else
        echo "${RED}FAIL${RESET}  expected ≥2 thread headers, found $count in ${matches[0]}"
        echo "      workdir kept: $workdir"
        FAIL=$((FAIL + 1))
        FAILED_NAMES+=("per-thread frames")
    fi
fi
popd >/dev/null

# ---- summary -----------------------------------------------------------
echo
echo "${YELLOW}--- summary ---${RESET}"
echo "passed: $PASS"
echo "failed: $FAIL"
if [ "$FAIL" -gt 0 ]; then
    for n in "${FAILED_NAMES[@]}"; do echo "  - $n"; done
    exit 1
fi
exit 0
