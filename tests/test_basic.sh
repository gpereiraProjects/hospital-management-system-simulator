#!/usr/bin/env bash
set -euo pipefail

source tests/test_helpers.sh
test_setup
trap test_cleanup EXIT
test_fast_config
test_start

test_send \
  'EMERGENCY BSC001 init:0 triage:1 stability:100 tests:[HEMO] meds:[]' \
  'APPOINTMENT BSC002 init:0 scheduled:1 doctor:CARDIO tests:[]' \
  'SURGERY BSC003 init:0 type:ORTHO scheduled:1 urgency:HIGH tests:[PREOP] meds:[ANEST_C]' \
  'PHARMACY_REQUEST BSC004 init:0 priority:URGENT items:[ANALG_A:2]'

test_wait_log_count '\[TRIAGE\].*\[COMPLETE\] BSC00[12]' 2
test_wait_log_count '\[SURGERY\].*\[COMPLETE\] BSC003' 1
test_wait_log_count '\[PHARMACY\].*\[DELIVERED\] BSC004' 1
test_snapshot

grep -q 'Total Emergências: 1' "$TEST_SNAPSHOT"
grep -q 'Total Consultas: 1' "$TEST_SNAPSHOT"
grep -q 'B02 (Ortopedia): 1 cirurgias' "$TEST_SNAPSHOT"
grep -q 'Cirurgias Concluídas: 1' "$TEST_SNAPSHOT"
grep -q 'Erros Sistema: 0' "$TEST_SNAPSHOT"
grep -q 'Estado: VALIDADO' "$TEST_RUNTIME_DIR"/results/lab_results/lab_results_BSC003_*.txt
grep -q 'Estado: ENTREGUE' "$TEST_RUNTIME_DIR"/results/pharmacy_deliveries/pharmacy_delivery_BSC004_*.txt

test_stop
echo 'basic black-box scenario: all assertions passed'
