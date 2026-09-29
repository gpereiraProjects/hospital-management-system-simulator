#include <errno.h>
#include <pthread.h>
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
  command_t command;
  int state; // 0=free, 1=waiting, 2=in treatment
  int critical;
  int lab_complete;
  int pharmacy_complete;
  int arrival_time;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
} patient_t;

typedef struct {
  int message_type;
} manager_argument_t;

static patient_t *patients;
static int patient_capacity;
static system_config_t triage_config;
static global_statistics_t *statistics;
static atomic_bool keep_running = ATOMIC_VAR_INIT(true);
static int normal_queue = -1;
static int urgent_queue = -1;
static int response_queue = -1;
static pthread_mutex_t patient_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static void handle_shutdown(int signal_number) {
  (void)signal_number;
  keep_running = 0;
}

static int simulation_time(void) {
  int value;
  pthread_mutex_lock(&statistics->mutex);
  value = statistics->simulation_time_units;
  pthread_mutex_unlock(&statistics->mutex);
  return value;
}

static int is_emergency(const patient_t *patient) {
  return patient->command.kind == COMMAND_EMERGENCY;
}

static int active_count(command_kind_t kind) {
  int count = 0;
  for (int i = 0; i < patient_capacity; i++)
    if (patients[i].state != 0 && patients[i].command.kind == kind)
      count++;
  return count;
}

static int duplicate_identifier(const char *identifier) {
  for (int i = 0; i < patient_capacity; i++)
    if (patients[i].state != 0 && strcmp(patients[i].command.id, identifier) == 0)
      return 1;
  return 0;
}

static void record_rejection(const char *identifier) {
  log_event(LOG_WARNING, "TRIAGE", "REJECT", identifier);
  pthread_mutex_lock(&statistics->mutex);
  statistics->rejected_patients++;
  pthread_mutex_unlock(&statistics->mutex);
}

static void admit_patient(const hospital_message_t *message) {
  pthread_mutex_lock(&patient_list_mutex);
  command_kind_t kind = message->command.kind;
  int limit = kind == COMMAND_EMERGENCY ? triage_config.max_emergency_patients
                                        : triage_config.max_appointments;
  if (active_count(kind) >= limit || duplicate_identifier(message->command.id)) {
    pthread_mutex_unlock(&patient_list_mutex);
    record_rejection(message->command.id);
    return;
  }

  int slot = -1;
  for (int i = 0; i < patient_capacity; i++)
    if (patients[i].state == 0) {
      slot = i;
      break;
    }
  if (slot == -1) {
    pthread_mutex_unlock(&patient_list_mutex);
    record_rejection(message->command.id);
    return;
  }

  patient_t *patient = &patients[slot];
  patient->command = message->command;
  patient->state = 1;
  patient->critical = kind == COMMAND_EMERGENCY &&
                      patient->command.stability <= triage_config.triage_critical_stability;
  patient->lab_complete = patient->command.test_count == 0;
  patient->pharmacy_complete = patient->command.medication_count == 0;
  patient->arrival_time = simulation_time();
  pthread_mutex_unlock(&patient_list_mutex);

  pthread_mutex_lock(&statistics->mutex);
  if (kind == COMMAND_EMERGENCY)
    statistics->total_emergency_patients++;
  else
    statistics->total_appointments++;
  pthread_mutex_unlock(&statistics->mutex);
  log_event(LOG_INFO, "TRIAGE", "ADMIT", patient->command.id);
}

