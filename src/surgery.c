#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/sem.h>
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
  int operation_id;
  char patient_id[MAX_PATIENT_ID];
  char type[10];
  int room_index;
  int lab_completed;
  int meds_completed;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  int active;
} active_surgery_t;

#define MAX_CONCURRENT_SURGERIES 10
static active_surgery_t surgery_list[MAX_CONCURRENT_SURGERIES];
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static system_config_t config;
static global_statistics_t *g_stats = NULL;
static surgery_block_shm_t *g_shm_bo = NULL;
static volatile sig_atomic_t keep_running = 1;

static int mq_urgent_id = -1;
static int mq_resp_id = -1;
static sem_t *sem_bo1 = NULL, *sem_bo2 = NULL, *sem_bo3 = NULL, *sem_teams = NULL;
static pthread_t t_manager, t_resp;

static void handle_shutdown(int s) {
  (void)s;
  keep_running = 0;
}

int get_surgery_duration(const char *type) {
  if (strcmp(type, "CARDIO") == 0)
    return config.b01_min_duration +
           rand() % (config.b01_max_duration - config.b01_min_duration + 1);
  if (strcmp(type, "ORTHO") == 0)
    return config.b02_min_duration +
           rand() % (config.b02_max_duration - config.b02_min_duration + 1);
  if (strcmp(type, "NEURO") == 0)
    return config.b03_min_duration +
           rand() % (config.b03_max_duration - config.b03_min_duration + 1);
  return 50;
}
int get_cleanup_duration(void) {
  return config.cleanup_min_time + rand() % (config.cleanup_max_time - config.cleanup_min_time + 1);
}
void send_request(int mq_id, int type, const char *target, const char *pid, const char *data) {
  hospital_message_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_priority = type;
  msg.msg_type = type;
  strcpy(msg.source, "SURGERY");
  strcpy(msg.target, target);
  strcpy(msg.patient_id, pid);
  msg.timestamp = time(NULL);
  if (data)
    strcpy(msg.data, data);
  size_t msg_sz = sizeof(hospital_message_t) - sizeof(long);
  if (msgsnd(mq_id, &msg, msg_sz, 0) == -1) { /* ignore */
  } else
    printf("[SURGERY] Pedido enviado para %s (%s)\n", target, pid);
}

