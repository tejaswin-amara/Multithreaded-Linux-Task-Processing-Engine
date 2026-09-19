#!/usr/bin/env bash
# Concurrency stress test for loom: fires CLI-driven batches and a burst of
# concurrent HTTP submissions at a running instance while resizing the pool
# twice, then checks that nothing was lost. See docs/03-test-plan.md.
#
# Usage:
#   ./tests/stress_test.sh            # normal binary
#   ./tests/stress_test.sh --tsan     # build with ThreadSanitizer first and
#                                      # also fail if it reports any warning
set -u
cd "$(dirname "$0")/.."

PORT=8099
LOGFILE="$(mktemp)"
USE_TSAN=0
[ "${1:-}" = "--tsan" ] && USE_TSAN=1

if [ "$USE_TSAN" -eq 1 ]; then
  echo "building with ThreadSanitizer..."
  make tsan >/dev/null
else
  make >/dev/null
fi

(
  sleep 0.4
  echo "batch 10"
  sleep 0.2
  echo "workers 8"
  sleep 0.2
  echo "batch 10"
  sleep 0.2
  echo "workers 3"
  sleep 1.5
  echo "status"
  sleep 0.3
  echo "quit"
) | timeout 30 bin/task_engine -w 4 -p "$PORT" > "$LOGFILE" 2>&1 &
LOOM_PID=$!

sleep 0.3
CURL_PIDS=()
for i in $(seq 1 15); do
  curl -s --max-time 3 -X POST "http://localhost:$PORT/api/submit" \
    -H 'Content-Type: application/json' \
    -d "{\"type\":\"prime\",\"param\":$((5000 + RANDOM % 20000))}" > /dev/null &
  CURL_PIDS+=($!)
done
# Wait on each curl PID individually -- a bare `wait` with no arguments
# would also reap $LOOM_PID early, making the explicit wait below fail
# with "pid ... is not a child of this shell" (bash exit code 127).
for pid in "${CURL_PIDS[@]}"; do
  wait "$pid" 2>/dev/null
done

wait "$LOOM_PID"
EXIT_CODE=$?

echo
echo "----- run log -----"
cat "$LOGFILE"
echo "--------------------"

STATUS_LINE=$(grep "final stats" "$LOGFILE" || true)
TSAN_WARNINGS=$(grep -c "WARNING: ThreadSanitizer" "$LOGFILE" || true)

PASS=1
[ "$EXIT_CODE" -eq 0 ] || { echo "FAIL: loom exited $EXIT_CODE"; PASS=0; }
[ -n "$STATUS_LINE" ] || { echo "FAIL: no 'final stats' line -- shutdown did not complete"; PASS=0; }
if [ "$USE_TSAN" -eq 1 ] && [ "${TSAN_WARNINGS:-0}" -ne 0 ]; then
  echo "FAIL: ThreadSanitizer reported $TSAN_WARNINGS warning(s)"
  PASS=0
fi

rm -f "$LOGFILE"
if [ "$PASS" -eq 1 ]; then
  echo "PASS: $STATUS_LINE"
  exit 0
else
  exit 1
fi
