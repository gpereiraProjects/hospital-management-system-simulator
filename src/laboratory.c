#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
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

typedef struct {
  hospital_message_t message;
} lab_task_t;

static system_config_t laboratory_config;
static global_statistics_t *statistics;
static lab_queue_shm_t *lab_state;
static atomic_bool keep_running = ATOMIC_VAR_INIT(true);
static int urgent_queue = -1;
static int normal_queue = -1;
static int response_queue = -1;
static sem_t *lab1_semaphore;
static sem_t *lab2_semaphore;
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t lifecycle_condition = PTHREAD_COND_INITIALIZER;
static int active_tasks;

static void handle_shutdown(int signal_number) {
  (void)signal_number;
  keep_running = 0;
}

static int is_lab1_test(const char *test) {
  return strcmp(test, "HEMO") == 0 || strcmp(test, "GLIC") == 0;
}

static int random_duration(int minimum, int maximum) {
  return minimum + rand() % (maximum - minimum + 1);
}

static void update_lab_state(int lab_number, int delta) {
  pthread_mutex_t *mutex = lab_number == 1 ? &lab_state->lab1_mutex : &lab_state->lab2_mutex;
  pthread_mutex_lock(mutex);
  if (lab_number == 1) {
    lab_state->lab1_count += delta;
    lab_state->lab1_available_slots -= delta;
  } else {
    lab_state->lab2_count += delta;
    lab_state->lab2_available_slots -= delta;
  }
  pthread_mutex_unlock(mutex);
}

static int execute_in_lab(int lab_number) {
  sem_t *semaphore = lab_number == 1 ? lab1_semaphore : lab2_semaphore;
  while (sem_wait(semaphore) == -1) {
    if (errno != EINTR)
      return -1;
  }
  update_lab_state(lab_number, 1);
  int duration = lab_number == 1
                     ? random_duration(laboratory_config.lab1_min, laboratory_config.lab1_max)
                     : random_duration(laboratory_config.lab2_min, laboratory_config.lab2_max);
  wait_time_units(duration);
  update_lab_state(lab_number, -1);
  sem_post(semaphore);
  return duration;
}

static const char *test_description(const char *test) {
  if (strcmp(test, "HEMO") == 0)
    return "Hemograma completo: hemoglobina 14.2 g/dL; leucocitos 7500/uL";
  if (strcmp(test, "GLIC") == 0)
    return "Glicemia: glicose em jejum 95 mg/dL";
  if (strcmp(test, "COLEST") == 0)
    return "Colesterol total: 180 mg/dL";
  if (strcmp(test, "RENAL") == 0)
    return "Funcao renal: creatinina 0.9 mg/dL";
  if (strcmp(test, "HEPAT") == 0)
    return "Funcao hepatica: ALT 24 U/L; AST 22 U/L";
  return "Pre-operatorio: parametros hematologicos e bioquimicos validados";
}

static int create_result(const hospital_message_t *message, int total_duration) {
  char path[256];
  snprintf(path, sizeof(path), "results/lab_results/lab_results_%s_%ld.txt", message->patient_id,
           time(NULL));
  FILE *file = fopen(path, "w");
  if (file == NULL)
    return -1;

  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
  fprintf(file, "=====================================\n");
  fprintf(file, "RELATORIO DE ANALISES LABORATORIAIS\n");
  fprintf(file, "=====================================\n");
  fprintf(file, "Pedido/Paciente: %s\nData/Hora: %s\nLaboratorio: %s\nPrioridade: %s\n\n",
          message->patient_id, timestamp, message->command.lab[0] ? message->command.lab : "BOTH",
          message->command.priority == COMMAND_PRIORITY_URGENT ? "URGENT" : "NORMAL");
  fprintf(file, "Analises Realizadas:\n--------------------\n");
  for (size_t i = 0; i < message->command.test_count; i++)
    fprintf(file, "%zu. %s\n   %s\n", i + 1, message->command.tests[i],
            test_description(message->command.tests[i]));
  fprintf(file, "\nObservacoes: parametros dentro dos valores de referencia.\n");
  fprintf(file, "Tempo total: %d time units\nEstado: VALIDADO\n", total_duration);
  return fclose(file);
}

static long response_type(const char *target) {
  if (strcmp(target, "SURGERY") == 0)
    return RESPONSE_SURGERY;
  if (strcmp(target, "TRIAGE") == 0)
    return RESPONSE_TRIAGE;
  return RESPONSE_MAIN;
}

static void send_response(const hospital_message_t *request) {
  if (strcmp(request->source, "MAIN") == 0)
    return;
  hospital_message_t response;
  memset(&response, 0, sizeof(response));
  response.msg_priority = response_type(request->source);
  response.msg_type = MSG_LAB_RESULTS_READY;
  snprintf(response.source, sizeof(response.source), "LAB");
  snprintf(response.target, sizeof(response.target), "%s", request->source);
  snprintf(response.patient_id, sizeof(response.patient_id), "%s", request->patient_id);
  response.timestamp = time(NULL);
  size_t size = sizeof(response) - sizeof(response.msg_priority);
  if (msgsnd(response_queue, &response, size, IPC_NOWAIT) == -1 && errno != EIDRM)
    log_event(LOG_ERROR, "LAB", "RESPONSE_FAIL", strerror(errno));
}

