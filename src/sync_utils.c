#include "../include/log.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Wrapper seguro para Lock Mutex
void mutex_lock(pthread_mutex_t *mutex, const char *context) {
  if (mutex == NULL)
    return;
  int ret = pthread_mutex_lock(mutex);
  if (ret != 0) {
    char err_buff[256];
    snprintf(err_buff, sizeof(err_buff), "Mutex Lock Fail (%s): %s", context,
             strerror(ret));
    log_event(LOG_ERROR, "SYNC", "MUTEX_ERROR", err_buff);
    // Em erros graves de sync, muitas vezes é melhor abortar ou tentar
    // recuperar
  }
}

// Wrapper seguro para Unlock Mutex
void mutex_unlock(pthread_mutex_t *mutex, const char *context) {
  if (mutex == NULL)
    return;
  int ret = pthread_mutex_unlock(mutex);
  if (ret != 0) {
    char err_buff[256];
    snprintf(err_buff, sizeof(err_buff), "Mutex Unlock Fail (%s): %s", context,
             strerror(ret));
    log_event(LOG_ERROR, "SYNC", "MUTEX_ERROR", err_buff);
  }
}

// Wrapper para Condition Wait
void cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
               const char *context) {
  int ret = pthread_cond_wait(cond, mutex);
  if (ret != 0) {
    char err_buff[256];
    snprintf(err_buff, sizeof(err_buff), "Cond Wait Fail (%s): %s", context,
             strerror(ret));
    log_event(LOG_ERROR, "SYNC", "COND_ERROR", err_buff);
  }
}

// Wrapper para Condition Signal
void cond_signal(pthread_cond_t *cond, const char *context) {
  int ret = pthread_cond_signal(cond);
  if (ret != 0) {
    char err_buff[256];
    snprintf(err_buff, sizeof(err_buff), "Cond Signal Fail (%s): %s", context,
             strerror(ret));
    log_event(LOG_ERROR, "SYNC", "COND_ERROR", err_buff);
  }
}

// Wrapper para Condition Broadcast
void cond_broadcast(pthread_cond_t *cond, const char *context) {
  int ret = pthread_cond_broadcast(cond);
  if (ret != 0) {
    char err_buff[256];
    snprintf(err_buff, sizeof(err_buff), "Cond Broadcast Fail (%s): %s",
             context, strerror(ret));
    log_event(LOG_ERROR, "SYNC", "COND_ERROR", err_buff);
  }
}