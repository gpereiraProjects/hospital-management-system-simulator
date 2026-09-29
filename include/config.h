#ifndef CONFIG_H
#define CONFIG_H

#include "hospital.h" // Para usar a struct medication_stock_t se necessário

typedef struct {
  // --- Globais ---
  int time_unit_ms;           // TIME_UNIT_MS (ex: 500)
  int max_emergency_patients; // MAX_EMERGENCY_PATIENTS
  int max_appointments;       // MAX_APPOINTMENTS
  int max_surgeries_pending;  // MAX_SURGERIES_PENDING

  // --- Centro de Triagem ---
  int triage_simultaneous_patients; // Threads de atendimento
  int triage_critical_stability;    // Limiar estabilidade
  int triage_emergency_duration;    // Tempo de triagem
  int triage_appointment_duration;  // Tempo consulta

  // --- Blocos Operatórios ---
  int b01_min_duration, b01_max_duration;
  int b02_min_duration, b02_max_duration;
  int b03_min_duration, b03_max_duration;
  int cleanup_min_time, cleanup_max_time;
  int max_medical_teams;

  // --- Farmácia ---
  int pharm_prep_time_min, pharm_prep_time_max;
  int auto_restock_enabled;
  int restock_qty_multiplier;

  // --- Laboratórios ---
  int lab1_min, lab1_max;
  int lab2_min, lab2_max;
  int max_tests_lab1;
  int max_tests_lab2;

  // --- Stocks Iniciais (Array de configurações) ---
  // Estrutura auxiliar para guardar o config lido antes de meter na SHM
  struct {
    char name[30];
    int initial_stock;
    int threshold;
  } med_configs[15];

} system_config_t;

// Função para carregar o ficheiro (implementada em src/config_parser.c)
int load_config(const char *filename, system_config_t *config);

#endif