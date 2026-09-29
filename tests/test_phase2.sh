#!/usr/bin/env bash
set -euo pipefail

BINARY=${BINARY:-build/release/bin/hospital_system}
SOURCE_BINARY=$(readlink -f "$BINARY")
SOURCE_CONFIG=$(readlink -f config/config.txt)
runtime_dir=$(mktemp -d)
output=$(mktemp)
second_output=$(mktemp)
parent_pid=""

cleanup() {
  if [[ -n "$parent_pid" ]] && kill -0 "$parent_pid" 2>/dev/null; then
    kill -TERM "$parent_pid" 2>/dev/null || true
    wait "$parent_pid" 2>/dev/null || true
  fi
  rm -f "$output" "$second_output"
  rm -rf "$runtime_dir"
}
trap cleanup EXIT

mkdir -p "$runtime_dir/config"
cp "$SOURCE_BINARY" "$runtime_dir/hospital_system"
cp "$SOURCE_CONFIG" "$runtime_dir/config/config.txt"
cd "$runtime_dir"
BINARY=./hospital_system

"$BINARY" >"$output" 2>&1 &
parent_pid=$!

for _ in $(seq 1 100); do
  [[ -p input_pipe ]] && break
  kill -0 "$parent_pid" 2>/dev/null || {
    cat "$output"
    exit 1
  }
  sleep 0.05
done
for fifo in input_pipe triage_pipe surgery_pipe pharmacy_pipe lab_pipe; do
  [[ -p "$fifo" ]]
done

set +e
"$BINARY" >"$second_output" 2>&1
second_status=$?
set -e
[[ $second_status -ne 0 ]]
kill -0 "$parent_pid"

exec 3>input_pipe
printf '%s' 'EMERGENCY PAT900 init:0 triage:1 stability:500 ' >&3
sleep 0.05
printf '%s\n' 'tests:[HEMO] meds:[ANALG_A]' >&3
printf '%s\n' 'EMERGENCY BAD init:0 triage:9 stability:50 tests:[] meds:[]' >&3
printf '%s\n' 'STATUS ALL' >&3
exec 3>&-

sleep 0.3
kill -USR1 "$parent_pid"
kill -USR2 "$parent_pid"
sleep 0.3
kill -INT "$parent_pid"

for _ in $(seq 1 200); do
  kill -0 "$parent_pid" 2>/dev/null || break
  sleep 0.05
done
if kill -0 "$parent_pid" 2>/dev/null; then
  echo "system did not stop within 10 seconds" >&2
  exit 1
fi
wait "$parent_pid"
parent_pid=""

grep -q 'CMD_REJECT' logs/hospital_log.txt
grep -q 'PAT900' logs/hospital_log.txt
compgen -G 'results/stats_snapshots/stats_*.txt' >/dev/null
for fifo in input_pipe triage_pipe surgery_pipe pharmacy_pipe lab_pipe; do
  [[ ! -e "$fifo" ]]
done
[[ ! -e hospital_system.lock ]]

echo "phase 2 integration: all tests passed"
