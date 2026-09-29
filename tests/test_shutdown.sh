#!/usr/bin/env bash
set -euo pipefail

source tests/test_helpers.sh
test_setup
trap test_cleanup EXIT
test_fast_config
sed -i \
  -e 's/^B01_MIN_DURATION=.*/B01_MIN_DURATION=1000/' \
  -e 's/^B01_MAX_DURATION=.*/B01_MAX_DURATION=1000/' \
  -e 's/^LAB1_TEST_MIN_DURATION=.*/LAB1_TEST_MIN_DURATION=1000/' \
  -e 's/^LAB1_TEST_MAX_DURATION=.*/LAB1_TEST_MAX_DURATION=1000/' \
  "$TEST_RUNTIME_DIR/config/config.txt"
test_start

test_send \
  'SURGERY STOP01 init:0 type:CARDIO scheduled:1 urgency:HIGH tests:[PREOP] meds:[ANEST_C]' \
  'LAB_REQUEST STOP02 init:0 priority:URGENT lab:LAB1 tests:[HEMO]' \
  'EMERGENCY STOP03 init:0 triage:1 stability:100 tests:[HEMO] meds:[]'
test_wait_log_count '\[SURGERY\].*\[START\] STOP01' 1
test_wait_log_count '\[TRIAGE\].*\[ADMIT\] STOP03' 1

mapfile -t children < <(pgrep -P "$TEST_PID" || true)
test_stop INT

for child in "${children[@]}"; do
  ! kill -0 "$child" 2>/dev/null
done
find "$TEST_RUNTIME_DIR/results/stats_snapshots" -type f -name 'stats_*.txt' | grep -q .
for fifo in input_pipe triage_pipe surgery_pipe pharmacy_pipe lab_pipe; do
  [[ ! -e "$TEST_RUNTIME_DIR/$fifo" ]]
done

echo 'shutdown-under-load scenario: all assertions passed'