void *thread_response_monitor(void *arg) {
  (void)arg;
  hospital_message_t msg;
  size_t msg_sz = sizeof(hospital_message_t) - sizeof(long);
  while (keep_running) {
    if (msgrcv(mq_resp_id, &msg, msg_sz, 0, 0) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (strcmp(msg.target, "SURGERY") != 0)
      continue;
    mutex_lock(&list_mutex, "RespMonitor");
    for (int i = 0; i < MAX_CONCURRENT_SURGERIES; i++) {
      if (surgery_list[i].active && strcmp(surgery_list[i].patient_id, msg.patient_id) == 0) {
        mutex_lock(&surgery_list[i].mutex, "SurgeryUpdate");
        if (msg.msg_type == MSG_LAB_RESULTS_READY)
          surgery_list[i].lab_completed = 1;
        else if (msg.msg_type == MSG_PHARMACY_READY)
          surgery_list[i].meds_completed = 1;
        cond_signal(&surgery_list[i].cond, "RespSignal");
        mutex_unlock(&surgery_list[i].mutex, "SurgeryUpdate");
        break;
      }
    }
    mutex_unlock(&list_mutex, "RespMonitor");
  }
  return NULL;
}

void *thread_surgery_execution(void *arg) {
  int idx = *(int *)arg;
  free(arg);
  active_surgery_t *s = &surgery_list[idx];

  printf("[SURGERY] A iniciar proc. cirurgia para %s\n", s->patient_id);
  send_request(mq_urgent_id, MSG_LAB_REQUEST, "LAB", s->patient_id,
               "priority:URGENT tests:[PREOP]");
  send_request(mq_urgent_id, MSG_PHARMACY_REQUEST, "PHARMACY", s->patient_id,
               "priority:URGENT items:[ANESTESICO_C:1]");

  mutex_lock(&s->mutex, "SurgeryWait");
  while ((!s->lab_completed || !s->meds_completed) && keep_running) {
    pthread_cond_wait(&s->cond, &s->mutex);
  }
  mutex_unlock(&s->mutex, "SurgeryWait");

  if (!keep_running)
    return NULL;

  sem_t *my_room_sem = NULL;
  int room_shm_index = -1;
  if (strcmp(s->type, "CARDIO") == 0) {
    my_room_sem = sem_bo1;
    room_shm_index = 0;
  } else if (strcmp(s->type, "ORTHO") == 0) {
    my_room_sem = sem_bo2;
    room_shm_index = 1;
  } else {
    my_room_sem = sem_bo3;
    room_shm_index = 2;
  }

  sem_wait(my_room_sem);
  sem_wait(sem_teams);

  mutex_lock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomLock");
  g_shm_bo->rooms[room_shm_index].status = 1;
  strcpy(g_shm_bo->rooms[room_shm_index].current_patient, s->patient_id);
  mutex_unlock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomLock");

  wait_time_units(get_surgery_duration(s->type));
  sem_post(sem_teams);

  mutex_lock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomClean");
  g_shm_bo->rooms[room_shm_index].status = 2;
  mutex_unlock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomClean");
  wait_time_units(get_cleanup_duration());

  mutex_lock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomFree");
  g_shm_bo->rooms[room_shm_index].status = 0;
  g_shm_bo->rooms[room_shm_index].current_patient[0] = '\0';
  mutex_unlock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomFree");
  sem_post(my_room_sem);

  if (g_stats) {
    pthread_mutex_lock(&g_stats->mutex);
    if (room_shm_index == 0)
      g_stats->total_surgeries_bo1++;
    else if (room_shm_index == 1)
      g_stats->total_surgeries_bo2++;
    else
      g_stats->total_surgeries_bo3++;
    g_stats->completed_surgeries++;
    pthread_mutex_unlock(&g_stats->mutex);
  }
  printf("[SURGERY] Concluida %s\n", s->patient_id);
  mutex_lock(&list_mutex, "SurgeryEnd");
  s->active = 0;
  mutex_unlock(&list_mutex, "SurgeryEnd");
  return NULL;
}

void *thread_surgery_manager(void *arg) {
  (void)arg;
  hospital_message_t msg;
  size_t msg_sz = sizeof(hospital_message_t) - sizeof(long);
  printf("[SURGERY] Manager pronto.\n");
  while (keep_running) {
    if (msgrcv(mq_urgent_id, &msg, msg_sz, MSG_NEW_SURGERY, 0) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    printf("[SURGERY] Comando recebido: %s\n", msg.patient_id);
    mutex_lock(&list_mutex, "NewSurgery");
    int slot = -1;
    for (int i = 0; i < MAX_CONCURRENT_SURGERIES; i++) {
      if (!surgery_list[i].active) {
        slot = i;
        break;
      }
    }
    if (slot != -1) {
      active_surgery_t *s = &surgery_list[slot];
      s->active = 1;
      strcpy(s->patient_id, msg.patient_id);
      s->lab_completed = 0;
      s->meds_completed = 0;
      if (strstr(msg.data, "CARDIO"))
        strcpy(s->type, "CARDIO");
      else if (strstr(msg.data, "ORTHO"))
        strcpy(s->type, "ORTHO");
      else
        strcpy(s->type, "NEURO");

      pthread_t t;
      int *arg_idx = malloc(sizeof(int));
      *arg_idx = slot;
      pthread_create(&t, NULL, thread_surgery_execution, arg_idx);
      pthread_detach(t);
    }
    mutex_unlock(&list_mutex, "NewSurgery");
  }
  return NULL;
}

int surgery_main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);

  if (load_config("config/config.txt", &config) != 0) {
    if (load_config("config.txt", &config) != 0)
      return -1;
  }

  key_t k_stats = ftok(IPC_CONFIG_FILE, KEY_SHM_STATS);
  key_t k_bo = ftok(IPC_CONFIG_FILE, KEY_SHM_BO);
  int id_stats = shmget(k_stats, sizeof(global_statistics_t), 0666);
  int id_bo = shmget(k_bo, sizeof(surgery_block_shm_t), 0666);

  if (id_stats != -1) {
    g_stats = shmat(id_stats, NULL, 0);
    if (g_stats == (void *)-1)
      g_stats = NULL;
  }
  if (id_bo != -1) {
    g_shm_bo = shmat(id_bo, NULL, 0);
    if (g_shm_bo == (void *)-1)
      g_shm_bo = NULL;
  }

  mq_urgent_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT), 0666);
  mq_resp_id = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP), 0666);

  sem_bo1 = sem_open(SEM_BO1_NAME, 0);
  sem_bo2 = sem_open(SEM_BO2_NAME, 0);
  sem_bo3 = sem_open(SEM_BO3_NAME, 0);
  sem_teams = sem_open(SEM_TEAMS_NAME, 0);

  for (int i = 0; i < MAX_CONCURRENT_SURGERIES; i++) {
    surgery_list[i].active = 0;
    pthread_mutex_init(&surgery_list[i].mutex, NULL);
    pthread_cond_init(&surgery_list[i].cond, NULL);
  }

  pthread_create(&t_manager, NULL, thread_surgery_manager, NULL);
  pthread_create(&t_resp, NULL, thread_response_monitor, NULL);

  pthread_join(t_manager, NULL);
  pthread_join(t_resp, NULL);

  if (g_stats)
    shmdt(g_stats);
  if (g_shm_bo)
    shmdt(g_shm_bo);
  if (sem_bo1 != SEM_FAILED)
    sem_close(sem_bo1);
  if (sem_bo2 != SEM_FAILED)
    sem_close(sem_bo2);
  if (sem_bo3 != SEM_FAILED)
    sem_close(sem_bo3);
  if (sem_teams != SEM_FAILED)
    sem_close(sem_teams);
  printf("[SURGERY] Encerrado com sucesso.\n");
  return 0;
}
