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

static system_config_t pharmacy_config;
static global_statistics_t *statistics;
static pharmacy_shm_t *stock;
static atomic_bool keep_running = ATOMIC_VAR_INIT(true);
static int urgent_queue = -1;
static int normal_queue = -1;
static int response_queue = -1;
static sem_t *access_semaphore;
static pthread_mutex_t stock_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stock_changed = PTHREAD_COND_INITIALIZER;

typedef struct {
  int queue;
  int prioritized;
} worker_argument_t;

static void handle_shutdown(int signal_number) {
  (void)signal_number;
  keep_running = 0;
}

static long response_type(const char *target) {
  if (strcmp(target, "SURGERY") == 0)
    return RESPONSE_SURGERY;
  if (strcmp(target, "TRIAGE") == 0)
    return RESPONSE_TRIAGE;
  return RESPONSE_MAIN;
}

static int medication_index(const char *name) {
  for (int i = 0; i < MAX_MED_TYPES; i++)
    if (strcmp(stock->medications[i].name, name) == 0)
      return i;
  return -1;
}

static int default_quantity(int index) {
  static const int minimums[MAX_MED_TYPES] = {5,  10, 20, 10, 5,  15, 20, 10,
                                              15, 10, 5,  10, 15, 5,  10};
  return minimums[index];
}

static int request_available(const command_t *command) {
  int available = 1;
  pthread_mutex_lock(&stock->global_mutex);
  for (size_t i = 0; i < command->medication_count; i++) {
    int index = medication_index(command->medications[i].name);
    if (index < 0) {
      available = 0;
      break;
    }
    int quantity = command->medications[i].quantity > 0 ? command->medications[i].quantity
                                                        : default_quantity(index);
    medication_stock_t *medication = &stock->medications[index];
    pthread_mutex_lock(&medication->mutex);
    if (medication->current_stock < quantity)
      available = 0;
    pthread_mutex_unlock(&medication->mutex);
    if (!available)
      break;
  }
  pthread_mutex_unlock(&stock->global_mutex);
  return available;
}

static int reserve_request(command_t *command) {
  pthread_mutex_lock(&stock->global_mutex);
  for (size_t i = 0; i < command->medication_count; i++) {
    int index = medication_index(command->medications[i].name);
    if (index < 0) {
      pthread_mutex_unlock(&stock->global_mutex);
      return -1;
    }
    if (command->medications[i].quantity == 0)
      command->medications[i].quantity = default_quantity(index);
    medication_stock_t *medication = &stock->medications[index];
    pthread_mutex_lock(&medication->mutex);
    int sufficient = medication->current_stock >= command->medications[i].quantity;
    pthread_mutex_unlock(&medication->mutex);
    if (!sufficient) {
      pthread_mutex_unlock(&stock->global_mutex);
      return 1;
    }
  }

  for (size_t i = 0; i < command->medication_count; i++) {
    int index = medication_index(command->medications[i].name);
    medication_stock_t *medication = &stock->medications[index];
    pthread_mutex_lock(&medication->mutex);
    medication->current_stock -= command->medications[i].quantity;
    medication->reserved += command->medications[i].quantity;
    pthread_mutex_unlock(&medication->mutex);
  }
  stock->total_active_requests++;
  pthread_mutex_unlock(&stock->global_mutex);
  return 0;
}

static void finish_request(const command_t *command) {
  pthread_mutex_lock(&stock->global_mutex);
  for (size_t i = 0; i < command->medication_count; i++) {
    int index = medication_index(command->medications[i].name);
    medication_stock_t *medication = &stock->medications[index];
    pthread_mutex_lock(&medication->mutex);
    medication->reserved -= command->medications[i].quantity;
    pthread_mutex_unlock(&medication->mutex);
  }
  stock->total_active_requests--;
  pthread_mutex_unlock(&stock->global_mutex);
}

static void wait_for_stock(const command_t *command) {
  pthread_mutex_lock(&stock_wait_mutex);
  while (keep_running && !request_available(command)) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += (long)pharmacy_config.time_unit_ms * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    pthread_cond_timedwait(&stock_changed, &stock_wait_mutex, &deadline);
  }
  pthread_mutex_unlock(&stock_wait_mutex);
}

static const char *priority_name(command_priority_t priority) {
  if (priority == COMMAND_PRIORITY_URGENT)
    return "URGENT";
  if (priority == COMMAND_PRIORITY_HIGH)
    return "HIGH";
  return "NORMAL";
}

