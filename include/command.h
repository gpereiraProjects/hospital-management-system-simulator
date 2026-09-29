#ifndef COMMAND_H
#define COMMAND_H

#include "hospital.h"

#include <stddef.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_COMMAND_TESTS 3
#define MAX_COMMAND_MEDICATIONS 5
#define MAX_TEST_NAME 16
#define MAX_COMPONENT_NAME 16

typedef enum {
  COMMAND_EMERGENCY = 1,
  COMMAND_APPOINTMENT,
  COMMAND_SURGERY,
  COMMAND_PHARMACY_REQUEST,
  COMMAND_LAB_REQUEST,
  COMMAND_RESTOCK,
  COMMAND_STATUS
} command_kind_t;

typedef enum {
  COMMAND_PRIORITY_NORMAL = 1,
  COMMAND_PRIORITY_HIGH = 2,
  COMMAND_PRIORITY_URGENT = 3
} command_priority_t;

typedef struct {
  char name[MAX_MED_NAME];
  int quantity;
} medication_request_t;

typedef struct {
  command_kind_t kind;
  char id[MAX_PATIENT_ID];
  int init_time;
  int scheduled_time;
  int triage_level;
  int stability;
  command_priority_t priority;
  char surgery_type[MAX_COMPONENT_NAME];
  char urgency[MAX_COMPONENT_NAME];
  char doctor[MAX_COMPONENT_NAME];
  char lab[MAX_COMPONENT_NAME];
  char status_component[MAX_COMPONENT_NAME];
  char tests[MAX_COMMAND_TESTS][MAX_TEST_NAME];
  size_t test_count;
  medication_request_t medications[MAX_COMMAND_MEDICATIONS];
  size_t medication_count;
  char restock_medication[MAX_MED_NAME];
  int restock_quantity;
  char raw[MAX_COMMAND_LENGTH];
} command_t;

typedef enum {
  COMMAND_PARSE_ERROR = -1,
  COMMAND_PARSE_OK = 0,
  COMMAND_PARSE_IGNORED = 1
} command_parse_result_t;

command_parse_result_t command_parse(const char *line, command_t *command, char *error,
                                     size_t error_size);
const char *command_kind_name(command_kind_t kind);

#endif
