#include "../include/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int log_fd = -1;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static critical_log_shm_t *critical_buffer;
static int debug_mode;

static const char *severity_name(log_severity_t severity) {
  switch (severity) {
  case LOG_CRITICAL:
    return "CRITICAL";
  case LOG_ERROR:
    return "ERROR";
  case LOG_WARNING:
    return "WARNING";
  case LOG_INFO:
    return "INFO";
  case LOG_DEBUG:
    return "DEBUG";
  default:
    return "UNKNOWN";
  }
}

static int write_all(int descriptor, const char *text, size_t length) {
  while (length > 0) {
    ssize_t written = write(descriptor, text, length);
    if (written == -1 && errno == EINTR)
      continue;
    if (written <= 0)
      return -1;
    text += written;
    length -= (size_t)written;
  }
  return 0;
}

void log_init(const char *filename) {
  pthread_mutex_lock(&log_mutex);
  log_fd = open(filename, O_CREAT | O_WRONLY | O_APPEND, 0600);
  if (log_fd == -1) {
    perror("Nao foi possivel abrir o ficheiro de log");
  } else {
    char line[128];
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    int length = snprintf(line, sizeof(line), "--- SESSAO INICIADA: %s ---\n", timestamp);
    if (length > 0)
      write_all(log_fd, line, (size_t)length);
  }
#ifdef DEBUG
  debug_mode = 1;
#endif
  pthread_mutex_unlock(&log_mutex);
}

void log_bind_critical_buffer(critical_log_shm_t *buffer) { critical_buffer = buffer; }

void log_close(void) {
  pthread_mutex_lock(&log_mutex);
  if (log_fd != -1) {
    static const char closing[] = "--- SESSAO TERMINADA ---\n";
    write_all(log_fd, closing, sizeof(closing) - 1);
    close(log_fd);
    log_fd = -1;
  }
  pthread_mutex_unlock(&log_mutex);
}

static void store_critical_event(log_severity_t severity, const char *component,
                                 const char *event_type, const char *details, time_t now) {
  if (critical_buffer == NULL || severity > LOG_WARNING)
    return;
  pthread_mutex_lock(&critical_buffer->mutex);
  int index = critical_buffer->current_index;
  critical_event_t *event = &critical_buffer->events[index];
  event->timestamp = now;
  event->severity = severity;
  snprintf(event->component, sizeof(event->component), "%s", component);
  snprintf(event->event_type, sizeof(event->event_type), "%s", event_type);
  snprintf(event->description, sizeof(event->description), "%s", details);
  critical_buffer->current_index = (index + 1) % MAX_LOG_EVENTS;
  if (critical_buffer->event_count < MAX_LOG_EVENTS)
    critical_buffer->event_count++;
  pthread_mutex_unlock(&critical_buffer->mutex);
}

void log_event(log_severity_t severity, const char *component, const char *event_type,
               const char *details) {
  if (log_fd == -1 || (severity == LOG_DEBUG && !debug_mode))
    return;

  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);

  char line[768];
  int length = snprintf(line, sizeof(line), "[%s] [%s] [%s] [%s] %s\n", timestamp, component,
                        severity_name(severity), event_type, details);
  if (length < 0)
    return;
  size_t output_length = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;

  pthread_mutex_lock(&log_mutex);
  write_all(log_fd, line, output_length);
  if (severity <= LOG_ERROR)
    write_all(STDERR_FILENO, line, output_length);
  pthread_mutex_unlock(&log_mutex);
  store_critical_event(severity, component, event_type, details, now);
}