static int create_receipt(const hospital_message_t *message, const command_t *command,
                          int preparation_time) {
  char path[256];
  snprintf(path, sizeof(path), "results/pharmacy_deliveries/pharmacy_delivery_%s_%ld.txt",
           message->patient_id, time(NULL));
  FILE *file = fopen(path, "w");
  if (file == NULL)
    return -1;

  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
  fprintf(file, "=========================================\n");
  fprintf(file, "COMPROVATIVO DE ENTREGA - FARMACIA CENTRAL\n");
  fprintf(file, "=========================================\n");
  fprintf(file, "Pedido: %s\nData/Hora: %s\nDestino: %s\nPrioridade: %s\n\n", message->patient_id,
          timestamp, message->source, priority_name(command->priority));
  fprintf(file, "Medicamentos Fornecidos:\n-------------------------\n");
  for (size_t i = 0; i < command->medication_count; i++) {
    int index = medication_index(command->medications[i].name);
    pthread_mutex_lock(&stock->medications[index].mutex);
    int current = stock->medications[index].current_stock;
    pthread_mutex_unlock(&stock->medications[index].mutex);
    fprintf(file, "%zu. %s\n   Quantidade: %d unidades\n   Stock atual: %d unidades\n", i + 1,
            command->medications[i].name, command->medications[i].quantity, current);
  }
  fprintf(file, "\nTempo de Preparacao: %d time units\nEstado: ENTREGUE\n", preparation_time);
  return fclose(file);
}

static void send_response(const hospital_message_t *request) {
  if (strcmp(request->source, "MAIN") == 0)
    return;
  hospital_message_t response;
  memset(&response, 0, sizeof(response));
  response.msg_priority = response_type(request->source);
  response.msg_type = MSG_PHARMACY_READY;
  snprintf(response.source, sizeof(response.source), "PHARMACY");
  snprintf(response.target, sizeof(response.target), "%s", request->source);
  snprintf(response.patient_id, sizeof(response.patient_id), "%s", request->patient_id);
  response.timestamp = time(NULL);
  size_t size = sizeof(response) - sizeof(response.msg_priority);
  if (msgsnd(response_queue, &response, size, IPC_NOWAIT) == -1 && errno != EIDRM)
    log_event(LOG_ERROR, "PHARMACY", "RESPONSE_FAIL", strerror(errno));
}

static void process_request(hospital_message_t *message) {
  command_t command = message->command;
  while (keep_running) {
    wait_for_stock(&command);
    if (!keep_running)
      return;
    int result = reserve_request(&command);
    if (result == 0)
      break;
    if (result < 0)
      return;
  }

  int semaphore_result;
  do {
    semaphore_result = sem_wait(access_semaphore);
  } while (semaphore_result == -1 && errno == EINTR);
  if (semaphore_result == -1) {
    finish_request(&command);
    return;
  }
  int range = pharmacy_config.pharm_prep_time_max - pharmacy_config.pharm_prep_time_min + 1;
  int preparation_time = pharmacy_config.pharm_prep_time_min + rand() % range;
  wait_time_units(preparation_time);
  finish_request(&command);
  sem_post(access_semaphore);

  if (create_receipt(message, &command, preparation_time) != 0)
    log_event(LOG_ERROR, "PHARMACY", "RECEIPT_FAIL", strerror(errno));
  send_response(message);

  pthread_mutex_lock(&statistics->mutex);
  statistics->total_pharmacy_requests++;
  if (command.priority == COMMAND_PRIORITY_URGENT)
    statistics->urgent_requests++;
  else
    statistics->normal_requests++;
  statistics->total_pharmacy_response_time += preparation_time;
  pthread_mutex_unlock(&statistics->mutex);
  log_event(LOG_INFO, "PHARMACY", "DELIVERED", message->patient_id);
}

static void manual_restock(const hospital_message_t *message) {
  int index = medication_index(message->command.restock_medication);
  if (index < 0)
    return;
  medication_stock_t *medication = &stock->medications[index];
  pthread_mutex_lock(&medication->mutex);
  medication->current_stock += message->command.restock_quantity;
  pthread_mutex_unlock(&medication->mutex);
  char detail[128];
  snprintf(detail, sizeof(detail), "%s +%d", medication->name, message->command.restock_quantity);
  log_event(LOG_INFO, "PHARMACY", "RESTOCK_MANUAL", detail);
  pthread_mutex_lock(&stock_wait_mutex);
  pthread_cond_broadcast(&stock_changed);
  pthread_mutex_unlock(&stock_wait_mutex);
}

