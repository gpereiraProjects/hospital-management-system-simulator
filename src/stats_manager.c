#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../include/hospital.h"
#include "../include/log.h"
#include "../include/stats.h"

static global_statistics_t *ptr_stats = NULL;
static surgery_block_shm_t *ptr_bo = NULL;
static pharmacy_shm_t *ptr_pharm = NULL;
static lab_queue_shm_t *ptr_lab = NULL;

void stats_init_pointers(global_statistics_t *s, surgery_block_shm_t *b, pharmacy_shm_t *p,
                         lab_queue_shm_t *l) {
  ptr_stats = s;
  ptr_bo = b;
  ptr_pharm = p;
  ptr_lab = l;
}

void print_stats(FILE *out) {
  if (!ptr_stats) {
    fprintf(out, "Estatísticas não disponíveis (SHM não ligada).\n");
    return;
  }

  global_statistics_t s;
  // Mutex robusto para leitura consistente
  pthread_mutex_lock(&ptr_stats->mutex);
  s = *ptr_stats;
  pthread_mutex_unlock(&ptr_stats->mutex);

  fprintf(out, "ESTATÍSTICAS DO SISTEMA HOSPITALAR\n");
  fprintf(out, "==================================\n");
  fprintf(out, "Tempo Operação: %d unidades de tempo\n\n", s.simulation_time_units);

  fprintf(out, "CENTRO DE TRIAGEM\n");
  fprintf(out, "Total Emergências: %d\n", s.total_emergency_patients);
  fprintf(out, "Total Consultas: %d\n", s.total_appointments);
  double emergency_average =
      s.completed_emergencies > 0 ? s.total_emergency_wait_time / s.completed_emergencies : 0.0;
  double appointment_average =
      s.completed_appointments > 0 ? s.total_appointment_wait_time / s.completed_appointments : 0.0;
  fprintf(out, "Tempo Médio Espera (Emerg.): %.2f ut\n", emergency_average);
  fprintf(out, "Tempo Médio Espera (Consultas): %.2f ut\n", appointment_average);
  fprintf(out, "Pacientes Transferidos: %d\n", s.critical_transfers);
  fprintf(out, "Pacientes Rejeitados: %d\n\n", s.rejected_patients);

  fprintf(out, "BLOCOS OPERATÓRIOS\n");
  fprintf(out, "B01 (Cardiologia): %d cirurgias\n", s.total_surgeries_bo1);
  fprintf(out, "B02 (Ortopedia): %d cirurgias\n", s.total_surgeries_bo2);
  fprintf(out, "B03 (Neurologia): %d cirurgias\n", s.total_surgeries_bo3);
  fprintf(out, "Cirurgias Concluídas: %d\n\n", s.completed_surgeries);

  fprintf(out, "FARMÁCIA CENTRAL\n");
  fprintf(out, "Total Pedidos: %d\n", s.total_pharmacy_requests);
  fprintf(out, "Reposições Stock: %d\n\n", s.auto_restocks);

  fprintf(out, "LABORATÓRIOS\n");
  fprintf(out, "Total Análises: %d (Lab1) + %d (Lab2)\n", s.total_lab_tests_lab1,
          s.total_lab_tests_lab2);
  fprintf(out, "Testes PREOP: %d\n\n", s.total_preop_tests);

  fprintf(out, "GLOBAIS\n");
  fprintf(out, "Erros Sistema: %d\n", s.system_errors);
  fprintf(out, "==================================\n");
}

static void print_triage_status(FILE *out) {
  global_statistics_t snapshot;
  pthread_mutex_lock(&ptr_stats->mutex);
  snapshot = *ptr_stats;
  pthread_mutex_unlock(&ptr_stats->mutex);
  fprintf(out, "TRIAGE emergencies=%d appointments=%d completed=%d rejected=%d transferred=%d\n",
          snapshot.total_emergency_patients, snapshot.total_appointments,
          snapshot.completed_emergencies + snapshot.completed_appointments,
          snapshot.rejected_patients, snapshot.critical_transfers);
}

static void print_surgery_status(FILE *out) {
  for (int i = 0; i < MAX_ROOMS; i++) {
    pthread_mutex_lock(&ptr_bo->rooms[i].mutex);
    fprintf(out, "SURGERY room=%d status=%d patient=%s\n", ptr_bo->rooms[i].room_id,
            ptr_bo->rooms[i].status,
            ptr_bo->rooms[i].current_patient[0] ? ptr_bo->rooms[i].current_patient : "-");
    pthread_mutex_unlock(&ptr_bo->rooms[i].mutex);
  }
}

static void print_pharmacy_status(FILE *out) {
  for (int i = 0; i < MAX_MED_TYPES; i++) {
    medication_stock_t *medication = &ptr_pharm->medications[i];
    pthread_mutex_lock(&medication->mutex);
    fprintf(out, "PHARMACY medication=%s stock=%d reserved=%d threshold=%d\n", medication->name,
            medication->current_stock, medication->reserved, medication->threshold);
    pthread_mutex_unlock(&medication->mutex);
  }
}

static void print_lab_status(FILE *out) {
  pthread_mutex_lock(&ptr_lab->lab1_mutex);
  fprintf(out, "LAB lab=LAB1 queued=%d available=%d\n", ptr_lab->lab1_count,
          ptr_lab->lab1_available_slots);
  pthread_mutex_unlock(&ptr_lab->lab1_mutex);
  pthread_mutex_lock(&ptr_lab->lab2_mutex);
  fprintf(out, "LAB lab=LAB2 queued=%d available=%d\n", ptr_lab->lab2_count,
          ptr_lab->lab2_available_slots);
  pthread_mutex_unlock(&ptr_lab->lab2_mutex);
}

void print_component_status(FILE *out, const char *component) {
  if (ptr_stats == NULL || ptr_bo == NULL || ptr_pharm == NULL || ptr_lab == NULL) {
    fprintf(out, "Estado indisponivel.\n");
    return;
  }
  if (strcmp(component, "ALL") == 0 || strcmp(component, "TRIAGE") == 0)
    print_triage_status(out);
  if (strcmp(component, "ALL") == 0 || strcmp(component, "SURGERY") == 0)
    print_surgery_status(out);
  if (strcmp(component, "ALL") == 0 || strcmp(component, "PHARMACY") == 0)
    print_pharmacy_status(out);
  if (strcmp(component, "ALL") == 0 || strcmp(component, "LAB") == 0)
    print_lab_status(out);
  fflush(out);
}

int save_stats_snapshot(char *path, size_t path_size) {
  char filename[128];
  time_t now = time(NULL);

  snprintf(filename, sizeof(filename), "results/stats_snapshots/stats_%ld.txt", now);

  FILE *f = fopen(filename, "w");
  if (f == NULL)
    return -1;
  print_stats(f);
  if (fclose(f) != 0)
    return -1;
  if (path != NULL && path_size > 0)
    snprintf(path, path_size, "%s", filename);
  return 0;
}
