#ifndef SYNC_H
#define SYNC_H

#include <pthread.h>

// Wrappers com tratamento de erros (Implementados em src/sync_utils.c)
void mutex_lock(pthread_mutex_t *mutex, const char *context);
void mutex_unlock(pthread_mutex_t *mutex, const char *context);
void cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
               const char *context);
void cond_signal(pthread_cond_t *cond, const char *context);
void cond_broadcast(pthread_cond_t *cond, const char *context);

#endif