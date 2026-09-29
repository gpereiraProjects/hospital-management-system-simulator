#include "../include/config.h"
#include "../include/log.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Remove espaços em branco e newlines do início e fim
static void trim(char *s) {
  if (!s)
    return; // Proteção contra NULL
  char *p = s;
  int l = strlen(p);
  while (l > 0 && (p[l - 1] == '\n' || p[l - 1] == ' ' || p[l - 1] == '\r'))
    p[--l] = 0;
  while (*p && *p == ' ')
    p++;
  if (p != s)
    memmove(s, p, l + 1);
}

static int parse_integer(const char *text, int *value) {
  errno = 0;
  char *end = NULL;
  long parsed = strtol(text, &end, 10);
  if (text == end || *end != '\0' || errno != 0 || parsed < INT_MIN || parsed > INT_MAX)
    return -1;
  *value = (int)parsed;
  return 0;
}

static int valid_medication_name(const char *name) {
  static const char *const names[MAX_MED_TYPES] = {
      "ANALGESICO_A",        "ANTIBIOTICO_B",    "ANESTESICO_C",  "SEDATIVO_D",
      "ANTIINFLAMATORIO_E",  "CARDIOVASCULAR_F", "NEUROLOGICO_G", "ORTOPEDICO_H",
      "HEMOSTATIC_I",        "ANTICOAGULANTE_J", "INSULINA_K",    "ANALGESICO_FORTE_L",
      "ANTIBIOTICO_FORTE_M", "VITAMINA_N",       "SUPLEMENTO_O"};
  for (int i = 0; i < MAX_MED_TYPES; i++)
    if (strcmp(name, names[i]) == 0)
      return 1;
  return 0;
}

