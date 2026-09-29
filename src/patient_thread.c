#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <time.h>
#include <unistd.h>

#include "../include/hospital.h"
#include "../include/ipc.h"
#include "../include/log.h"
#include "../include/stats.h"

extern int mq_urgent_id;
extern int mq_normal_id;
extern global_statistics_t *g_stats_ptr;

typedef struct {
  char command_line[512];
} thread_arg_t;

static void parse_command(char *cmd, hospital_message_t *msg) {
  char *saveptr;
  char buffer[512];
  snprintf(buffer, sizeof(buffer), "%s", cmd);

  char *token = strtok_r(buffer, " ", &saveptr);
  if (!token)
    return;

  if (strcmp(token, "EMERGENCY") == 0) {
    msg->msg_type = MSG_NEW_EMERGENCY;
    strcpy(msg->target, "TRIAGE");
  } else if (strcmp(token, "APPOINTMENT") == 0) {
    msg->msg_type = MSG_NEW_APPOINTMENT;
    strcpy(msg->target, "TRIAGE");
  } else if (strcmp(token, "SURGERY") == 0) {
    msg->msg_type = MSG_NEW_SURGERY;
    strcpy(msg->target, "SURGERY");
  } else if (strcmp(token, "PHARMACY_REQUEST") == 0) {
    msg->msg_type = MSG_PHARMACY_REQUEST;
    strcpy(msg->target, "PHARMACY");
  } else if (strcmp(token, "LAB_REQUEST") == 0) {
    msg->msg_type = MSG_LAB_REQUEST;
    strcpy(msg->target, "LAB");
  }

  token = strtok_r(NULL, " ", &saveptr);
  if (token) {
    snprintf(msg->patient_id, sizeof(msg->patient_id), "%s", token);
  } else {
    strcpy(msg->patient_id, "UNKNOWN");
  }

  snprintf(msg->data, sizeof(msg->data), "%s", cmd);
  msg->timestamp = time(NULL);

  /* O msg_priority (mtype) TEM de ser igual ao msg_type para que os
  // processos destinatários (Surgery, Lab, Pharm) consigam filtrar e capturar a
   mensagem.*/
  msg->msg_priority = msg->msg_type;
}

void *patient_lifecycle_thread(void *arg) {
  thread_arg_t *args = (thread_arg_t *)arg;
  hospital_message_t msg;
  memset(&msg, 0, sizeof(msg));

  parse_command(args->command_line, &msg);

  // Seleção de Fila
  int dest_queue = mq_normal_id;

  // 1. Cirurgias vão sempre para URGENTE
  if (msg.msg_type == MSG_NEW_SURGERY) {
    dest_queue = mq_urgent_id;
  }
  // 2. Pedidos diretos (Farm/Lab) urgentes vão para URGENTE
  else if ((msg.msg_type == MSG_PHARMACY_REQUEST || msg.msg_type == MSG_LAB_REQUEST) &&
           strstr(args->command_line, "URGENT") != NULL) {
    dest_queue = mq_urgent_id;
  }
  // 3. Emergências e Consultas vão para NORMAL
  else if (msg.msg_type == MSG_NEW_EMERGENCY || msg.msg_type == MSG_NEW_APPOINTMENT) {
    dest_queue = mq_normal_id;
  }

  size_t sz = sizeof(hospital_message_t) - sizeof(long);
  if (msgsnd(dest_queue, &msg, sz, 0) == -1) {
    perror("[PatientThread] Erro msgsnd");
  } else {
    if (g_stats_ptr) {
      pthread_mutex_lock(&g_stats_ptr->mutex);
      g_stats_ptr->total_operations++;
      pthread_mutex_unlock(&g_stats_ptr->mutex);
    }
  }

  free(args);
  return NULL;
}

void start_patient_thread(const char *command) {
  pthread_t t;
  thread_arg_t *args = malloc(sizeof(thread_arg_t));
  if (!args)
    return;

  snprintf(args->command_line, sizeof(args->command_line), "%s", command);
  args->command_line[strcspn(args->command_line, "\n")] = 0;

  if (pthread_create(&t, NULL, patient_lifecycle_thread, args) != 0) {
    free(args);
  } else {
    pthread_detach(t);
  }
}