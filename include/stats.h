#ifndef STATS_H
#define STATS_H

#include <pthread.h>
#include <time.h>

// --- SHM1: Estatísticas Globais (PDF Pag 16) ---
typedef struct {
  // Sincronização da estrutura
  pthread_mutex_t mutex;
  pthread_mutexattr_t
      mutex_attr; // Para configurar mutex robusto/process-shared

  // --- Triagem ---
  int total_emergency_patients;
  int total_appointments;
  double total_emergency_wait_time;
  double total_appointment_wait_time;
  int completed_emergencies;
  int completed_appointments;
  int critical_transfers;
  int rejected_patients;

  // --- Blocos Operatórios ---
  int total_surgeries_bo1;
  int total_surgeries_bo2;
  int total_surgeries_bo3;
  double total_surgery_wait_time;
  int completed_surgeries;
  int cancelled_surgeries;
  double bo1_utilization_time;
  double bo2_utilization_time;
  double bo3_utilization_time;

  // --- Farmácia ---
  int total_pharmacy_requests;
  int urgent_requests;
  int normal_requests;
  double total_pharmacy_response_time;
  int stock_depletions;
  int auto_restocks;

  // --- Laboratórios ---
  int total_lab_tests_lab1;
  int total_lab_tests_lab2;
  int total_preop_tests;
  double total_lab_turnaround_time;
  int urgent_lab_tests;

  // --- Globais do Sistema ---
  int total_operations;
  int system_errors;
  time_t system_start_time;
  int simulation_time_units; // Tempo lógico atual
} global_statistics_t;

#endif