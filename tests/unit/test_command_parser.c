#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"

static int failures;

static void expect_valid(const char *text, command_kind_t kind) {
  command_t command;
  char error[160] = "";
  command_parse_result_t result = command_parse(text, &command, error, sizeof(error));
  if (result != COMMAND_PARSE_OK || command.kind != kind) {
    fprintf(stderr, "Expected valid command, got '%s': %s\n", text, error);
    failures++;
  }
}

static void expect_invalid(const char *text) {
  command_t command;
  char error[160] = "";
  if (command_parse(text, &command, error, sizeof(error)) != COMMAND_PARSE_ERROR) {
    fprintf(stderr, "Expected invalid command: %s\n", text);
    failures++;
  }
}

int main(void) {
  expect_valid("EMERGENCY PAT001 init:0 triage:1 stability:500 tests:[HEMO] meds:[ANALG_A]",
               COMMAND_EMERGENCY);
  expect_valid("APPOINTMENT PAT002 init:1 scheduled:2 doctor:CARDIO tests:[GLIC]",
               COMMAND_APPOINTMENT);
  expect_valid("SURGERY PAT003 init:1 type:ORTHO scheduled:5 urgency:HIGH "
               "tests:[HEMO,PREOP] meds:[ANEST_C]",
               COMMAND_SURGERY);
  expect_valid("PHARMACY_REQUEST PAT004 init:1 priority:URGENT items:[ANALGESICO_A:2]",
               COMMAND_PHARMACY_REQUEST);
  expect_valid("LAB_REQUEST PAT005 init:1 priority:NORMAL lab:BOTH tests:[HEMO,RENAL]",
               COMMAND_LAB_REQUEST);
  expect_valid("RESTOCK ANTIINFLAMATORIO_E quantity:25", COMMAND_RESTOCK);
  expect_valid("STATUS ALL", COMMAND_STATUS);

  expect_invalid("EMERGENCY X init:0 triage:1 stability:500 tests:[] meds:[]");
  expect_invalid("EMERGENCY PAT006 init:0 triage:9 stability:500 tests:[] meds:[]");
  expect_invalid("EMERGENCY PAT006 init:0 triage:2 stability:99 tests:[] meds:[]");
  expect_invalid("SURGERY PAT007 init:1 type:CARDIO scheduled:4 urgency:HIGH "
                 "tests:[HEMO] meds:[ANEST_C]");
  expect_invalid("LAB_REQUEST PAT008 init:1 priority:HIGH lab:LAB1 tests:[HEMO]");
  expect_invalid("LAB_REQUEST PAT008 init:1 priority:NORMAL lab:LAB1 tests:[RENAL]");
  expect_invalid("UNKNOWN PAT009 init:0");

  command_t canonical;
  char error[160] = "";
  if (command_parse("PHARMACY_REQUEST PAT010 init:0 priority:HIGH items:[ANALG_A:2]",
                    &canonical, error, sizeof(error)) != COMMAND_PARSE_OK ||
      strcmp(canonical.medications[0].name, "ANALGESICO_A") != 0) {
    fprintf(stderr, "Medication aliases were not canonicalized: %s\n", error);
    failures++;
  }

  if (failures != 0)
    return EXIT_FAILURE;
  puts("command parser: all tests passed");
  return EXIT_SUCCESS;
}
