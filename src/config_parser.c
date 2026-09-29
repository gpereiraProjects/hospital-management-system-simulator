#include "../include/config.h"
#include "../include/log.h"
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

int load_config(const char *filename, system_config_t *config) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    char err[256];
    snprintf(err, sizeof(err), "Nao foi possivel abrir %s", filename);
    log_event(LOG_ERROR, "CONFIG", "LOAD_FAIL", err);
    return -1;
  }

  char line[256];
  int med_count = 0;

  // Valores default caso falte algo
  config->time_unit_ms = 500;

  while (fgets(line, sizeof(line), f)) {
    trim(line);
    // Ignorar comentários e linhas vazias
    if (line[0] == '#' || line[0] == '\0')
      continue;

    char *key = strtok(line, "=");
    char *val = strtok(NULL, "=");

    if (!key || !val)
      continue;

    trim(key);
    trim(val);

    // --- Mapeamento Global ---
    if (strcmp(key, "TIME_UNIT_MS") == 0)
      config->time_unit_ms = atoi(val);
    else if (strcmp(key, "MAX_EMERGENCY_PATIENTS") == 0)
      config->max_emergency_patients = atoi(val);
    else if (strcmp(key, "MAX_APPOINTMENTS") == 0)
      config->max_appointments = atoi(val);
    else if (strcmp(key, "MAX_SURGERIES_PENDING") == 0)
      config->max_surgeries_pending = atoi(val);

    // --- Triagem ---
    else if (strcmp(key, "TRIAGE_SIMULTANEOUS_PATIENTS") == 0)
      config->triage_simultaneous_patients = atoi(val);
    else if (strcmp(key, "TRIAGE_CRITICAL_STABILITY") == 0)
      config->triage_critical_stability = atoi(val);
    else if (strcmp(key, "TRIAGE_EMERGENCY_DURATION") == 0)
      config->triage_emergency_duration = atoi(val);
    else if (strcmp(key, "TRIAGE_APPOINTMENT_DURATION") == 0)
      config->triage_appointment_duration = atoi(val);

    // --- Blocos Operatórios ---
    else if (strcmp(key, "B01_MIN_DURATION") == 0)
      config->b01_min_duration = atoi(val);
    else if (strcmp(key, "B01_MAX_DURATION") == 0)
      config->b01_max_duration = atoi(val);
    else if (strcmp(key, "B02_MIN_DURATION") == 0)
      config->b02_min_duration = atoi(val);
    else if (strcmp(key, "B02_MAX_DURATION") == 0)
      config->b02_max_duration = atoi(val);
    else if (strcmp(key, "B03_MIN_DURATION") == 0)
      config->b03_min_duration = atoi(val);
    else if (strcmp(key, "B03_MAX_DURATION") == 0)
      config->b03_max_duration = atoi(val);
    else if (strcmp(key, "MAX_MEDICAL_TEAMS") == 0)
      config->max_medical_teams = atoi(val);

    // --- Laboratórios ---
    else if (strcmp(key, "LAB1_TEST_MIN_DURATION") == 0)
      config->lab1_min = atoi(val);
    else if (strcmp(key, "LAB1_TEST_MAX_DURATION") == 0)
      config->lab1_max = atoi(val);
    else if (strcmp(key, "LAB2_TEST_MIN_DURATION") == 0)
      config->lab2_min = atoi(val);
    else if (strcmp(key, "LAB2_TEST_MAX_DURATION") == 0)
      config->lab2_max = atoi(val);
    else if (strcmp(key, "MAX_SIMULTANEOUS_TESTS_LAB1") == 0)
      config->max_tests_lab1 = atoi(val);
    else if (strcmp(key, "MAX_SIMULTANEOUS_TESTS_LAB2") == 0)
      config->max_tests_lab2 = atoi(val);

    // --- Farmácia ---
    else if (strcmp(key, "PHARMACY_PREPARATION_TIME_MIN") == 0)
      config->pharm_prep_time_min = atoi(val);
    else if (strcmp(key, "PHARMACY_PREPARATION_TIME_MAX") == 0)
      config->pharm_prep_time_max = atoi(val);

    // --- Medicamentos ---
    // Detecta chaves que não são standard e assume que são medicamentos
    // O parser verifica se o valor tem formato "int:int"
    else if (med_count < 15) {
      char *stock_str = strtok(val, ":");
      char *threshold_str = strtok(NULL, ":");

      if (stock_str && threshold_str) {
        strncpy(config->med_configs[med_count].name, key, 29);
        config->med_configs[med_count].initial_stock = atoi(stock_str);
        config->med_configs[med_count].threshold = atoi(threshold_str);
        med_count++;
      }
    }
  }

  fclose(f);
  log_event(LOG_INFO, "CONFIG", "LOAD_SUCCESS", "Configuracao carregada com sucesso");
  return 0;
}