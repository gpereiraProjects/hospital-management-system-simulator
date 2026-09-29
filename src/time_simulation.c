#include "../include/config.h"
#include "../include/stats.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// Variável externa do config (carregada no main)
extern system_config_t config; // Assume-se que o processo tem acesso ao config global ou carregado

// Função para dormir N unidades de tempo
void wait_time_units(int units) {
  if (units <= 0)
    return;

  // TIME_UNIT_MS vem do config (ex: 500ms)
  int ms = config.time_unit_ms;
  if (ms <= 0)
    ms = 500; // Valor default de segurança

  useconds_t usec = (useconds_t)units * ms * 1000;
  usleep(usec);
}

// Atualiza o tempo global (chamado periodicamente pelo Main ou thread dedicada)
void update_simulation_time(global_statistics_t *stats, int units_passed) {
  if (stats) {
    pthread_mutex_lock(&stats->mutex);
    stats->simulation_time_units += units_passed;
    pthread_mutex_unlock(&stats->mutex);
  }
}

// Retorna timestamp formatado para logs
void get_current_time_str(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}