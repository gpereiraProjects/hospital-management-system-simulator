#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <time.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/log.h"
#include "../include/patient_thread.h"
#include "../include/stats.h"

extern int mq_urgent_id;
extern int mq_normal_id;
extern global_statistics_t *g_stats_ptr;
extern volatile sig_atomic_t shutdown_requested;
extern system_config_t config;

static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t lifecycle_cond = PTHREAD_COND_INITIALIZER;
static size_t active_threads;

static void thread_finished(void) {
  pthread_mutex_lock(&lifecycle_mutex);
  active_threads--;
  if (active_threads == 0)
    pthread_cond_broadcast(&lifecycle_cond);
  pthread_mutex_unlock(&lifecycle_mutex);
}

static void prepare_message(const command_t *command, hospital_message_t *message) {
  memset(message, 0, sizeof(*message));
  snprintf(message->source, sizeof(message->source), "MAIN");
  snprintf(message->patient_id, sizeof(message->patient_id), "%s", command->id);
  snprintf(message->data, sizeof(message->data), "%s", command->raw);
  message->command = *command;
  message->timestamp = time(NULL);

  switch (command->kind) {
  case COMMAND_EMERGENCY:
    message->msg_type = MSG_NEW_EMERGENCY;
    message->operation_id = command->triage_level;
    snprintf(message->target, sizeof(message->target), "TRIAGE");
    break;
  case COMMAND_APPOINTMENT:
    message->msg_type = MSG_NEW_APPOINTMENT;
    message->operation_id = command->scheduled_time;
    snprintf(message->target, sizeof(message->target), "TRIAGE");
    break;
  case COMMAND_SURGERY:
    message->msg_type = MSG_NEW_SURGERY;
    snprintf(message->target, sizeof(message->target), "SURGERY");
    break;
  case COMMAND_PHARMACY_REQUEST:
    message->msg_type = MSG_PHARMACY_REQUEST;
    snprintf(message->target, sizeof(message->target), "PHARMACY");
    break;
  case COMMAND_LAB_REQUEST:
    message->msg_type = MSG_LAB_REQUEST;
    snprintf(message->target, sizeof(message->target), "LAB");
    break;
  case COMMAND_RESTOCK:
    message->msg_type = MSG_RESTOCK;
    snprintf(message->target, sizeof(message->target), "PHARMACY");
    snprintf(message->patient_id, sizeof(message->patient_id), "RESTOCK");
    break;
  case COMMAND_STATUS:
    break;
  }
  message->msg_priority = message->msg_type;
  if (command->kind == COMMAND_PHARMACY_REQUEST) {
    if (command->priority == COMMAND_PRIORITY_URGENT)
      message->msg_priority = PHARMACY_URGENT;
    else if (command->priority == COMMAND_PRIORITY_HIGH)
      message->msg_priority = PHARMACY_HIGH;
    else
      message->msg_priority = PHARMACY_NORMAL;
  }
}

static void *patient_lifecycle_thread(void *argument) {
  command_t *command = argument;
  while (!shutdown_requested && command->init_time >= 0) {
    int current_time = 0;
    if (g_stats_ptr != NULL) {
      pthread_mutex_lock(&g_stats_ptr->mutex);
      current_time = g_stats_ptr->simulation_time_units;
      pthread_mutex_unlock(&g_stats_ptr->mutex);
    }
    if (current_time >= command->init_time)
      break;
    usleep((useconds_t)config.time_unit_ms * 1000U);
  }
  if (shutdown_requested) {
    free(command);
    thread_finished();
    return NULL;
  }

  hospital_message_t message;
  prepare_message(command, &message);

  int urgent =
      command->kind == COMMAND_SURGERY ||
      (command->kind == COMMAND_LAB_REQUEST && command->priority == COMMAND_PRIORITY_URGENT) ||
      (command->kind == COMMAND_PHARMACY_REQUEST && command->priority >= COMMAND_PRIORITY_HIGH);
  int destination = urgent ? mq_urgent_id : mq_normal_id;
  size_t size = sizeof(message) - sizeof(message.msg_priority);

  if (msgsnd(destination, &message, size, IPC_NOWAIT) == -1) {
    char detail[160];
    snprintf(detail, sizeof(detail), "%s: %s", command->id, strerror(errno));
    log_event(LOG_ERROR, "INPUT", "QUEUE_REJECT", detail);
    if (g_stats_ptr != NULL) {
      pthread_mutex_lock(&g_stats_ptr->mutex);
      g_stats_ptr->system_errors++;
      pthread_mutex_unlock(&g_stats_ptr->mutex);
    }
  } else if (g_stats_ptr != NULL) {
    pthread_mutex_lock(&g_stats_ptr->mutex);
    g_stats_ptr->total_operations++;
    pthread_mutex_unlock(&g_stats_ptr->mutex);
  }

  free(command);
  thread_finished();
  return NULL;
}

int start_patient_thread(const command_t *command) {
  command_t *copy = malloc(sizeof(*copy));
  if (copy == NULL)
    return -1;
  *copy = *command;

  pthread_mutex_lock(&lifecycle_mutex);
  active_threads++;
  pthread_mutex_unlock(&lifecycle_mutex);

  pthread_t thread;
  int error = pthread_create(&thread, NULL, patient_lifecycle_thread, copy);
  if (error != 0) {
    free(copy);
    thread_finished();
    errno = error;
    return -1;
  }
  pthread_detach(thread);
  return 0;
}

void wait_for_patient_threads(void) {
  pthread_mutex_lock(&lifecycle_mutex);
  while (active_threads != 0)
    pthread_cond_wait(&lifecycle_cond, &lifecycle_mutex);
  pthread_mutex_unlock(&lifecycle_mutex);
}
