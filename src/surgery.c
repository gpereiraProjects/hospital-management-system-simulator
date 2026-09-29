#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
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
  char type[MAX_COMPONENT_NAME];
  int room_index;
  int lab_completed;
  int meds_completed;
  command_t command;
  pthread_t thread;
  int thread_started;
  int ready;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  int active;
} active_surgery_t;

#define MAX_CONCURRENT_SURGERIES 64
static active_surgery_t surgery_list[MAX_CONCURRENT_SURGERIES];
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_condition = PTHREAD_COND_INITIALIZER;
static int occupied_rooms[MAX_ROOMS];
static system_config_t config;
static global_statistics_t *g_stats = NULL;
static surgery_block_shm_t *g_shm_bo = NULL;
static atomic_bool keep_running = ATOMIC_VAR_INIT(true);

static int mq_urgent_id = -1;
static int mq_resp_id = -1;
static sem_t *sem_bo1 = NULL, *sem_bo2 = NULL, *sem_bo3 = NULL, *sem_teams = NULL;
static pthread_t t_manager, t_resp;

static int wait_for_semaphore(sem_t *semaphore) {
  while (sem_wait(semaphore) == -1) {
    if (errno != EINTR)
      return -1;
  }
  return 0;
}

static int room_for_type(const char *type) {
  if (strcmp(type, "CARDIO") == 0)
    return 0;
  if (strcmp(type, "ORTHO") == 0)
    return 1;
  return 2;
}

static int urgency_rank(const char *urgency) {
  if (strcmp(urgency, "HIGH") == 0)
    return 3;
  if (strcmp(urgency, "MEDIUM") == 0)
    return 2;
  return 1;
}

static int has_better_ready_surgery(int index) {
  active_surgery_t *candidate = &surgery_list[index];
  for (int i = 0; i < MAX_CONCURRENT_SURGERIES; i++) {
    active_surgery_t *other = &surgery_list[i];
    if (i == index || !other->active || !other->ready ||
        room_for_type(other->type) != candidate->room_index)
      continue;
    int other_urgency = urgency_rank(other->command.urgency);
    int candidate_urgency = urgency_rank(candidate->command.urgency);
    if (other_urgency > candidate_urgency ||
        (other_urgency == candidate_urgency &&
         other->command.scheduled_time < candidate->command.scheduled_time) ||
        (other_urgency == candidate_urgency &&
         other->command.scheduled_time == candidate->command.scheduled_time && i < index))
      return 1;
  }
  return 0;
}

static int claim_surgery_room(int index) {
  mutex_lock(&list_mutex, "SurgerySchedule");
  active_surgery_t *surgery = &surgery_list[index];
  while (keep_running && (occupied_rooms[surgery->room_index] || has_better_ready_surgery(index)))
    pthread_cond_wait(&schedule_condition, &list_mutex);
  if (!keep_running) {
    surgery->ready = 0;
    mutex_unlock(&list_mutex, "SurgerySchedule");
    return -1;
  }
  occupied_rooms[surgery->room_index] = 1;
  surgery->ready = 0;
  mutex_unlock(&list_mutex, "SurgerySchedule");
  return 0;
}

