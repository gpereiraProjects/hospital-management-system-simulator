#include "../include/command.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const valid_tests[] = {"HEMO", "GLIC", "COLEST", "RENAL", "HEPAT", "PREOP"};

static const char *const valid_medications[] = {"ANALGESICO_A",
                                                "ANTIBIOTICO_B",
                                                "ANESTESICO_C",
                                                "SEDATIVO_D",
                                                "ANTIINFLAMATORIO_E",
                                                "CARDIOVASCULAR_F",
                                                "NEUROLOGICO_G",
                                                "ORTOPEDICO_H",
                                                "HEMOSTATIC_I",
                                                "ANTICOAGULANTE_J",
                                                "INSULINA_K",
                                                "ANALGESICO_FORTE_L",
                                                "ANTIBIOTICO_FORTE_M",
                                                "VITAMINA_N",
                                                "SUPLEMENTO_O",
                                                "ANALG_A",
                                                "ANTIB_B",
                                                "ANEST_C",
                                                "CARDIOV_F",
                                                "NEUROLOG_G",
                                                "ORTOPED_H"};

static void set_error(char *error, size_t error_size, const char *format, ...) {
  if (error == NULL || error_size == 0)
    return;

  va_list args;
  va_start(args, format);
  vsnprintf(error, error_size, format, args);
  va_end(args);
}

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;

  char *end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1]))
    end--;
  *end = '\0';
  return text;
}

static int value_in_list(const char *value, const char *const *values, size_t value_count) {
  for (size_t i = 0; i < value_count; i++) {
    if (strcmp(value, values[i]) == 0)
      return 1;
  }
  return 0;
}

static int valid_identifier(const char *identifier) {
  size_t length = strlen(identifier);
  if (length < 5 || length >= MAX_PATIENT_ID)
    return 0;

  for (size_t i = 0; i < length; i++) {
    unsigned char value = (unsigned char)identifier[i];
    if (!isalnum(value) && value != '_' && value != '-')
      return 0;
  }
  return 1;
}

static const char *find_field(const char *line, const char *field) {
  const char *position = strstr(line, field);
  if (position == NULL)
    return NULL;

  position += strlen(field);
  while (isspace((unsigned char)*position))
    position++;
  return position;
}

static int parse_integer_field(const char *line, const char *field, int *value) {
  const char *position = find_field(line, field);
  if (position == NULL)
    return -1;

  errno = 0;
  char *end = NULL;
  long parsed = strtol(position, &end, 10);
  if (position == end || errno != 0 || parsed < INT_MIN || parsed > INT_MAX)
    return -1;

  *value = (int)parsed;
  return 0;
}

static int parse_word_field(const char *line, const char *field, char *value, size_t value_size) {
  const char *position = find_field(line, field);
  if (position == NULL || *position == '\0' || *position == '[')
    return -1;

  size_t length = strcspn(position, " \t\r\n");
  if (length == 0 || length >= value_size)
    return -1;

  memcpy(value, position, length);
  value[length] = '\0';
  return 0;
}

static int extract_list(const char *line, const char *field, char *contents, size_t contents_size) {
  const char *position = find_field(line, field);
  if (position == NULL || *position != '[')
    return -1;

  const char *end = strchr(position + 1, ']');
  if (end == NULL)
    return -1;

  size_t length = (size_t)(end - position - 1);
  if (length >= contents_size)
    return -1;

  memcpy(contents, position + 1, length);
  contents[length] = '\0';
  return 0;
}

static int parse_tests(const char *line, command_t *command, char *error, size_t error_size) {
  char contents[128];
  if (extract_list(line, "tests:", contents, sizeof(contents)) != 0) {
    set_error(error, error_size, "missing or malformed tests list");
    return -1;
  }

  char *list = trim(contents);
  if (*list == '\0')
    return 0;

  char *save = NULL;
  for (char *item = strtok_r(list, ",", &save); item != NULL; item = strtok_r(NULL, ",", &save)) {
    item = trim(item);
    if (command->test_count >= MAX_COMMAND_TESTS) {
      set_error(error, error_size, "at most %d tests are allowed", MAX_COMMAND_TESTS);
      return -1;
    }
    if (!value_in_list(item, valid_tests, sizeof(valid_tests) / sizeof(valid_tests[0]))) {
      set_error(error, error_size, "unknown test '%s'", item);
      return -1;
    }
    snprintf(command->tests[command->test_count], MAX_TEST_NAME, "%s", item);
    command->test_count++;
  }
  return 0;
}

