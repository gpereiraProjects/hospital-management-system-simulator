#!/usr/bin/env bash
set -euo pipefail

BINARY=${BINARY:-build/release/bin/hospital_system}
source_binary=$(readlink -f "$BINARY")
source_config=$(readlink -f config/config.txt)
runtime_dir=$(mktemp -d)
output=$(mktemp)
parent_pid=""

cleanup() {
  if [[ -n "$parent_pid" ]] && kill -0 "$parent_pid" 2>/dev/null; then
    kill -TERM "$parent_pid" 2>/dev/null || true
    wait "$parent_pid" 2>/dev/null || true
  fi
  rm -f "$output"
  rm -rf "$runtime_dir"
}
trap cleanup EXIT

mkdir -p "$runtime_dir/config"
cp "$source_binary" "$runtime_dir/hospital_system"
cp "$source_config" "$runtime_dir/config/config.txt"

mkdir -p "$runtime_dir/invalid/config"
cp "$source_binary" "$runtime_dir/invalid/hospital_system"
cp "$source_config" "$runtime_dir/invalid/config/config.txt"
sed -i 's/^TIME_UNIT_MS=.*/TIME_UNIT_MS=not-a-number/' "$runtime_dir/invalid/config/config.txt"
set +e
(cd "$runtime_dir/invalid" && ./hospital_system >/dev/null 2>&1)
invalid_status=$?
set -e
[[ $invalid_status -ne 0 ]]

sed -i \
  -e 's/^TIME_UNIT_MS=.*/TIME_UNIT_MS=5/' \
  -e 's/^TRIAGE_EMERGENCY_DURATION=.*/TRIAGE_EMERGENCY_DURATION=1/' \
  -e 's/^TRIAGE_APPOINTMENT_DURATION=.*/TRIAGE_APPOINTMENT_DURATION=1/' \
  -e 's/^TRIAGE_SIMULTANEOUS_PATIENTS=.*/TRIAGE_SIMULTANEOUS_PATIENTS=1/' \
  -e 's/^TRIAGE_CRITICAL_STABILITY=.*/TRIAGE_CRITICAL_STABILITY=99/' \
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
  "$runtime_dir/config/config.txt"

cd "$runtime_dir"
./hospital_system >"$output" 2>&1 &
parent_pid=$!
for _ in $(seq 1 100); do
  [[ -p input_pipe ]] && break
  kill -0 "$parent_pid" 2>/dev/null || {
    cat "$output"
    exit 1
  }
  sleep 0.05
done
[[ -p input_pipe ]]

exec 3>input_pipe
printf '%s\n' 'RESTOCK ANALG_A quantity:50' >&3
printf '%s\n' 'PHARMACY_REQUEST REQ301 init:0 priority:NORMAL items:[ANALG_A:950]' >&3
printf '%s\n' 'LAB_REQUEST LAB301 init:0 priority:NORMAL lab:LAB1 tests:[HEMO,GLIC]' >&3
printf '%s\n' 'LAB_REQUEST LAB302 init:0 priority:URGENT lab:LAB2 tests:[RENAL,HEPAT]' >&3
printf '%s\n' 'EMERGENCY PAT301 init:0 triage:1 stability:100 tests:[HEMO] meds:[ANALG_A]' >&3
printf '%s\n' 'APPOINTMENT PAT302 init:0 scheduled:2 doctor:CARDIO tests:[GLIC]' >&3
printf '%s\n' 'SURGERY SUR301 init:0 type:ORTHO scheduled:2 urgency:HIGH tests:[PREOP] meds:[ANEST_C]' >&3
printf '%s\n' 'SURGERY SURLOW init:0 type:ORTHO scheduled:20 urgency:LOW tests:[PREOP] meds:[ANEST_C]' >&3
printf '%s\n' 'SURGERY SURHIGH init:0 type:ORTHO scheduled:20 urgency:HIGH tests:[PREOP] meds:[ANEST_C]' >&3
exec 3>&-

for _ in $(seq 1 300); do
  if grep -q '\[PHARMACY\].*\[DELIVERED\] REQ301' logs/hospital_log.txt &&
    grep -q '\[LAB\].*\[COMPLETE\] LAB301' logs/hospital_log.txt &&
    grep -q '\[TRIAGE\].*\[COMPLETE\] PAT301' logs/hospital_log.txt &&
    grep -q '\[TRIAGE\].*\[COMPLETE\] PAT302' logs/hospital_log.txt &&
    grep -q '\[SURGERY\].*\[COMPLETE\] SUR301' logs/hospital_log.txt &&
    grep -q '\[SURGERY\].*\[COMPLETE\] SURLOW' logs/hospital_log.txt &&
    grep -q '\[SURGERY\].*\[COMPLETE\] SURHIGH' logs/hospital_log.txt &&
    grep -q '\[PHARMACY\].*\[RESTOCK_AUTO\]' logs/hospital_log.txt; then
    break
  fi
  kill -0 "$parent_pid" 2>/dev/null || {
    cat "$output"
    exit 1
  }
  sleep 0.05
done

grep -q '\[PHARMACY\].*\[RESTOCK_MANUAL\] ANALGESICO_A +50' logs/hospital_log.txt
grep -q '\[SURGERY\].*\[COMPLETE\] SUR301' logs/hospital_log.txt
high_line=$(grep -n '\[SURGERY\].*\[START\] SURHIGH' logs/hospital_log.txt | head -1 | cut -d: -f1)
low_line=$(grep -n '\[SURGERY\].*\[START\] SURLOW' logs/hospital_log.txt | head -1 | cut -d: -f1)
[[ $high_line -lt $low_line ]]
grep -q 'ANALGESICO_A' results/pharmacy_deliveries/pharmacy_delivery_REQ301_*.txt
grep -q 'Quantidade: 950 unidades' results/pharmacy_deliveries/pharmacy_delivery_REQ301_*.txt
grep -q 'HEMO' results/lab_results/lab_results_LAB301_*.txt
grep -q 'GLIC' results/lab_results/lab_results_LAB301_*.txt
grep -q 'PREOP' results/lab_results/lab_results_SUR301_*.txt

# Keep the sole triage worker waiting for unavailable stock so the next
# emergency remains queued long enough to exercise critical/transfer handling.
exec 3>input_pipe
printf '%s\n' 'EMERGENCY BLK301 init:0 triage:1 stability:100 tests:[] meds:[SUPLEMENTO_O:100000]' >&3
printf '%s\n' 'EMERGENCY TRN301 init:0 triage:2 stability:100 tests:[] meds:[]' >&3
exec 3>&-
for _ in $(seq 1 200); do
  grep -q '\[TRIAGE\].*\[TRANSFER\] TRN301' logs/hospital_log.txt && break
  sleep 0.05
done
grep -q '\[TRIAGE\].*\[CRITICAL\] TRN301' logs/hospital_log.txt
grep -q '\[TRIAGE\].*\[TRANSFER\] TRN301' logs/hospital_log.txt

printf '%s\n' 'STATUS PHARMACY' >input_pipe
printf '%s\n' 'STATUS LAB' >input_pipe
sleep 0.1
grep -q 'PHARMACY medication=ANALGESICO_A' "$output"
grep -q 'LAB lab=LAB1' "$output"

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
[[ ! -e input_pipe ]]
[[ ! -e hospital_system.lock ]]

echo "phase 3 functional integration: all tests passed"
