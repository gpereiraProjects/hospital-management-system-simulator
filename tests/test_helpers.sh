#!/usr/bin/env bash

# Shared, bounded helpers for the black-box integration tests.

test_setup() {
  local binary=${BINARY:-build/release/bin/hospital_system}
  TEST_SOURCE_BINARY=$(readlink -f "$binary")
  TEST_SOURCE_CONFIG=$(readlink -f config/config.txt)
  TEST_RUNTIME_DIR=$(mktemp -d)
  TEST_OUTPUT="$TEST_RUNTIME_DIR/system.out"
  TEST_PID=""

  mkdir -p "$TEST_RUNTIME_DIR/config"
  cp "$TEST_SOURCE_BINARY" "$TEST_RUNTIME_DIR/hospital_system"
  cp "$TEST_SOURCE_CONFIG" "$TEST_RUNTIME_DIR/config/config.txt"
}

test_fast_config() {
  sed -i \
    -e 's/^TIME_UNIT_MS=.*/TIME_UNIT_MS=2/' \
    -e 's/^MAX_EMERGENCY_PATIENTS=.*/MAX_EMERGENCY_PATIENTS=100/' \
    -e 's/^MAX_APPOINTMENTS=.*/MAX_APPOINTMENTS=100/' \
    -e 's/^MAX_SURGERIES_PENDING=.*/MAX_SURGERIES_PENDING=30/' \
    -e 's/^TRIAGE_SIMULTANEOUS_PATIENTS=.*/TRIAGE_SIMULTANEOUS_PATIENTS=4/' \
    -e 's/^TRIAGE_CRITICAL_STABILITY=.*/TRIAGE_CRITICAL_STABILITY=1/' \
    -e 's/^TRIAGE_EMERGENCY_DURATION=.*/TRIAGE_EMERGENCY_DURATION=1/' \
    -e 's/^TRIAGE_APPOINTMENT_DURATION=.*/TRIAGE_APPOINTMENT_DURATION=1/' \
    -e 's/^B01_MIN_DURATION=.*/B01_MIN_DURATION=1/' \
    -e 's/^B01_MAX_DURATION=.*/B01_MAX_DURATION=1/' \
    -e 's/^B02_MIN_DURATION=.*/B02_MIN_DURATION=1/' \
    -e 's/^B02_MAX_DURATION=.*/B02_MAX_DURATION=1/' \
    -e 's/^B03_MIN_DURATION=.*/B03_MIN_DURATION=1/' \
    -e 's/^B03_MAX_DURATION=.*/B03_MAX_DURATION=1/' \
    -e 's/^CLEANUP_MIN_TIME=.*/CLEANUP_MIN_TIME=1/' \
    -e 's/^CLEANUP_MAX_TIME=.*/CLEANUP_MAX_TIME=1/' \
    -e 's/^PHARMACY_PREPARATION_TIME_MIN=.*/PHARMACY_PREPARATION_TIME_MIN=1/' \
    -e 's/^PHARMACY_PREPARATION_TIME_MAX=.*/PHARMACY_PREPARATION_TIME_MAX=1/' \
    -e 's/^LAB1_TEST_MIN_DURATION=.*/LAB1_TEST_MIN_DURATION=1/' \
    -e 's/^LAB1_TEST_MAX_DURATION=.*/LAB1_TEST_MAX_DURATION=1/' \
    -e 's/^LAB2_TEST_MIN_DURATION=.*/LAB2_TEST_MIN_DURATION=1/' \
    -e 's/^LAB2_TEST_MAX_DURATION=.*/LAB2_TEST_MAX_DURATION=1/' \
    "$TEST_RUNTIME_DIR/config/config.txt"
}

test_start() {
  (cd "$TEST_RUNTIME_DIR" && exec ./hospital_system) >"$TEST_OUTPUT" 2>&1 &
  TEST_PID=$!
  for _ in $(seq 1 200); do
    [[ -p "$TEST_RUNTIME_DIR/input_pipe" ]] && return 0
    kill -0 "$TEST_PID" 2>/dev/null || {
      cat "$TEST_OUTPUT" >&2
      return 1
    }
    sleep 0.025
  done
  echo "timed out waiting for input_pipe" >&2
  return 1
}

test_send() {
  printf '%s\n' "$@" >"$TEST_RUNTIME_DIR/input_pipe"
}

test_wait_log_count() {
  local pattern=$1 expected=$2
  for _ in $(seq 1 600); do
    local actual=0
    if [[ -f "$TEST_RUNTIME_DIR/logs/hospital_log.txt" ]]; then
      actual=$(grep -cE "$pattern" "$TEST_RUNTIME_DIR/logs/hospital_log.txt" || true)
    fi
    [[ $actual -ge $expected ]] && return 0
    kill -0 "$TEST_PID" 2>/dev/null || {
      cat "$TEST_OUTPUT" >&2
      return 1
    }
    sleep 0.025
  done
  echo "timed out: expected $expected log entries matching $pattern" >&2
  return 1
}

test_snapshot() {
  kill -USR2 "$TEST_PID"
  for _ in $(seq 1 200); do
    TEST_SNAPSHOT=$(find "$TEST_RUNTIME_DIR/results/stats_snapshots" -type f -name 'stats_*.txt' 2>/dev/null | head -1)
    [[ -n ${TEST_SNAPSHOT:-} ]] && return 0
    sleep 0.025
  done
  echo "timed out waiting for statistics snapshot" >&2
  return 1
}

test_stop() {
  local signal=${1:-INT}
  kill -"$signal" "$TEST_PID" 2>/dev/null || true
  for _ in $(seq 1 400); do
    kill -0 "$TEST_PID" 2>/dev/null || break
    sleep 0.025
  done
  if kill -0 "$TEST_PID" 2>/dev/null; then
    echo "system did not stop within 10 seconds" >&2
    return 1
  fi
  wait "$TEST_PID"
  TEST_PID=""
  [[ ! -e "$TEST_RUNTIME_DIR/input_pipe" ]]
  [[ ! -e "$TEST_RUNTIME_DIR/hospital_system.lock" ]]
}

test_cleanup() {
  if [[ -n ${TEST_PID:-} ]] && kill -0 "$TEST_PID" 2>/dev/null; then
    kill -TERM "$TEST_PID" 2>/dev/null || true
    wait "$TEST_PID" 2>/dev/null || true
  fi
  if [[ ${TEST_KEEP_RUNTIME:-0} == 1 ]]; then
    echo "preserved test runtime: $TEST_RUNTIME_DIR" >&2
  elif [[ -n ${TEST_RUNTIME_DIR:-} ]]; then
    rm -rf "$TEST_RUNTIME_DIR"
  fi
}
