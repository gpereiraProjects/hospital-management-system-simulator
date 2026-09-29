#include <pthread.h>
#include <signal.h>
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

void stats_init_pointers(global_statistics_t *s, surgery_block_shm_t *b,
                         pharmacy_shm_t *p, lab_queue_shm_t *l) {
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
  fprintf(out, "Tempo Operação: %d unidades de tempo\n\n",
          s.simulation_time_units);

  fprintf(out, "CENTRO DE TRIAGEM\n");
  fprintf(out, "Total Emergências: %d\n", s.total_emergency_patients);
  fprintf(out, "Total Consultas: %d\n", s.total_appointments);
  fprintf(out, "Tempo Médio Espera (Emerg.): %.2f ut\n",
          s.total_emergency_wait_time);
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
  fprintf(out, "Total Análises: %d (Lab1) + %d (Lab2)\n",
          s.total_lab_tests_lab1, s.total_lab_tests_lab2);
  fprintf(out, "Testes PREOP: %d\n\n", s.total_preop_tests);

  fprintf(out, "GLOBAIS\n");
  fprintf(out, "Erros Sistema: %d\n", s.system_errors);
  fprintf(out, "==================================\n");
}

void sigusr1_handler(int signum) {
  (void)signum;
  print_stats(stdout);
}

void sigusr2_handler(int signum) {
  (void)signum;
  char filename[128];
  time_t now = time(NULL);

  snprintf(filename, sizeof(filename), "results/stats_snapshots/stats_%ld.txt",
           now);

  FILE *f = fopen(filename, "w");
  if (f) {
    print_stats(f);
    fclose(f);

    const char *msg = "[SIGUSR2] Snapshot gerado.\n";
    if (write(STDOUT_FILENO, msg, strlen(msg)) == -1) {
      // Ignorar erro de escrita
    }
  }
}