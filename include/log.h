#ifndef LOG_H
#define LOG_H

#include <pthread.h>
#include <time.h>

// --- Níveis de Severidade (PDF Pag 23) ---
typedef enum {
  LOG_CRITICAL = 1, // Eventos críticos (transferências, falhas graves)
  LOG_ERROR = 2,    // Erros operacionais
  LOG_WARNING = 3,  // Avisos (stock baixo, esperas longas)
  LOG_INFO = 4,     // Eventos informativos normais
  LOG_DEBUG = 5     // Informação de debug
} log_severity_t;

// --- SHM5: Log Eventos Críticos (PDF Pag 18) ---
// Buffer circular para guardar os últimos eventos críticos em memória
typedef struct {
  time_t timestamp;
  char event_type[30];
  char component[20];
  char description[256];
  int severity;
} critical_event_t;

#define MAX_LOG_EVENTS 1000

typedef struct {
  critical_event_t events[MAX_LOG_EVENTS];
  int event_count;
  int current_index; // Para buffer circular
  pthread_mutex_t mutex;
} critical_log_shm_t;

// --- Protótipos ---
// Inicializa o sistema de logs (abre ficheiro, liga mutexes)
void log_init(const char *filename);

// Fecha o sistema de logs
void log_close(void);

// Regista um evento (Thread-safe) [PDF Pag 23]
void log_event(log_severity_t severity, const char *component, const char *event_type,
               const char *details);

#endif
