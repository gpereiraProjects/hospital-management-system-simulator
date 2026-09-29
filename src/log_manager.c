#include "../include/log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Variáveis Estáticas (Internas ao módulo)
static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int debug_mode = 0; // Pode ser ativado via flag de compilação ou config

void log_init(const char *filename) {
  pthread_mutex_lock(&log_mutex);

  log_file = fopen(filename, "a");
  if (!log_file) {
    perror("ERRO CRÍTICO: Não foi possível criar/abrir ficheiro de log");
    // Em caso de falha de log, o sistema deve alertar stderr
  } else {
    // Cabeçalho de sessão
    time_t now = time(NULL);
    char *date = ctime(&now);
    date[strlen(date) - 1] = '\0'; // Remove newline
    fprintf(log_file, "--- SESSÃO INICIADA: %s ---\n", date);
    fflush(log_file);
  }

#ifdef DEBUG
  debug_mode = 1;
#endif

  pthread_mutex_unlock(&log_mutex);
}

void log_close() {
  pthread_mutex_lock(&log_mutex);
  if (log_file) {
    fprintf(log_file, "--- SESSÃO TERMINADA ---\n");
    fclose(log_file);
    log_file = NULL;
  }
  pthread_mutex_unlock(&log_mutex);
}

// Implementação conforme especificação PDF Pag 23
void log_event(log_severity_t severity, const char *component,
               const char *event_type, const char *details) {

  // Se não estiver inicializado ou for DEBUG e debug_mode off, ignora
  if (!log_file || (severity == LOG_DEBUG && !debug_mode))
    return;

  // Obter Timestamp
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char time_str[20];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

  // Converter Enum para String
  const char *sev_str;
  switch (severity) {
  case LOG_CRITICAL:
    sev_str = "CRITICAL";
    break;
  case LOG_ERROR:
    sev_str = "ERROR";
    break;
  case LOG_WARNING:
    sev_str = "WARNING";
    break;
  case LOG_INFO:
    sev_str = "INFO";
    break;
  case LOG_DEBUG:
    sev_str = "DEBUG";
    break;
  default:
    sev_str = "UNKNOWN";
    break;
  }

  // --- SECÇÃO CRÍTICA (Escrita no Ficheiro) ---
  pthread_mutex_lock(&log_mutex);

  // Formato: [TIMESTAMP] [COMPONENT] [SEVERITY] [EVENT_TYPE] [DETAILS]
  fprintf(log_file, "[%s] [%s] [%s] [%s] %s\n", time_str, component, sev_str,
          event_type, details);

  // Forçar escrita no disco imediatamente (importante para debugging de
  // crashes)
  fflush(log_file);

  // Imprimir erros graves também na consola
  if (severity <= LOG_ERROR) {
    fprintf(stderr, "[%s] [%s] %s: %s\n", component, sev_str, event_type,
            details);
  }

  pthread_mutex_unlock(&log_mutex);
}