static void *request_worker(void *argument) {
  worker_argument_t *worker = argument;
  hospital_message_t message;
  size_t size = sizeof(message) - sizeof(message.msg_priority);
  while (keep_running) {
    ssize_t received;
    if (worker->prioritized) {
      received = msgrcv(worker->queue, &message, size, PHARMACY_URGENT, IPC_NOWAIT);
      if (received == -1 && errno == ENOMSG)
        received = msgrcv(worker->queue, &message, size, PHARMACY_HIGH, IPC_NOWAIT);
      if (received == -1 && errno == ENOMSG) {
        usleep((useconds_t)pharmacy_config.time_unit_ms * 1000U);
        continue;
      }
    } else {
      received = msgrcv(worker->queue, &message, size, PHARMACY_NORMAL, 0);
    }
    if (received == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    process_request(&message);
  }
  return NULL;
}

static void automatic_restock(void) {
  if (!pharmacy_config.auto_restock_enabled)
    return;
  for (int i = 0; i < MAX_MED_TYPES; i++) {
    medication_stock_t *medication = &stock->medications[i];
    int quantity = 0;
    pthread_mutex_lock(&medication->mutex);
    if (medication->current_stock < medication->threshold) {
      quantity = medication->max_capacity * pharmacy_config.restock_qty_multiplier;
      medication->current_stock += quantity;
    }
    pthread_mutex_unlock(&medication->mutex);
    if (quantity > 0) {
      char detail[128];
      snprintf(detail, sizeof(detail), "%s +%d", medication->name, quantity);
      log_event(LOG_INFO, "PHARMACY", "RESTOCK_AUTO", detail);
      pthread_mutex_lock(&statistics->mutex);
      statistics->auto_restocks++;
      pthread_mutex_unlock(&statistics->mutex);
      pthread_mutex_lock(&stock_wait_mutex);
      pthread_cond_broadcast(&stock_changed);
      pthread_mutex_unlock(&stock_wait_mutex);
    }
  }
}

static void *stock_monitor(void *argument) {
  (void)argument;
  hospital_message_t message;
  size_t size = sizeof(message) - sizeof(message.msg_priority);
  while (keep_running) {
    while (msgrcv(normal_queue, &message, size, MSG_RESTOCK, IPC_NOWAIT) != -1)
      manual_restock(&message);
    if (errno != ENOMSG && errno != EIDRM && errno != EINVAL)
      log_event(LOG_ERROR, "PHARMACY", "RESTOCK_QUEUE_FAIL", strerror(errno));
    automatic_restock();
    wait_time_units(1);
  }
  pthread_mutex_lock(&stock_wait_mutex);
  pthread_cond_broadcast(&stock_changed);
  pthread_mutex_unlock(&stock_wait_mutex);
  return NULL;
}

static void *statistics_worker(void *argument) {
  (void)argument;
  while (keep_running) {
    wait_time_units(10);
    int active;
    pthread_mutex_lock(&stock->global_mutex);
    active = stock->total_active_requests;
    pthread_mutex_unlock(&stock->global_mutex);
    char detail[64];
    snprintf(detail, sizeof(detail), "active_requests=%d", active);
    log_event(LOG_DEBUG, "PHARMACY", "STATUS", detail);
  }
  return NULL;
}

static int attach_resources(void) {
  int statistics_id = shmget(ftok(IPC_CONFIG_FILE, KEY_SHM_STATS), sizeof(*statistics), 0600);
  int stock_id = shmget(ftok(IPC_CONFIG_FILE, KEY_SHM_PHARM), sizeof(*stock), 0600);
  if (statistics_id == -1 || stock_id == -1)
    return -1;
  statistics = shmat(statistics_id, NULL, 0);
  stock = shmat(stock_id, NULL, 0);
  if (statistics == (void *)-1 || stock == (void *)-1)
    return -1;
  urgent_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT), 0600);
  normal_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_NORMAL), 0600);
  response_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP), 0600);
  access_semaphore = sem_open(SEM_PHARM_NAME, 0);
  return urgent_queue == -1 || normal_queue == -1 || response_queue == -1 ||
                 access_semaphore == SEM_FAILED
             ? -1
             : 0;
}

int pharmacy_main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);
  srand((unsigned int)getpid());
  if (load_config(IPC_CONFIG_FILE, &pharmacy_config) != 0 || attach_resources() != 0)
    return -1;

  pthread_t urgent_worker;
  pthread_t normal_worker;
  pthread_t restock_worker;
  pthread_t stats_worker;
  worker_argument_t urgent_argument = {urgent_queue, 1};
  worker_argument_t normal_argument = {normal_queue, 0};
  if (pthread_create(&urgent_worker, NULL, request_worker, &urgent_argument) != 0 ||
      pthread_create(&normal_worker, NULL, request_worker, &normal_argument) != 0 ||
      pthread_create(&restock_worker, NULL, stock_monitor, NULL) != 0 ||
      pthread_create(&stats_worker, NULL, statistics_worker, NULL) != 0)
    return -1;

  pthread_join(urgent_worker, NULL);
  pthread_join(normal_worker, NULL);
  keep_running = 0;
  pthread_join(restock_worker, NULL);
  pthread_join(stats_worker, NULL);
  shmdt(statistics);
  shmdt(stock);
  sem_close(access_semaphore);
  return 0;
}