static int valid_medication(const char *name) {
  return value_in_list(name, valid_medications,
                       sizeof(valid_medications) / sizeof(valid_medications[0]));
}

static const char *canonical_medication(const char *name) {
  static const struct {
    const char *alias;
    const char *canonical;
  } aliases[] = {{"ANALG_A", "ANALGESICO_A"},     {"ANTIB_B", "ANTIBIOTICO_B"},
                 {"ANEST_C", "ANESTESICO_C"},     {"CARDIOV_F", "CARDIOVASCULAR_F"},
                 {"NEUROLOG_G", "NEUROLOGICO_G"}, {"ORTOPED_H", "ORTOPEDICO_H"}};
  for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++)
    if (strcmp(name, aliases[i].alias) == 0)
      return aliases[i].canonical;
  return name;
}

static int parse_medications(const char *line, const char *field, int quantities_required,
                             command_t *command, char *error, size_t error_size) {
  char contents[256];
  if (extract_list(line, field, contents, sizeof(contents)) != 0) {
    set_error(error, error_size, "missing or malformed medication list");
    return -1;
  }

  char *list = trim(contents);
  if (*list == '\0')
    return 0;

  char *save = NULL;
  for (char *item = strtok_r(list, ",", &save); item != NULL; item = strtok_r(NULL, ",", &save)) {
    item = trim(item);
    if (command->medication_count >= MAX_COMMAND_MEDICATIONS) {
      set_error(error, error_size, "at most %d medications are allowed", MAX_COMMAND_MEDICATIONS);
      return -1;
    }

    char *quantity_text = strchr(item, ':');
    int quantity = 0;
    if (quantity_text != NULL) {
      *quantity_text++ = '\0';
      quantity_text = trim(quantity_text);
      errno = 0;
      char *end = NULL;
      long parsed = strtol(quantity_text, &end, 10);
      if (quantity_text == end || *trim(end) != '\0' || errno != 0 || parsed <= 0 ||
          parsed > INT_MAX) {
        set_error(error, error_size, "invalid quantity for medication '%s'", item);
        return -1;
      }
      quantity = (int)parsed;
    } else if (quantities_required) {
      set_error(error, error_size, "medication '%s' requires a quantity", item);
      return -1;
    }

    item = trim(item);
    if (!valid_medication(item)) {
      set_error(error, error_size, "unknown medication '%s'", item);
      return -1;
    }

    medication_request_t *request = &command->medications[command->medication_count++];
    snprintf(request->name, sizeof(request->name), "%s", canonical_medication(item));
    request->quantity = quantity;
  }
  return 0;
}

static int parse_prefix(const char *line, char *kind, size_t kind_size, char *id, size_t id_size) {
  char buffer[MAX_COMMAND_LENGTH];
  snprintf(buffer, sizeof(buffer), "%s", line);
  char *save = NULL;
  char *kind_token = strtok_r(buffer, " \t", &save);
  char *id_token = strtok_r(NULL, " \t", &save);
  if (kind_token == NULL || id_token == NULL)
    return -1;
  if (strlen(kind_token) >= kind_size || strlen(id_token) >= id_size)
    return -1;
  snprintf(kind, kind_size, "%s", kind_token);
  snprintf(id, id_size, "%s", id_token);
  return 0;
}

static int parse_priority(const char *value, command_priority_t *priority) {
  if (strcmp(value, "URGENT") == 0)
    *priority = COMMAND_PRIORITY_URGENT;
  else if (strcmp(value, "HIGH") == 0)
    *priority = COMMAND_PRIORITY_HIGH;
  else if (strcmp(value, "NORMAL") == 0)
    *priority = COMMAND_PRIORITY_NORMAL;
  else
    return -1;
  return 0;
}

static int require_nonnegative_time(int value, const char *field, char *error, size_t error_size) {
  if (value < 0) {
    set_error(error, error_size, "%s must be non-negative", field);
    return -1;
  }
  return 0;
}

