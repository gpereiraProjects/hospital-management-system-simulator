#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/hospital.h"
#include "../include/ipc.h"
#include "../include/log.h"
#include "../include/stats.h"
#include "../include/sync.h"

typedef struct {
  char patient_id[15];
  char test_type[10];
} test_req_t;

static system_config_t config;
static global_statistics_t *g_stats = NULL;
static volatile sig_atomic_t keep_running = 1;
static int mq_urgent_id = -1, mq_resp_id = -1;
static sem_t *sem_lab1 = NULL, *sem_lab2 = NULL;
static pthread_t t; // Global para cancel

static void handle_shutdown(int s) {
  (void)s;
  keep_running = 0;
  pthread_cancel(t);
}

void generate_result(const char *pid, const char *type) {
  char path[256];
  snprintf(path, sizeof(path), "results/lab_results/RESULT_%s_%ld.txt", pid,
           time(NULL));
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "RELATORIO LAB: %s - %s\nVALIDADO\n", pid, type);
    fclose(f);
    printf("[LAB] Gerado ficheiro para %s\n", pid);
  }
}

static void send_lab_response(const char *pid) {
  hospital_message_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_type = MSG_LAB_RESULTS_READY;
  msg.msg_priority = 1;
  strcpy(msg.source, "LAB");
  strcpy(msg.target, "SURGERY");
  strcpy(msg.patient_id, pid);
  strcpy(msg.data, "Done");
  size_t sz = sizeof(hospital_message_t) - sizeof(long);
  if (msgsnd(mq_resp_id, &msg, sz, 0) == -1) {
  } else
    printf("[LAB] Resposta enviada para %s\n", pid);
}

void *thread_test(void *arg) {
  test_req_t *req = (test_req_t *)arg;

  if (strcmp(req->test_type, "PREOP") == 0) {
    sem_wait(sem_lab1);
    wait_time_units(config.lab1_min);
    sem_post(sem_lab1);
    sem_wait(sem_lab2);
    wait_time_units(config.lab2_min);
    sem_post(sem_lab2);
    if (g_stats) {
      pthread_mutex_lock(&g_stats->mutex);
      g_stats->total_preop_tests++;
      pthread_mutex_unlock(&g_stats->mutex);
    }
  } else {
    sem_t *sem = (strcmp(req->test_type, "HEMO") == 0) ? sem_lab1 : sem_lab2;
    sem_wait(sem);
    wait_time_units(config.lab1_min);
    sem_post(sem);
  }

  generate_result(req->patient_id, req->test_type);
  send_lab_response(req->patient_id);
  free(req);
  return NULL;
}

void *thread_manager(void *arg) {
  (void)arg;
  hospital_message_t msg;
  size_t sz = sizeof(hospital_message_t) - sizeof(long);
  printf("[LAB] Thread Manager iniciada.\n");
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  while (keep_running) {
    if (msgrcv(mq_urgent_id, &msg, sz, MSG_LAB_REQUEST, 0) != -1) {
      printf("[LAB] Pedido recebido: %s (%s)\n", msg.patient_id, msg.data);
      test_req_t *req = malloc(sizeof(test_req_t));
      snprintf(req->patient_id, sizeof(req->patient_id), "%s", msg.patient_id);

      if (strstr(msg.data, "PREOP"))
        strcpy(req->test_type, "PREOP");
      else if (strstr(msg.data, "HEMO"))
        strcpy(req->test_type, "HEMO");
      else
        strcpy(req->test_type, "GENERIC");

      pthread_t t;
      pthread_create(&t, NULL, thread_test, req);
      pthread_detach(t);
    } else {
      if (errno == EINTR)
        continue;
      break;
    }
  }
  return NULL;
}

int laboratory_main(int argc, char *argv[]) {
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
  if (id_stats != -1)
    g_stats = shmat(id_stats, NULL, 0);

  mq_urgent_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT), 0666);
  mq_resp_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP), 0666);

  sem_lab1 = sem_open(SEM_LAB1_NAME, 0);
  sem_lab2 = sem_open(SEM_LAB2_NAME, 0);

  pthread_create(&t, NULL, thread_manager, NULL);
  pthread_join(t, NULL);

  if (g_stats)
    shmdt(g_stats);
  if (sem_lab1 != SEM_FAILED)
    sem_close(sem_lab1);
  if (sem_lab2 != SEM_FAILED)
    sem_close(sem_lab2);
  printf("[LAB] Encerrado com sucesso.\n");
  return 0;
}