static void *queue_manager(void *argument) {
  manager_argument_t *manager = argument;
  hospital_message_t message;
  size_t size = sizeof(message) - sizeof(message.msg_priority);
  while (keep_running) {
    if (msgrcv(normal_queue, &message, size, manager->message_type, 0) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    admit_patient(&message);
  }
  return NULL;
}

static int better_emergency(int candidate, int current) {
  if (current == -1)
    return 1;
  patient_t *left = &patients[candidate];
  patient_t *right = &patients[current];
  if (left->critical != right->critical)
    return left->critical > right->critical;
  if (left->command.triage_level != right->command.triage_level)
    return left->command.triage_level < right->command.triage_level;
  return left->arrival_time < right->arrival_time;
}

static int select_next_patient(void) {
  int selected = -1;
  int now = simulation_time();
  for (int i = 0; i < patient_capacity; i++)
    if (patients[i].state == 1 && is_emergency(&patients[i]) && better_emergency(i, selected))
      selected = i;
  if (selected != -1)
    return selected;

  for (int i = 0; i < patient_capacity; i++) {
    if (patients[i].state != 1 || patients[i].command.kind != COMMAND_APPOINTMENT ||
        patients[i].command.scheduled_time > now)
      continue;
    if (selected == -1 ||
        patients[i].command.scheduled_time < patients[selected].command.scheduled_time)
      selected = i;
  }
  return selected;
}

static int tests_use_lab1(const command_t *command) {
  for (size_t i = 0; i < command->test_count; i++)
    if (strcmp(command->tests[i], "HEMO") == 0 || strcmp(command->tests[i], "GLIC") == 0)
      return 1;
  return 0;
}

static int tests_use_lab2(const command_t *command) {
  for (size_t i = 0; i < command->test_count; i++)
    if (strcmp(command->tests[i], "COLEST") == 0 || strcmp(command->tests[i], "RENAL") == 0 ||
        strcmp(command->tests[i], "HEPAT") == 0 || strcmp(command->tests[i], "PREOP") == 0)
      return 1;
  return 0;
}

static void send_dependency(patient_t *patient, int message_type) {
  hospital_message_t message;
  memset(&message, 0, sizeof(message));
  message.msg_type = message_type;
  message.msg_priority = message_type;
  snprintf(message.source, sizeof(message.source), "TRIAGE");
  snprintf(message.patient_id, sizeof(message.patient_id), "%s", patient->command.id);
  message.command = patient->command;
  int queue = normal_queue;

  if (message_type == MSG_LAB_REQUEST) {
    message.command.kind = COMMAND_LAB_REQUEST;
    message.command.priority =
        patient->critical ? COMMAND_PRIORITY_URGENT : COMMAND_PRIORITY_NORMAL;
    int lab1 = tests_use_lab1(&patient->command);
    int lab2 = tests_use_lab2(&patient->command);
    snprintf(message.command.lab, sizeof(message.command.lab), "%s",
             lab1 && lab2 ? "BOTH" : (lab1 ? "LAB1" : "LAB2"));
    snprintf(message.target, sizeof(message.target), "LAB");
  } else {
    message.command.kind = COMMAND_PHARMACY_REQUEST;
    message.command.priority = patient->critical ? COMMAND_PRIORITY_URGENT : COMMAND_PRIORITY_HIGH;
    snprintf(message.target, sizeof(message.target), "PHARMACY");
    message.msg_priority = patient->critical ? PHARMACY_URGENT : PHARMACY_HIGH;
  }
  if (message.command.priority != COMMAND_PRIORITY_NORMAL)
    queue = urgent_queue;

  size_t size = sizeof(message) - sizeof(message.msg_priority);
  if (msgsnd(queue, &message, size, IPC_NOWAIT) == -1)
    log_event(LOG_ERROR, "TRIAGE", "DEPENDENCY_FAIL", strerror(errno));
}

static void wait_for_dependencies(patient_t *patient) {
  if (patient->command.test_count > 0)
    send_dependency(patient, MSG_LAB_REQUEST);
  if (patient->command.medication_count > 0)
    send_dependency(patient, MSG_PHARMACY_REQUEST);

  pthread_mutex_lock(&patient->mutex);
  while (keep_running && (!patient->lab_complete || !patient->pharmacy_complete))
    pthread_cond_wait(&patient->condition, &patient->mutex);
  pthread_mutex_unlock(&patient->mutex);
}

static void complete_patient(patient_t *patient, int start_time) {
  int duration = is_emergency(patient) ? triage_config.triage_emergency_duration
                                       : triage_config.triage_appointment_duration;
  wait_time_units(duration);
  pthread_mutex_lock(&statistics->mutex);
  if (is_emergency(patient)) {
    statistics->completed_emergencies++;
    statistics->total_emergency_wait_time += start_time - patient->arrival_time;
  } else {
    statistics->completed_appointments++;
    statistics->total_appointment_wait_time += start_time - patient->arrival_time;
  }
  pthread_mutex_unlock(&statistics->mutex);
  log_event(LOG_INFO, "TRIAGE", "COMPLETE", patient->command.id);
  pthread_mutex_lock(&patient_list_mutex);
  patient->state = 0;
  pthread_mutex_unlock(&patient_list_mutex);
}

static void *doctor_worker(void *argument) {
  (void)argument;
  while (keep_running) {
    pthread_mutex_lock(&patient_list_mutex);
    int selected = select_next_patient();
    if (selected != -1)
      patients[selected].state = 2;
    pthread_mutex_unlock(&patient_list_mutex);
    if (selected == -1) {
      usleep(1000U * (useconds_t)triage_config.time_unit_ms);
      continue;
    }
    patient_t *patient = &patients[selected];
    int start_time = simulation_time();
    wait_for_dependencies(patient);
    if (keep_running)
      complete_patient(patient, start_time);
    else {
      pthread_mutex_lock(&patient_list_mutex);
      patient->state = 0;
      pthread_mutex_unlock(&patient_list_mutex);
    }
  }
  return NULL;
}

static void *response_monitor(void *argument) {
  (void)argument;
  hospital_message_t message;
  size_t size = sizeof(message) - sizeof(message.msg_priority);
  while (keep_running) {
    if (msgrcv(response_queue, &message, size, RESPONSE_TRIAGE, 0) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
    pthread_mutex_lock(&patient_list_mutex);
    for (int i = 0; i < patient_capacity; i++) {
      if (patients[i].state == 0 || strcmp(patients[i].command.id, message.patient_id) != 0)
        continue;
      pthread_mutex_lock(&patients[i].mutex);
      if (message.msg_type == MSG_LAB_RESULTS_READY)
        patients[i].lab_complete = 1;
      else if (message.msg_type == MSG_PHARMACY_READY)
        patients[i].pharmacy_complete = 1;
      pthread_cond_broadcast(&patients[i].condition);
      pthread_mutex_unlock(&patients[i].mutex);
      break;
    }
    pthread_mutex_unlock(&patient_list_mutex);
  }
  return NULL;
}

static void *stability_monitor(void *argument) {
  (void)argument;
  while (keep_running) {
    wait_time_units(1);
    pthread_mutex_lock(&patient_list_mutex);
    for (int i = 0; i < patient_capacity; i++) {
      patient_t *patient = &patients[i];
      if (patient->state != 1 || !is_emergency(patient))
        continue;
      patient->command.stability--;
      if (!patient->critical &&
          patient->command.stability <= triage_config.triage_critical_stability) {
        patient->critical = 1;
        log_event(LOG_CRITICAL, "TRIAGE", "CRITICAL", patient->command.id);
      }
      if (patient->command.stability <= 0) {
        patient->state = 0;
        pthread_mutex_lock(&statistics->mutex);
        statistics->critical_transfers++;
        pthread_mutex_unlock(&statistics->mutex);
        log_event(LOG_CRITICAL, "TRIAGE", "TRANSFER", patient->command.id);
      }
    }
    pthread_mutex_unlock(&patient_list_mutex);
  }
  return NULL;
}

static int attach_resources(void) {
  int statistics_id = shmget(ftok(IPC_CONFIG_FILE, KEY_SHM_STATS), sizeof(*statistics), 0600);
  if (statistics_id == -1)
    return -1;
  statistics = shmat(statistics_id, NULL, 0);
  if (statistics == (void *)-1)
    return -1;
  normal_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_NORMAL), 0600);
  urgent_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT), 0600);
  response_queue = msgget(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP), 0600);
  return normal_queue == -1 || urgent_queue == -1 || response_queue == -1 ? -1 : 0;
}

