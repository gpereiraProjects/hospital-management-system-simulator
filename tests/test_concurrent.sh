#!/usr/bin/env bash
set -euo pipefail

source tests/test_helpers.sh
test_setup
trap test_cleanup EXIT
test_fast_config
test_start

commands=()
for i in $(seq -w 1 12); do
  commands+=("EMERGENCY CON$i init:0 triage:$((10#$i % 5 + 1)) stability:100 tests:[] meds:[]")
done
commands+=(
  'SURGERY CRD001 init:0 type:CARDIO scheduled:1 urgency:HIGH tests:[PREOP] meds:[ANEST_C]'
  'SURGERY ORT001 init:0 type:ORTHO scheduled:1 urgency:HIGH tests:[PREOP] meds:[ANEST_C]'
  'SURGERY NEU001 init:0 type:NEURO scheduled:1 urgency:HIGH tests:[PREOP] meds:[ANEST_C]'
)
test_send "${commands[@]}"

test_wait_log_count '\[TRIAGE\].*\[COMPLETE\] CON' 12
test_wait_log_count '\[SURGERY\].*\[COMPLETE\] (CRD001|ORT001|NEU001)' 3
log="$TEST_RUNTIME_DIR/logs/hospital_log.txt"
grep -q '\[SURGERY\].*\[START\] CRD001' "$log"
grep -q '\[SURGERY\].*\[START\] ORT001' "$log"
grep -q '\[SURGERY\].*\[START\] NEU001' "$log"
[[ $(grep -c '\[TRIAGE\].*\[COMPLETE\] CON' "$log") -eq 12 ]]
! grep -Eq '\[(QUEUE_REJECT|REJECT|PARSE_ERROR|ERROR)\]' "$log"

test_snapshot
grep -q 'Total Emergências: 12' "$TEST_SNAPSHOT"
grep -q 'B01 (Cardiologia): 1 cirurgias' "$TEST_SNAPSHOT"
grep -q 'B02 (Ortopedia): 1 cirurgias' "$TEST_SNAPSHOT"
grep -q 'B03 (Neurologia): 1 cirurgias' "$TEST_SNAPSHOT"
grep -q 'Cirurgias Concluídas: 3' "$TEST_SNAPSHOT"
test_stop
echo 'concurrent multi-component scenario: all assertions passed'
