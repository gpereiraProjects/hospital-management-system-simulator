#!/usr/bin/env bash
set -euo pipefail

source tests/test_helpers.sh
test_setup
trap test_cleanup EXIT
test_fast_config
test_start

commands=()
for i in $(seq -w 1 40); do
  commands+=("EMERGENCY EMG$i init:0 triage:$((10#$i % 5 + 1)) stability:100 tests:[] meds:[]")
done
for i in $(seq -w 1 20); do
  commands+=("APPOINTMENT APP$i init:0 scheduled:1 doctor:CARDIO tests:[]")
done
for i in $(seq -w 1 15); do
  commands+=("PHARMACY_REQUEST PHA$i init:0 priority:NORMAL items:[VITAMINA_N:1]")
done
for i in $(seq -w 1 15); do
  commands+=("LAB_REQUEST LAB$i init:0 priority:NORMAL lab:LAB1 tests:[HEMO]")
done
for i in $(seq -w 1 10); do
  commands+=("SURGERY SUR$i init:0 type:ORTHO scheduled:1 urgency:MEDIUM tests:[PREOP] meds:[ANEST_C]")
done
[[ ${#commands[@]} -eq 100 ]]
exec 3>"$TEST_RUNTIME_DIR/input_pipe"
for command in "${commands[@]}"; do
  printf '%s\n' "$command" >&3
  sleep 0.005
done
exec 3>&-

test_wait_log_count '\[TRIAGE\].*\[COMPLETE\] (EMG|APP)[0-9][0-9]' 60
test_wait_log_count '\[PHARMACY\].*\[DELIVERED\] PHA[0-9][0-9]' 15
test_wait_log_count '\[LAB\].*\[COMPLETE\] LAB[0-9][0-9]' 15
test_wait_log_count '\[SURGERY\].*\[COMPLETE\] SUR[0-9][0-9]' 10

log="$TEST_RUNTIME_DIR/logs/hospital_log.txt"
[[ $(grep -c '\[INPUT\].*\[CMD_ACCEPT\]' "$log") -eq 100 ]]
! grep -Eq '\[(QUEUE_REJECT|REJECT|PARSE_ERROR|ERROR)\]' "$log"
[[ $(find "$TEST_RUNTIME_DIR/results/pharmacy_deliveries" -type f | wc -l) -eq 25 ]]
[[ $(find "$TEST_RUNTIME_DIR/results/lab_results" -type f | wc -l) -eq 25 ]]

test_snapshot
grep -q 'Total Emergências: 40' "$TEST_SNAPSHOT"
grep -q 'Total Consultas: 20' "$TEST_SNAPSHOT"
grep -q 'Cirurgias Concluídas: 10' "$TEST_SNAPSHOT"
grep -q 'Total Pedidos: 25' "$TEST_SNAPSHOT"
grep -q 'Total Análises: 25 (Lab1) + 10 (Lab2)' "$TEST_SNAPSHOT"
grep -q 'Testes PREOP: 10' "$TEST_SNAPSHOT"
grep -q 'Erros Sistema: 0' "$TEST_SNAPSHOT"
test_stop
echo 'bounded 100-command stress scenario: all assertions passed'