int triage_main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  signal(SIGINT, handle_shutdown);
  signal(SIGTERM, handle_shutdown);
  if (load_config(IPC_CONFIG_FILE, &triage_config) != 0 || attach_resources() != 0)
    return -1;

  patient_capacity = triage_config.max_emergency_patients + triage_config.max_appointments;
  patients = calloc((size_t)patient_capacity, sizeof(*patients));
  if (patients == NULL)
    return -1;
  for (int i = 0; i < patient_capacity; i++) {
    pthread_mutex_init(&patients[i].mutex, NULL);
    pthread_cond_init(&patients[i].condition, NULL);
  }

  manager_argument_t emergency_argument = {MSG_NEW_EMERGENCY};
  manager_argument_t appointment_argument = {MSG_NEW_APPOINTMENT};
  pthread_t emergency_manager;
  pthread_t appointment_manager;
  pthread_t responses;
  pthread_t stability;
  int doctor_count = triage_config.triage_simultaneous_patients;
  pthread_t *doctors = calloc((size_t)doctor_count, sizeof(*doctors));
  if (doctors == NULL ||
      pthread_create(&emergency_manager, NULL, queue_manager, &emergency_argument) != 0 ||
      pthread_create(&appointment_manager, NULL, queue_manager, &appointment_argument) != 0 ||
      pthread_create(&responses, NULL, response_monitor, NULL) != 0 ||
      pthread_create(&stability, NULL, stability_monitor, NULL) != 0)
    return -1;
  for (int i = 0; i < doctor_count; i++)
    if (pthread_create(&doctors[i], NULL, doctor_worker, NULL) != 0)
      return -1;

  pthread_join(emergency_manager, NULL);
  pthread_join(appointment_manager, NULL);
  pthread_join(responses, NULL);
  keep_running = 0;
  for (int i = 0; i < patient_capacity; i++) {
    pthread_mutex_lock(&patients[i].mutex);
    pthread_cond_broadcast(&patients[i].condition);
    pthread_mutex_unlock(&patients[i].mutex);
  }
  pthread_join(stability, NULL);
  for (int i = 0; i < doctor_count; i++)
    pthread_join(doctors[i], NULL);

  for (int i = 0; i < patient_capacity; i++) {
    pthread_cond_destroy(&patients[i].condition);
    pthread_mutex_destroy(&patients[i].mutex);
  }
  shmdt(statistics);
  free(doctors);
  free(patients);
  return 0;
}
