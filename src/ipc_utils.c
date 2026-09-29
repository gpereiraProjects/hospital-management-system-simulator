#include "../include/ipc.h"
#include "../include/log.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>

// Criação de filas de mensagens com tratamento de erro
int create_msg_queue(key_t key) {
  int id = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
  if (id == -1) {
    log_event(LOG_ERROR, "IPC", "MQ_CREATE_FAIL", strerror(errno));
    perror("Erro msgget");
    return -1;
  }
  return id;
}

// Criação de memória partilhada
int create_shm(key_t key, size_t size) {
  int id = shmget(key, size, IPC_CREAT | IPC_EXCL | 0600);
  if (id == -1) {
    log_event(LOG_ERROR, "IPC", "SHM_CREATE_FAIL", strerror(errno));
    perror("Erro shmget");
    return -1;
  }
  return id;
}

// Criação de semáforos POSIX nomeados
sem_t *create_sem(const char *name, int initial_value) {
  sem_t *sem = sem_open(name, O_CREAT | O_EXCL, 0600, initial_value);
  if (sem == SEM_FAILED) {
    log_event(LOG_ERROR, "IPC", "SEM_CREATE_FAIL", strerror(errno));
    perror("Erro sem_open");
    return SEM_FAILED;
  }
  return sem;
}

int create_fifo(const char *path) {
  if (mkfifo(path, 0600) == -1) {
    log_event(LOG_ERROR, "IPC", "FIFO_CREATE_FAIL", strerror(errno));
    return -1;
  }
  return 0;
}

static void remove_queue_for_key(int project_id) {
  key_t key = ftok(IPC_CONFIG_FILE, project_id);
  if (key == (key_t)-1)
    return;
  int id = msgget(key, 0600);
  if (id != -1)
    msgctl(id, IPC_RMID, NULL);
}

static void remove_shm_for_key(int project_id) {
  key_t key = ftok(IPC_CONFIG_FILE, project_id);
  if (key == (key_t)-1)
    return;
  int id = shmget(key, 1, 0600);
  if (id != -1)
    shmctl(id, IPC_RMID, NULL);
}

int cleanup_project_ipc(void) {
  const int queue_keys[] = {KEY_MQ_URGENT, KEY_MQ_NORMAL, KEY_MQ_RESP};
  const int shm_keys[] = {KEY_SHM_STATS, KEY_SHM_BO, KEY_SHM_PHARM, KEY_SHM_LAB, KEY_SHM_LOG};
  const char *const semaphore_names[] = {SEM_BO1_NAME,   SEM_BO2_NAME,  SEM_BO3_NAME,
                                         SEM_TEAMS_NAME, SEM_LAB1_NAME, SEM_LAB2_NAME,
                                         SEM_PHARM_NAME};
  const char *const fifo_paths[] = {PIPE_INPUT, PIPE_TRIAGE, PIPE_SURGERY, PIPE_PHARMACY, PIPE_LAB};

  for (size_t i = 0; i < sizeof(queue_keys) / sizeof(queue_keys[0]); i++)
    remove_queue_for_key(queue_keys[i]);
  for (size_t i = 0; i < sizeof(shm_keys) / sizeof(shm_keys[0]); i++)
    remove_shm_for_key(shm_keys[i]);
  for (size_t i = 0; i < sizeof(semaphore_names) / sizeof(semaphore_names[0]); i++)
    if (sem_unlink(semaphore_names[i]) == -1 && errno != ENOENT)
      return -1;
  for (size_t i = 0; i < sizeof(fifo_paths) / sizeof(fifo_paths[0]); i++)
    if (unlink(fifo_paths[i]) == -1 && errno != ENOENT)
      return -1;
  return 0;
}

// Funções de limpeza
void remove_msg_queue(int msgid) {
  if (msgid != -1) {
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
      log_event(LOG_WARNING, "IPC", "MQ_REMOVE_FAIL", strerror(errno));
    }
  }
}

void remove_shm(int shmid) {
  if (shmid != -1) {
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
      log_event(LOG_WARNING, "IPC", "SHM_REMOVE_FAIL", strerror(errno));
    }
  }
}

void close_sem(sem_t *sem) {
  if (sem != SEM_FAILED && sem != NULL) {
    sem_close(sem);
  }
}

void unlink_sem(const char *name) {
  // Apenas unlink se o nome for válido
  if (name)
    sem_unlink(name);
}
