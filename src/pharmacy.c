#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
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

static system_config_t config;
static global_statistics_t *g_stats = NULL;
static volatile sig_atomic_t keep_running = 1;
static int mq_urgent_id = -1, mq_normal_id = -1, mq_resp_id = -1;
static sem_t *sem_pharm = NULL;
static pthread_t t1, t2;

static void handle_shutdown(int s) {
  (void)s;
  keep_running = 0;
}

static void send_pharm_response(const char *target, const char *pid) {
  hospital_message_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_type = MSG_PHARMACY_READY;
  msg.msg_priority = 1;
  strcpy(msg.source, "PHARMACY");
  strcpy(msg.target, target);
  strcpy(msg.patient_id, pid);
  strcpy(msg.data, "Dispensed");
  size_t sz = sizeof(hospital_message_t) - sizeof(long);
  if (msgsnd(mq_resp_id, &msg, sz, 0) == -1) { /* ignore */
  } else
    printf("[PHARMACY] Resposta enviada para %s\n", pid);
}

void generate_receipt(const char *pid) {
  char path[256];
  snprintf(path, sizeof(path), "results/pharmacy_deliveries/RECIBO_%s_%ld.txt", pid, time(NULL));
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "RECIBO FARMACIA: %s\nENTREGUE\n", pid);
    fclose(f);
    printf("[PHARMACY] Recibo gerado para %s\n", pid);
  }
}

void process(hospital_message_t *msg) {
  wait_time_units(config.pharm_prep_time_min);
  generate_receipt(msg->patient_id);
  send_pharm_response(msg->source, msg->patient_id);
  if (g_stats) {
    pthread_mutex_lock(&g_stats->mutex);
    g_stats->total_pharmacy_requests++;
    pthread_mutex_unlock(&g_stats->mutex);
  }
}

void *thread_worker(void *arg) {
  int urgent = *(int *)arg;
  int q_id = urgent ? mq_urgent_id : mq_normal_id;
  hospital_message_t msg;
  size_t sz = sizeof(hospital_message_t) - sizeof(long);
  if (urgent)
    printf("[PHARMACY] Worker URGENTE iniciado.\n");

  while (keep_running) {
    if (msgrcv(q_id, &msg, sz, MSG_PHARMACY_REQUEST, 0) != -1) {
      printf("[PHARMACY] Pedido recebido (%s): %s\n", urgent ? "URG" : "NORM", msg.patient_id);
      if (sem_wait(sem_pharm) == -1) {
        if (errno == EINTR)
          continue;
      }
      if (!keep_running)
        break;
      process(&msg);
      sem_post(sem_pharm);
    } else {
      if (errno == EINTR)
        continue;
      break;
    }
  }
  return NULL;
}

int pharmacy_main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);

  if (load_config("config/config.txt", &config) != 0) {
    if (load_config("config.txt", &config) != 0)
      return -1;
  }

  key_t k_stats = ftok(IPC_CONFIG_FILE, KEY_SHM_STATS);
  int id_stats = shmget(k_stats, sizeof(global_statistics_t), 0666);
  if (id_stats != -1) {
    g_stats = shmat(id_stats, NULL, 0);
    if (g_stats == (void *)-1)
      g_stats = NULL;
  }

  mq_urgent_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT), 0666);
  mq_normal_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_NORMAL), 0666);
  mq_resp_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP), 0666);
  sem_pharm = sem_open(SEM_PHARM_NAME, 0);

  int u = 1, n = 0;
  pthread_create(&t1, NULL, thread_worker, &u);
  pthread_create(&t2, NULL, thread_worker, &n);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  if (g_stats)
    shmdt(g_stats);
  if (sem_pharm != SEM_FAILED)
    sem_close(sem_pharm);
  printf("[PHARMACY] Encerrado com sucesso.\n");
  return 0;
}