static void release_surgery_room(int room_index) {
  mutex_lock(&list_mutex, "SurgeryRelease");
  occupied_rooms[room_index] = 0;
  pthread_cond_broadcast(&schedule_condition);
  mutex_unlock(&list_mutex, "SurgeryRelease");
}

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
void send_request(int mq_id, int type, const char *target, const char *pid,
                  const command_t *command) {
  hospital_message_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_priority = type;
  msg.msg_type = type;
  strcpy(msg.source, "SURGERY");
  strcpy(msg.target, target);
  strcpy(msg.patient_id, pid);
  msg.timestamp = time(NULL);
  msg.command = *command;
  if (type == MSG_PHARMACY_REQUEST)
    msg.msg_priority = PHARMACY_URGENT;
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
    if (msgrcv(mq_resp_id, &msg, msg_sz, RESPONSE_SURGERY, 0) == -1) {
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
  command_t lab_command = s->command;
  lab_command.kind = COMMAND_LAB_REQUEST;
  lab_command.priority = COMMAND_PRIORITY_URGENT;
  snprintf(lab_command.lab, sizeof(lab_command.lab), "BOTH");
  command_t pharmacy_command = s->command;
  pharmacy_command.kind = COMMAND_PHARMACY_REQUEST;
  pharmacy_command.priority = COMMAND_PRIORITY_URGENT;
  send_request(mq_urgent_id, MSG_LAB_REQUEST, "LAB", s->patient_id, &lab_command);
  send_request(mq_urgent_id, MSG_PHARMACY_REQUEST, "PHARMACY", s->patient_id, &pharmacy_command);

  mutex_lock(&s->mutex, "SurgeryWait");
  while ((!s->lab_completed || !s->meds_completed) && keep_running) {
    pthread_cond_wait(&s->cond, &s->mutex);
  }
  mutex_unlock(&s->mutex, "SurgeryWait");

  if (!keep_running)
    goto finish;

  mutex_lock(&list_mutex, "SurgeryReady");
  s->ready = 1;
  pthread_cond_broadcast(&schedule_condition);
  mutex_unlock(&list_mutex, "SurgeryReady");

  while (keep_running) {
    int simulation_time;
    pthread_mutex_lock(&g_stats->mutex);
    simulation_time = g_stats->simulation_time_units;
    pthread_mutex_unlock(&g_stats->mutex);
    if (simulation_time >= s->command.scheduled_time)
      break;
    wait_time_units(1);
  }
  if (!keep_running)
    goto finish;

  if (claim_surgery_room(idx) != 0)
    goto finish;

  sem_t *my_room_sem = NULL;
  int room_shm_index = s->room_index;
  if (strcmp(s->type, "CARDIO") == 0) {
    my_room_sem = sem_bo1;
  } else if (strcmp(s->type, "ORTHO") == 0) {
    my_room_sem = sem_bo2;
  } else {
    my_room_sem = sem_bo3;
  }

  if (wait_for_semaphore(my_room_sem) != 0) {
    release_surgery_room(room_shm_index);
    goto finish;
  }
  if (wait_for_semaphore(sem_teams) != 0) {
    sem_post(my_room_sem);
    release_surgery_room(room_shm_index);
    goto finish;
  }

  mutex_lock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomLock");
  g_shm_bo->rooms[room_shm_index].status = 1;
  strcpy(g_shm_bo->rooms[room_shm_index].current_patient, s->patient_id);
  mutex_unlock(&g_shm_bo->rooms[room_shm_index].mutex, "RoomLock");
  log_event(LOG_INFO, "SURGERY", "START", s->patient_id);

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
  release_surgery_room(room_shm_index);

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
  log_event(LOG_INFO, "SURGERY", "COMPLETE", s->patient_id);
finish:
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
    int slot_limit = config.max_surgeries_pending;
    if (slot_limit <= 0 || slot_limit > MAX_CONCURRENT_SURGERIES)
      slot_limit = MAX_CONCURRENT_SURGERIES;
    for (int i = 0; i < slot_limit; i++) {
      if (!surgery_list[i].active) {
        if (surgery_list[i].thread_started) {
          pthread_join(surgery_list[i].thread, NULL);
          surgery_list[i].thread_started = 0;
        }
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
      s->command = msg.command;
      snprintf(s->type, sizeof(s->type), "%s", msg.command.surgery_type);
      s->room_index = room_for_type(s->type);
      s->ready = 0;

      int *arg_idx = malloc(sizeof(int));
      if (arg_idx == NULL) {
        s->active = 0;
        mutex_unlock(&list_mutex, "NewSurgery");
        continue;
      }
      *arg_idx = slot;
      if (pthread_create(&s->thread, NULL, thread_surgery_execution, arg_idx) == 0)
        s->thread_started = 1;
      else {
        free(arg_idx);
        s->active = 0;
      }
    } else {
      log_event(LOG_WARNING, "SURGERY", "REJECT", msg.patient_id);
      pthread_mutex_lock(&g_stats->mutex);
      g_stats->cancelled_surgeries++;
      pthread_mutex_unlock(&g_stats->mutex);
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
  mutex_lock(&list_mutex, "ScheduleShutdown");
  pthread_cond_broadcast(&schedule_condition);
  mutex_unlock(&list_mutex, "ScheduleShutdown");

  pthread_create(&t_manager, NULL, thread_surgery_manager, NULL);
  pthread_create(&t_resp, NULL, thread_response_monitor, NULL);

  pthread_join(t_manager, NULL);
  pthread_join(t_resp, NULL);

  for (int i = 0; i < MAX_CONCURRENT_SURGERIES; i++) {
    mutex_lock(&surgery_list[i].mutex, "SurgeryShutdown");
    pthread_cond_broadcast(&surgery_list[i].cond);
    mutex_unlock(&surgery_list[i].mutex, "SurgeryShutdown");
  }
  for (int i = 0; i < MAX_CONCURRENT_SURGERIES; i++)
    if (surgery_list[i].thread_started)
      pthread_join(surgery_list[i].thread, NULL);

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
