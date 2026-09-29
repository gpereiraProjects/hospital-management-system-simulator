#!/usr/bin/env bash
set -euo pipefail

tool=${1:-}
case "$tool" in
  memcheck | helgrind | drd) ;;
  *)
    echo "usage: $0 {memcheck|helgrind|drd}" >&2
    exit 2
    ;;
esac

source tests/test_helpers.sh
test_setup
trap test_cleanup EXIT
test_fast_config

reports="$TEST_RUNTIME_DIR/valgrind"
mkdir -p "$reports"
suppression_file=$(readlink -f tests/valgrind.supp)
common_args=(
  "--tool=$tool"
  --trace-children=yes
  "--log-file=$reports/$tool.%p.log"
)
if [[ $tool == memcheck ]]; then
  common_args+=(--leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect)
else
  common_args+=(--fair-sched=yes "--suppressions=$suppression_file")
fi

(cd "$TEST_RUNTIME_DIR" && exec valgrind "${common_args[@]}" ./hospital_system) >"$TEST_OUTPUT" 2>&1 &
TEST_PID=$!
for _ in $(seq 1 600); do
  [[ -p "$TEST_RUNTIME_DIR/input_pipe" ]] && break
  kill -0 "$TEST_PID" 2>/dev/null || {
    cat "$TEST_OUTPUT" >&2
    exit 1
  }
  sleep 0.05
done
[[ -p "$TEST_RUNTIME_DIR/input_pipe" ]]

test_send \
  'EMERGENCY INS001 init:0 triage:1 stability:100 tests:[] meds:[]' \
  'PHARMACY_REQUEST INS002 init:0 priority:URGENT items:[ANALG_A:1]' \
  'LAB_REQUEST INS003 init:0 priority:URGENT lab:LAB1 tests:[HEMO]' \
  'SURGERY INS004 init:0 type:NEURO scheduled:1 urgency:HIGH tests:[PREOP] meds:[ANEST_C]'
test_wait_log_count '\[TRIAGE\].*\[COMPLETE\] INS001' 1
test_wait_log_count '\[PHARMACY\].*\[DELIVERED\] INS002' 1
test_wait_log_count '\[LAB\].*\[COMPLETE\] INS003' 1
test_wait_log_count '\[SURGERY\].*\[COMPLETE\] INS004' 1
test_stop

mapfile -t report_files < <(find "$reports" -type f -name "$tool.*.log")
[[ ${#report_files[@]} -ge 5 ]]
if grep -EH 'ERROR SUMMARY: [1-9][0-9]* errors' "${report_files[@]}"; then
  if [[ ${TEST_SHOW_REPORTS:-0} == 1 ]]; then
    cat "${report_files[@]}"
  fi
  echo "$tool found runtime errors" >&2
  exit 1
fi
if [[ $tool == memcheck ]] &&
  grep -EH 'definitely lost: [1-9][0-9,]* bytes|indirectly lost: [1-9][0-9,]* bytes' \
    "${report_files[@]}"; then
  echo 'memcheck found definite leaks' >&2
  exit 1
fi

echo "$tool dynamic analysis: no reported errors"
