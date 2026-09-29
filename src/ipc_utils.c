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
  // IPC_CREAT | 0666 permite leitura/escrita para todos
  int id = msgget(key, IPC_CREAT | 0666);
  if (id == -1) {
    log_event(LOG_ERROR, "IPC", "MQ_CREATE_FAIL", strerror(errno));
    perror("Erro msgget");
    return -1;
  }
  return id;
}

// Criação de memória partilhada
int create_shm(key_t key, size_t size) {
  int id = shmget(key, size, IPC_CREAT | 0666);
  if (id == -1) {
    log_event(LOG_ERROR, "IPC", "SHM_CREATE_FAIL", strerror(errno));
    perror("Erro shmget");
    return -1;
  }
  return id;
}

// Criação de semáforos POSIX nomeados
sem_t *create_sem(const char *name, int initial_value) {
  // O_CREAT cria se não existir. O_EXCL falharia se já existisse (não queremos
  // isso aqui para restart fácil)
  sem_t *sem = sem_open(name, O_CREAT, 0644, initial_value);
  if (sem == SEM_FAILED) {
    log_event(LOG_ERROR, "IPC", "SEM_CREATE_FAIL", strerror(errno));
    perror("Erro sem_open");
    return SEM_FAILED;
  }
  return sem;
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