command_parse_result_t command_parse(const char *line, command_t *command, char *error,
                                     size_t error_size) {
  if (line == NULL || command == NULL) {
    set_error(error, error_size, "null command input");
    return COMMAND_PARSE_ERROR;
  }

  while (isspace((unsigned char)*line))
    line++;
  if (*line == '\0' || *line == '#')
    return COMMAND_PARSE_IGNORED;
  if (strlen(line) >= MAX_COMMAND_LENGTH) {
    set_error(error, error_size, "command exceeds %d bytes", MAX_COMMAND_LENGTH - 1);
    return COMMAND_PARSE_ERROR;
  }

  memset(command, 0, sizeof(*command));
  command->init_time = -1;
  command->scheduled_time = -1;
  snprintf(command->raw, sizeof(command->raw), "%s", line);

  char kind[32];
  char id[MAX_MED_NAME];
  if (parse_prefix(line, kind, sizeof(kind), id, sizeof(id)) != 0) {
    set_error(error, error_size, "command type and identifier are required");
    return COMMAND_PARSE_ERROR;
  }

  if (strcmp(kind, "STATUS") == 0) {
    command->kind = COMMAND_STATUS;
    const char *const components[] = {"ALL", "TRIAGE", "SURGERY", "PHARMACY", "LAB"};
    if (!value_in_list(id, components, sizeof(components) / sizeof(components[0]))) {
      set_error(error, error_size, "unknown status component '%s'", id);
      return COMMAND_PARSE_ERROR;
    }
    memcpy(command->status_component, id, strlen(id) + 1);
    return COMMAND_PARSE_OK;
  }

  if (strcmp(kind, "RESTOCK") == 0) {
    command->kind = COMMAND_RESTOCK;
    if (!valid_medication(id)) {
      set_error(error, error_size, "unknown medication '%s'", id);
      return COMMAND_PARSE_ERROR;
    }
    snprintf(command->restock_medication, sizeof(command->restock_medication), "%s",
             canonical_medication(id));
    if (parse_integer_field(line, "quantity:", &command->restock_quantity) != 0 ||
        command->restock_quantity <= 0) {
      set_error(error, error_size, "RESTOCK requires a positive quantity");
      return COMMAND_PARSE_ERROR;
    }
    return COMMAND_PARSE_OK;
  }

  if (!valid_identifier(id)) {
    set_error(error, error_size, "identifier must contain 5-15 letters, digits, '_' or '-'");
    return COMMAND_PARSE_ERROR;
  }
  memcpy(command->id, id, strlen(id) + 1);

  if (parse_integer_field(line, "init:", &command->init_time) != 0 ||
      require_nonnegative_time(command->init_time, "init", error, error_size) != 0) {
    if (error != NULL && error[0] == '\0')
      set_error(error, error_size, "missing or invalid init time");
    return COMMAND_PARSE_ERROR;
  }

  if (strcmp(kind, "EMERGENCY") == 0) {
    command->kind = COMMAND_EMERGENCY;
    if (parse_integer_field(line, "triage:", &command->triage_level) != 0 ||
        command->triage_level < 1 || command->triage_level > 5) {
      set_error(error, error_size, "triage must be between 1 and 5");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_integer_field(line, "stability:", &command->stability) != 0 ||
        command->stability < 100 || command->stability > 1000) {
      set_error(error, error_size, "stability must be between 100 and 1000");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_tests(line, command, error, error_size) != 0 ||
        parse_medications(line, "meds:", 0, command, error, error_size) != 0)
      return COMMAND_PARSE_ERROR;
  } else if (strcmp(kind, "APPOINTMENT") == 0) {
    command->kind = COMMAND_APPOINTMENT;
    if (parse_integer_field(line, "scheduled:", &command->scheduled_time) != 0 ||
        command->scheduled_time <= command->init_time) {
      set_error(error, error_size, "scheduled must be greater than init");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_word_field(line, "doctor:", command->doctor, sizeof(command->doctor)) != 0 ||
        (strcmp(command->doctor, "CARDIO") != 0 && strcmp(command->doctor, "ORTHO") != 0 &&
         strcmp(command->doctor, "NEURO") != 0)) {
      set_error(error, error_size, "doctor must be CARDIO, ORTHO, or NEURO");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_tests(line, command, error, error_size) != 0)
      return COMMAND_PARSE_ERROR;
  } else if (strcmp(kind, "SURGERY") == 0) {
    command->kind = COMMAND_SURGERY;
    if (parse_word_field(line, "type:", command->surgery_type, sizeof(command->surgery_type)) !=
            0 ||
        (strcmp(command->surgery_type, "CARDIO") != 0 &&
         strcmp(command->surgery_type, "ORTHO") != 0 &&
         strcmp(command->surgery_type, "NEURO") != 0)) {
      set_error(error, error_size, "surgery type must be CARDIO, ORTHO, or NEURO");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_integer_field(line, "scheduled:", &command->scheduled_time) != 0 ||
        command->scheduled_time <= command->init_time) {
      set_error(error, error_size, "scheduled must be greater than init");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_word_field(line, "urgency:", command->urgency, sizeof(command->urgency)) != 0 ||
        (strcmp(command->urgency, "LOW") != 0 && strcmp(command->urgency, "MEDIUM") != 0 &&
         strcmp(command->urgency, "HIGH") != 0)) {
      set_error(error, error_size, "urgency must be LOW, MEDIUM, or HIGH");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_tests(line, command, error, error_size) != 0 || command->test_count == 0)
      return COMMAND_PARSE_ERROR;
    int has_preop = 0;
    for (size_t i = 0; i < command->test_count; i++)
      has_preop |= strcmp(command->tests[i], "PREOP") == 0;
    if (!has_preop) {
      set_error(error, error_size, "surgery requires a PREOP test");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_medications(line, "meds:", 0, command, error, error_size) != 0 ||
        command->medication_count == 0) {
      set_error(error, error_size, "surgery requires at least one medication");
      return COMMAND_PARSE_ERROR;
    }
  } else if (strcmp(kind, "PHARMACY_REQUEST") == 0) {
    command->kind = COMMAND_PHARMACY_REQUEST;
    char priority[16];
    if (parse_word_field(line, "priority:", priority, sizeof(priority)) != 0 ||
        parse_priority(priority, &command->priority) != 0) {
      set_error(error, error_size, "pharmacy priority must be URGENT, HIGH, or NORMAL");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_medications(line, "items:", 1, command, error, error_size) != 0 ||
        command->medication_count == 0) {
      set_error(error, error_size, "pharmacy request requires at least one item");
      return COMMAND_PARSE_ERROR;
    }
  } else if (strcmp(kind, "LAB_REQUEST") == 0) {
    command->kind = COMMAND_LAB_REQUEST;
    char priority[16];
    if (parse_word_field(line, "priority:", priority, sizeof(priority)) != 0 ||
        parse_priority(priority, &command->priority) != 0 ||
        command->priority == COMMAND_PRIORITY_HIGH) {
      set_error(error, error_size, "laboratory priority must be URGENT or NORMAL");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_word_field(line, "lab:", command->lab, sizeof(command->lab)) != 0 ||
        (strcmp(command->lab, "LAB1") != 0 && strcmp(command->lab, "LAB2") != 0 &&
         strcmp(command->lab, "BOTH") != 0)) {
      set_error(error, error_size, "lab must be LAB1, LAB2, or BOTH");
      return COMMAND_PARSE_ERROR;
    }
    if (parse_tests(line, command, error, error_size) != 0 || command->test_count == 0) {
      set_error(error, error_size, "laboratory request requires at least one test");
      return COMMAND_PARSE_ERROR;
    }
    for (size_t i = 0; i < command->test_count; i++) {
      int lab1_test =
          strcmp(command->tests[i], "HEMO") == 0 || strcmp(command->tests[i], "GLIC") == 0;
      int lab2_test = strcmp(command->tests[i], "COLEST") == 0 ||
                      strcmp(command->tests[i], "RENAL") == 0 ||
                      strcmp(command->tests[i], "HEPAT") == 0;
      if ((strcmp(command->lab, "LAB1") == 0 && !lab1_test) ||
          (strcmp(command->lab, "LAB2") == 0 && !lab2_test)) {
        set_error(error, error_size, "test '%s' is incompatible with %s", command->tests[i],
                  command->lab);
        return COMMAND_PARSE_ERROR;
      }
    }
  } else {
    set_error(error, error_size, "unknown command '%s'", kind);
    return COMMAND_PARSE_ERROR;
  }

  return COMMAND_PARSE_OK;
}

const char *command_kind_name(command_kind_t kind) {
  switch (kind) {
  case COMMAND_EMERGENCY:
    return "EMERGENCY";
  case COMMAND_APPOINTMENT:
    return "APPOINTMENT";
  case COMMAND_SURGERY:
    return "SURGERY";
  case COMMAND_PHARMACY_REQUEST:
    return "PHARMACY_REQUEST";
  case COMMAND_LAB_REQUEST:
    return "LAB_REQUEST";
  case COMMAND_RESTOCK:
    return "RESTOCK";
  case COMMAND_STATUS:
    return "STATUS";
  default:
    return "UNKNOWN";
  }
}