static void task_finished(void) {
  pthread_mutex_lock(&lifecycle_mutex);
  active_tasks--;
  if (active_tasks == 0)
    pthread_cond_broadcast(&lifecycle_condition);
  pthread_mutex_unlock(&lifecycle_mutex);
}

static void *execute_request(void *argument) {
  lab_task_t *task = argument;
  hospital_message_t *message = &task->message;
  int total_duration = 0;
  int lab1_tests = 0;
  int lab2_tests = 0;
  int preop_tests = 0;

  for (size_t i = 0; i < message->command.test_count; i++) {
    const char *test = message->command.tests[i];
    if (strcmp(test, "PREOP") == 0) {
      int first = execute_in_lab(1);
      int second = execute_in_lab(2);
      if (first > 0 && second > 0)
        total_duration += first + second;
      lab1_tests++;
      lab2_tests++;
      preop_tests++;
    } else {
      int lab_number = is_lab1_test(test) ? 1 : 2;
      int duration = execute_in_lab(lab_number);
      if (duration > 0)
        total_duration += duration;
      if (lab_number == 1)
        lab1_tests++;
      else
        lab2_tests++;
    }
  }

  if (create_result(message, total_duration) != 0)
    log_event(LOG_ERROR, "LAB", "RESULT_FAIL", strerror(errno));
  send_response(message);
  pthread_mutex_lock(&statistics->mutex);
  statistics->total_lab_tests_lab1 += lab1_tests;
  statistics->total_lab_tests_lab2 += lab2_tests;
  statistics->total_preop_tests += preop_tests;
  statistics->total_lab_turnaround_time += total_duration;
  if (message->command.priority == COMMAND_PRIORITY_URGENT)
    statistics->urgent_lab_tests += (int)message->command.test_count;
  pthread_mutex_unlock(&statistics->mutex);
  log_event(LOG_INFO, "LAB", "COMPLETE", message->patient_id);
  free(task);
  task_finished();
  return NULL;
}

static void *request_manager(void *argument) {
  int queue = *(int *)argument;
  hospital_message_t message;
  size_t size = sizeof(message) - sizeof(message.msg_priority);
  while (keep_running) {
    if (msgrcv(queue, &message, size, MSG_LAB_REQUEST, 0) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    lab_task_t *task = malloc(sizeof(*task));
    if (task == NULL)
      continue;
    task->message = message;
    pthread_mutex_lock(&lifecycle_mutex);
    active_tasks++;
    pthread_mutex_unlock(&lifecycle_mutex);
    pthread_t thread;
    if (pthread_create(&thread, NULL, execute_request, task) != 0) {
      free(task);
      task_finished();
      continue;
    }
    pthread_detach(thread);
  }
  return NULL;
}

static int attach_resources(void) {
  int statistics_id = shmget(ftok(IPC_CONFIG_FILE, KEY_SHM_STATS), sizeof(*statistics), 0600);
  int lab_id = shmget(ftok(IPC_CONFIG_FILE, KEY_SHM_LAB), sizeof(*lab_state), 0600);
  if (statistics_id == -1 || lab_id == -1)
    return -1;
  statistics = shmat(statistics_id, NULL, 0);
  lab_state = shmat(lab_id, NULL, 0);
  if (statistics == (void *)-1 || lab_state == (void *)-1)
    return -1;
  urgent_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT), 0600);
  normal_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_NORMAL), 0600);
  response_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP), 0600);
  lab1_semaphore = sem_open(SEM_LAB1_NAME, 0);
  lab2_semaphore = sem_open(SEM_LAB2_NAME, 0);
  return urgent_queue == -1 || normal_queue == -1 || response_queue == -1 ||
                 lab1_semaphore == SEM_FAILED || lab2_semaphore == SEM_FAILED
             ? -1
             : 0;
}

int laboratory_main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);
  srand((unsigned int)getpid());
  if (load_config(IPC_CONFIG_FILE, &laboratory_config) != 0 || attach_resources() != 0)
    return -1;

  pthread_t urgent_manager;
  pthread_t normal_manager;
  if (pthread_create(&urgent_manager, NULL, request_manager, &urgent_queue) != 0 ||
      pthread_create(&normal_manager, NULL, request_manager, &normal_queue) != 0)
    return -1;
  pthread_join(urgent_manager, NULL);
  pthread_join(normal_manager, NULL);

  pthread_mutex_lock(&lifecycle_mutex);
  while (active_tasks != 0)
    pthread_cond_wait(&lifecycle_condition, &lifecycle_mutex);
  pthread_mutex_unlock(&lifecycle_mutex);
  shmdt(statistics);
  shmdt(lab_state);
  sem_close(lab1_semaphore);
  sem_close(lab2_semaphore);
  return 0;
}
