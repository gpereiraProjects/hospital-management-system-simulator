#ifndef IPC_H
#define IPC_H

#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <time.h>

// --- Definições de Chaves e Ficheiros ---
#define IPC_CONFIG_FILE "config/config.txt"

// IDs para ftok
#define KEY_MQ_URGENT 'U'
#define KEY_MQ_NORMAL 'N'
#define KEY_MQ_RESP 'R'
#define KEY_SHM_STATS 'S'
#define KEY_SHM_BO 'B'
#define KEY_SHM_PHARM 'P'
#define KEY_SHM_LAB 'L'
#define KEY_SHM_LOG 'G'

// --- Message Queues ---
typedef struct {
  long msg_priority; // Tipo de mensagem para msgrcv

  // Payload da mensagem
  int msg_type; // MSG_NEW_EMERGENCY, etc.
  char source[20];
  char target[20];
  char patient_id[16];
  int operation_id;
  time_t timestamp;
  char data[1024];
} hospital_message_t;

// Tipos de Mensagem
#define MSG_NEW_EMERGENCY 1
#define MSG_NEW_APPOINTMENT 2
#define MSG_NEW_SURGERY 3
#define MSG_PHARMACY_REQUEST 4
#define MSG_LAB_REQUEST 5
#define MSG_PHARMACY_READY 6
#define MSG_LAB_RESULTS_READY 7
#define MSG_CRITICAL_STATUS 8
#define MSG_TRANSFER_PATIENT 9
#define MSG_REJECT_PATIENT 10
#define MSG_RESTOCK 11

// --- Named Pipes ---
#define PIPE_INPUT "input_pipe"
#define PIPE_TRIAGE "triage_pipe"
#define PIPE_SURGERY "surgery_pipe"
#define PIPE_PHARMACY "pharmacy_pipe"
#define PIPE_LAB "lab_pipe"

// --- Semáforos POSIX ---
#define SEM_BO1_NAME "/sem_surgery_bo1"
#define SEM_BO2_NAME "/sem_surgery_bo2"
#define SEM_BO3_NAME "/sem_surgery_bo3"
#define SEM_TEAMS_NAME "/sem_medical_teams"
#define SEM_LAB1_NAME "/sem_lab1_equipment"
#define SEM_LAB2_NAME "/sem_lab2_equipment"
#define SEM_PHARM_NAME "/sem_pharmacy_access"

// --- Protótipos de Funções IPC (Implementadas em src/ipc_utils.c) ---
// Estas funções são exigidas nas secções 6.1 e 6.2 [cite: 598-602, 707-710]

int create_msg_queue(key_t key);
int create_shm(key_t key, size_t size);
sem_t *create_sem(const char *name, int initial_value);
int create_fifo(const char *path);
int cleanup_project_ipc(void);

void remove_msg_queue(int msgid);
void remove_shm(int shmid);
void close_sem(sem_t *sem);
void unlink_sem(const char *name);

#endif
