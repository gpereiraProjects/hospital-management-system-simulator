#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/hospital.h"
#include "../include/ipc.h"
#include "../include/log.h"
#include "../include/stats.h"
#include "../include/sync.h"

typedef struct {
  char id[MAX_PATIENT_ID];
  int triage_level;
  int is_active;
  time_t arrival;
  int type;
} patient_t;

static patient_t *patients;
static int max_patients;
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static system_config_t config;
static global_statistics_t *g_stats = NULL;

static volatile sig_atomic_t keep_running = 1;
static int mq_normal_id = -1;
static pthread_t t_man;

static void handle_shutdown(int s) {
  (void)s;
  keep_running = 0;
}

int get_next(void) {
  int best = -1;
  for (int i = 0; i < max_patients; i++) {
    if (patients[i].is_active == 1) {
      if (best == -1) {
        best = i;
        continue;
      }
      int prio_i = (patients[i].type == 1) ? patients[i].triage_level : 10;
      int prio_best = (patients[best].type == 1) ? patients[best].triage_level : 10;
      if (prio_i < prio_best)
        best = i;
      else if (prio_i == prio_best && patients[i].arrival < patients[best].arrival)
        best = i;
    }
  }
  return best;
}

void *doctor(void *arg) {
  int id = *(int *)arg;
  free(arg);
  printf("[TRIAGE] Medico %d iniciado.\n", id);

  while (keep_running) {
    mutex_lock(&list_mutex, "DocPick");
    int idx = get_next();
    if (idx != -1) {
      patients[idx].is_active = 2;
      printf("[TRIAGE] Medico %d atende %s\n", id, patients[idx].id);
      mutex_unlock(&list_mutex, "DocPick");

      int duration = (patients[idx].type == 1) ? config.triage_emergency_duration
                                               : config.triage_appointment_duration;
      for (int k = 0; k < duration && keep_running; k++)
        usleep(1000 * config.time_unit_ms);

      mutex_lock(&list_mutex, "DocEnd");
      patients[idx].is_active = 0;
      if (g_stats) {
        pthread_mutex_lock(&g_stats->mutex);
        if (patients[idx].type == 1)
          g_stats->completed_emergencies++;
        else
          g_stats->completed_appointments++;
        pthread_mutex_unlock(&g_stats->mutex);
      }
      mutex_unlock(&list_mutex, "DocEnd");
    } else {
      mutex_unlock(&list_mutex, "DocPick");
      usleep(100000);
    }
  }
  return NULL;
}

void *manager(void *arg) {
  (void)arg;
  hospital_message_t msg;
  size_t sz = sizeof(hospital_message_t) - sizeof(long);
  printf("[TRIAGE] Manager pronto.\n");

  while (keep_running) {
    if (msgrcv(mq_normal_id, &msg, sz, 0, 0) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (msg.msg_type != MSG_NEW_EMERGENCY && msg.msg_type != MSG_NEW_APPOINTMENT)
      continue;

    mutex_lock(&list_mutex, "Add");
    int slot = -1;
    for (int i = 0; i < max_patients; i++) {
      if (patients[i].is_active == 0) {
        slot = i;
        break;
      }
    }

    if (slot != -1) {
      snprintf(patients[slot].id, sizeof(patients[slot].id), "%s", msg.patient_id);
      if (msg.msg_type == MSG_NEW_EMERGENCY) {
        patients[slot].type = 1;
        patients[slot].triage_level = msg.operation_id;
        if (g_stats) {
          pthread_mutex_lock(&g_stats->mutex);
          g_stats->total_emergency_patients++;
          pthread_mutex_unlock(&g_stats->mutex);
        }
      } else {
        patients[slot].type = 2;
        patients[slot].triage_level = 0;
        if (g_stats) {
          pthread_mutex_lock(&g_stats->mutex);
          g_stats->total_appointments++;
          pthread_mutex_unlock(&g_stats->mutex);
        }
      }
      patients[slot].arrival = time(NULL);
      patients[slot].is_active = 1;
      log_event(LOG_INFO, "TRIAGE", "ADMIT", msg.patient_id);
    } else {
      log_event(LOG_WARNING, "TRIAGE", "REJECT", "Lotado");
      if (g_stats) {
        pthread_mutex_lock(&g_stats->mutex);
        g_stats->rejected_patients++;
        pthread_mutex_unlock(&g_stats->mutex);
      }
    }
    mutex_unlock(&list_mutex, "Add");
  }
  return NULL;
}

int triage_main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);

  if (load_config("config/config.txt", &config) != 0) {
    if (load_config("config.txt", &config) != 0)
      return -1;
  }

  max_patients = config.max_emergency_patients + config.max_appointments;
  if (max_patients <= 0)
    max_patients = 150;
  patients = calloc(max_patients, sizeof(patient_t));

  key_t k_stats = ftok(IPC_CONFIG_FILE, KEY_SHM_STATS);
  int id_stats = shmget(k_stats, sizeof(global_statistics_t), 0666);
  if (id_stats != -1) {
    g_stats = shmat(id_stats, NULL, 0);
    if (g_stats == (void *)-1)
      g_stats = NULL;
  }

  mq_normal_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_NORMAL), 0666);

  pthread_create(&t_man, NULL, manager, NULL);

  int docs = config.triage_simultaneous_patients;
  if (docs <= 0)
    docs = 3;
  pthread_t *t_docs = malloc(sizeof(pthread_t) * docs);
  for (int i = 0; i < docs; i++) {
    int *id = malloc(sizeof(int));
    *id = i + 1;
    pthread_create(&t_docs[i], NULL, doctor, id);
  }

  // Esperar pelo manager (se for cancelado, o join retorna logo)
  pthread_join(t_man, NULL);

  // Garantir que médicos saem
  keep_running = 0;
  for (int i = 0; i < docs; i++)
    pthread_join(t_docs[i], NULL);

  if (g_stats)
    shmdt(g_stats);
  free(t_docs);
  free(patients);
  printf("[TRIAGE] Encerrado com sucesso.\n");
  return 0;
}