int load_config(const char *filename, system_config_t *config) {
  if (config == NULL)
    return -1;
  memset(config, 0, sizeof(*config));

  FILE *f = fopen(filename, "r");
  if (!f) {
    char err[256];
    snprintf(err, sizeof(err), "Nao foi possivel abrir %s", filename);
    log_event(LOG_ERROR, "CONFIG", "LOAD_FAIL", err);
    return -1;
  }

  char line[256];
  int med_count = 0;
  int invalid_value = 0;

  // Valores default caso falte algo
  config->time_unit_ms = 500;

  while (fgets(line, sizeof(line), f)) {
    trim(line);
    // Ignorar comentários e linhas vazias
    if (line[0] == '#' || line[0] == '\0')
      continue;

    char *key = strtok(line, "=");
    char *val = strtok(NULL, "=");
    char *extra_value = strtok(NULL, "=");

    if (!key || !val || extra_value != NULL) {
      invalid_value = 1;
      continue;
    }

    trim(key);
    trim(val);

    // --- Mapeamento Global ---
    if (strcmp(key, "TIME_UNIT_MS") == 0)
      invalid_value |= parse_integer(val, &config->time_unit_ms) != 0;
    else if (strcmp(key, "MAX_EMERGENCY_PATIENTS") == 0)
      invalid_value |= parse_integer(val, &config->max_emergency_patients) != 0;
    else if (strcmp(key, "MAX_APPOINTMENTS") == 0)
      invalid_value |= parse_integer(val, &config->max_appointments) != 0;
    else if (strcmp(key, "MAX_SURGERIES_PENDING") == 0)
      invalid_value |= parse_integer(val, &config->max_surgeries_pending) != 0;

    // --- Triagem ---
    else if (strcmp(key, "TRIAGE_SIMULTANEOUS_PATIENTS") == 0)
      invalid_value |= parse_integer(val, &config->triage_simultaneous_patients) != 0;
    else if (strcmp(key, "TRIAGE_CRITICAL_STABILITY") == 0)
      invalid_value |= parse_integer(val, &config->triage_critical_stability) != 0;
    else if (strcmp(key, "TRIAGE_EMERGENCY_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->triage_emergency_duration) != 0;
    else if (strcmp(key, "TRIAGE_APPOINTMENT_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->triage_appointment_duration) != 0;

    // --- Blocos Operatórios ---
    else if (strcmp(key, "B01_MIN_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->b01_min_duration) != 0;
    else if (strcmp(key, "B01_MAX_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->b01_max_duration) != 0;
    else if (strcmp(key, "B02_MIN_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->b02_min_duration) != 0;
    else if (strcmp(key, "B02_MAX_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->b02_max_duration) != 0;
    else if (strcmp(key, "B03_MIN_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->b03_min_duration) != 0;
    else if (strcmp(key, "B03_MAX_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->b03_max_duration) != 0;
    else if (strcmp(key, "CLEANUP_MIN_TIME") == 0)
      invalid_value |= parse_integer(val, &config->cleanup_min_time) != 0;
    else if (strcmp(key, "CLEANUP_MAX_TIME") == 0)
      invalid_value |= parse_integer(val, &config->cleanup_max_time) != 0;
    else if (strcmp(key, "MAX_MEDICAL_TEAMS") == 0)
      invalid_value |= parse_integer(val, &config->max_medical_teams) != 0;

    // --- Laboratórios ---
    else if (strcmp(key, "LAB1_TEST_MIN_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->lab1_min) != 0;
    else if (strcmp(key, "LAB1_TEST_MAX_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->lab1_max) != 0;
    else if (strcmp(key, "LAB2_TEST_MIN_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->lab2_min) != 0;
    else if (strcmp(key, "LAB2_TEST_MAX_DURATION") == 0)
      invalid_value |= parse_integer(val, &config->lab2_max) != 0;
    else if (strcmp(key, "MAX_SIMULTANEOUS_TESTS_LAB1") == 0)
      invalid_value |= parse_integer(val, &config->max_tests_lab1) != 0;
    else if (strcmp(key, "MAX_SIMULTANEOUS_TESTS_LAB2") == 0)
      invalid_value |= parse_integer(val, &config->max_tests_lab2) != 0;

    // --- Farmácia ---
    else if (strcmp(key, "PHARMACY_PREPARATION_TIME_MIN") == 0)
      invalid_value |= parse_integer(val, &config->pharm_prep_time_min) != 0;
    else if (strcmp(key, "PHARMACY_PREPARATION_TIME_MAX") == 0)
      invalid_value |= parse_integer(val, &config->pharm_prep_time_max) != 0;
    else if (strcmp(key, "AUTO_RESTOCK_ENABLED") == 0)
      invalid_value |= parse_integer(val, &config->auto_restock_enabled) != 0;
    else if (strcmp(key, "RESTOCK_QUANTITY_MULTIPLIER") == 0)
      invalid_value |= parse_integer(val, &config->restock_qty_multiplier) != 0;

    // --- Medicamentos ---
    // Detecta chaves que não são standard e assume que são medicamentos
    // O parser verifica se o valor tem formato "int:int"
    else if (med_count < 15) {
      char *stock_str = strtok(val, ":");
      char *threshold_str = strtok(NULL, ":");
      char *extra = strtok(NULL, ":");

      if (stock_str && threshold_str && extra == NULL && valid_medication_name(key)) {
        for (int i = 0; i < med_count; i++)
          if (strcmp(config->med_configs[i].name, key) == 0)
            invalid_value = 1;
        strncpy(config->med_configs[med_count].name, key, 29);
        config->med_configs[med_count].name[29] = '\0';
        invalid_value |=
            parse_integer(stock_str, &config->med_configs[med_count].initial_stock) != 0;
        invalid_value |=
            parse_integer(threshold_str, &config->med_configs[med_count].threshold) != 0;
        med_count++;
      } else
        invalid_value = 1;
    } else
      invalid_value = 1;
  }

  fclose(f);
  for (int i = 0; i < med_count; i++)
    if (config->med_configs[i].initial_stock < 0 || config->med_configs[i].threshold < 0 ||
        config->med_configs[i].threshold > config->med_configs[i].initial_stock)
      invalid_value = 1;
  if (config->time_unit_ms <= 0 || config->max_emergency_patients <= 0 ||
      config->max_appointments <= 0 || config->max_surgeries_pending <= 0 ||
      config->max_surgeries_pending > 64 || config->triage_simultaneous_patients <= 0 ||
      config->triage_critical_stability < 0 || config->triage_emergency_duration <= 0 ||
      config->triage_appointment_duration <= 0 || config->b01_min_duration <= 0 ||
      config->b01_max_duration < config->b01_min_duration || config->b02_min_duration <= 0 ||
      config->b02_max_duration < config->b02_min_duration || config->b03_min_duration <= 0 ||
      config->b03_max_duration < config->b03_min_duration || config->max_medical_teams <= 0 ||
      config->max_tests_lab1 <= 0 || config->max_tests_lab2 <= 0 || config->cleanup_min_time < 0 ||
      config->cleanup_max_time < config->cleanup_min_time || config->pharm_prep_time_min <= 0 ||
      config->pharm_prep_time_max < config->pharm_prep_time_min || config->lab1_min <= 0 ||
      config->lab1_max < config->lab1_min || config->lab2_min <= 0 ||
      config->lab2_max < config->lab2_min || config->auto_restock_enabled < 0 ||
      config->auto_restock_enabled > 1 || config->restock_qty_multiplier <= 0 || invalid_value ||
      med_count != MAX_MED_TYPES) {
    log_event(LOG_ERROR, "CONFIG", "VALIDATION_FAIL", "Configuracao incompleta ou invalida");
    return -1;
  }
  log_event(LOG_INFO, "CONFIG", "LOAD_SUCCESS", "Configuracao carregada com sucesso");
  return 0;
}
