#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/hospital.h"
#include "../include/ipc.h"
#include "../include/log.h"
#include "../include/stats.h"

// Protótipos
extern int triage_main(int argc, char *argv[]);
extern int surgery_main(int argc, char *argv[]);
extern int pharmacy_main(int argc, char *argv[]);
extern int laboratory_main(int argc, char *argv[]);
extern void stats_init_pointers(global_statistics_t *s, surgery_block_shm_t *b,
                                pharmacy_shm_t *p, lab_queue_shm_t *l);
extern void sigusr1_handler(int signum);
extern void sigusr2_handler(int signum);

// Nova função da thread de paciente
void start_patient_thread(const char *command);

// Globais
volatile sig_atomic_t g_shutdown = 0;
system_config_t config;
global_statistics_t *g_stats_ptr =
    NULL; // Ponteiro global para uso no patient_thread

// PIDs e IPCs
pid_t pid_triage = -1, pid_surgery = -1, pid_pharmacy = -1, pid_laboratory = -1;
int mq_urgent_id = -1, mq_normal_id = -1, mq_resp_id = -1;
int shm_stats_id = -1, shm_bo_id = -1, shm_pharm_id = -1, shm_lab_id = -1,
    shm_log_id = -1;

void cleanup_resources() {
  log_event(LOG_INFO, "SYSTEM", "CLEANUP", "A limpar recursos IPC...");
  if (mq_urgent_id != -1)
    msgctl(mq_urgent_id, IPC_RMID, NULL);
  if (mq_normal_id != -1)
    msgctl(mq_normal_id, IPC_RMID, NULL);
  if (mq_resp_id != -1)
    msgctl(mq_resp_id, IPC_RMID, NULL);
  if (shm_stats_id != -1)
    shmctl(shm_stats_id, IPC_RMID, NULL);
  if (shm_bo_id != -1)
    shmctl(shm_bo_id, IPC_RMID, NULL);
  if (shm_pharm_id != -1)
    shmctl(shm_pharm_id, IPC_RMID, NULL);
  if (shm_lab_id != -1)
    shmctl(shm_lab_id, IPC_RMID, NULL);
  if (shm_log_id != -1)
    shmctl(shm_log_id, IPC_RMID, NULL);

  unlink_sem(SEM_BO1_NAME);
  unlink_sem(SEM_BO2_NAME);
  unlink_sem(SEM_BO3_NAME);
  unlink_sem(SEM_TEAMS_NAME);
  unlink_sem(SEM_LAB1_NAME);
  unlink_sem(SEM_LAB2_NAME);
  unlink_sem(SEM_PHARM_NAME);
  unlink(PIPE_INPUT);
  log_close();
}

void graceful_shutdown() {
  log_event(LOG_INFO, "SYSTEM", "SHUTDOWN",
            "Iniciando encerramento gracioso...");

  // Matar filhos
  if (pid_triage > 0)
    kill(pid_triage, SIGTERM);
  if (pid_surgery > 0)
    kill(pid_surgery, SIGTERM);
  if (pid_pharmacy > 0)
    kill(pid_pharmacy, SIGTERM);
  if (pid_laboratory > 0)
    kill(pid_laboratory, SIGTERM);

  // Esperar por eles
  wait(NULL);
  wait(NULL);
  wait(NULL);
  wait(NULL);

  cleanup_resources();
  printf("\n[Main] Sistema encerrado com sucesso.\n");
  exit(0);
}

void sigint_handler(int signum) {
  (void)signum;
  g_shutdown = 1;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  // Criação segura de diretórios no início
  mkdir("logs", 0777);
  mkdir("results", 0777);
  mkdir("results/stats_snapshots", 0777);
  mkdir("results/lab_results", 0777);
  mkdir("results/pharmacy_deliveries", 0777);

  log_init("logs/hospital_log.txt");

  // Tenta carregar APENAS do sítio certo
  if (load_config("config/config.txt", &config) != 0) {
    perror("ERRO FATAL: Nao foi possivel ler 'config/config.txt'. Verifique se "
           "o ficheiro existe.");
    // Fecha o log se necessário
    log_close();
    exit(EXIT_FAILURE);
  }

  // Criar ficheiro para ftok se não existir
  FILE *ftok_f = fopen(IPC_CONFIG_FILE, "a");
  if (ftok_f)
    fclose(ftok_f);

  // --- Criação IPC ---
  mq_urgent_id = create_msg_queue(ftok(IPC_CONFIG_FILE, KEY_MQ_URGENT));
  mq_normal_id = create_msg_queue(ftok(IPC_CONFIG_FILE, KEY_MQ_NORMAL));
  mq_resp_id = create_msg_queue(ftok(IPC_CONFIG_FILE, KEY_MQ_RESP));

  shm_stats_id = create_shm(ftok(IPC_CONFIG_FILE, KEY_SHM_STATS),
                            sizeof(global_statistics_t));
  shm_bo_id = create_shm(ftok(IPC_CONFIG_FILE, KEY_SHM_BO),
                         sizeof(surgery_block_shm_t));
  shm_pharm_id =
      create_shm(ftok(IPC_CONFIG_FILE, KEY_SHM_PHARM), sizeof(pharmacy_shm_t));
  shm_lab_id =
      create_shm(ftok(IPC_CONFIG_FILE, KEY_SHM_LAB), sizeof(lab_queue_shm_t));
  shm_log_id = create_shm(ftok(IPC_CONFIG_FILE, KEY_SHM_LOG),
                          sizeof(critical_log_shm_t));

  create_sem(SEM_BO1_NAME, 1);
  create_sem(SEM_BO2_NAME, 1);
  create_sem(SEM_BO3_NAME, 1);
  create_sem(SEM_TEAMS_NAME, config.max_medical_teams);
  create_sem(SEM_LAB1_NAME, config.max_tests_lab1);
  create_sem(SEM_LAB2_NAME, config.max_tests_lab2);
  create_sem(SEM_PHARM_NAME, 4);

  mkfifo(PIPE_INPUT, 0666);

  // --- Attach SHM ---
  g_stats_ptr = (global_statistics_t *)shmat(shm_stats_id, NULL, 0);
  surgery_block_shm_t *p_bo = (surgery_block_shm_t *)shmat(shm_bo_id, NULL, 0);
  pharmacy_shm_t *p_pharm = (pharmacy_shm_t *)shmat(shm_pharm_id, NULL, 0);
  lab_queue_shm_t *p_lab = (lab_queue_shm_t *)shmat(shm_lab_id, NULL, 0);

  // Inicialização de pointers para stats
  if (g_stats_ptr != (void *)-1) {
    memset(g_stats_ptr, 0, sizeof(global_statistics_t));
    stats_init_pointers(g_stats_ptr, p_bo, p_pharm, p_lab);

    // Inicializar mutex de stats
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_stats_ptr->mutex, &attr);
  }

  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);

  // --- Forks ---
  pid_triage = fork();
  if (pid_triage == 0) {
    triage_main(argc, argv);
    exit(0);
  }

  pid_surgery = fork();
  if (pid_surgery == 0) {
    surgery_main(argc, argv);
    exit(0);
  }

  pid_pharmacy = fork();
  if (pid_pharmacy == 0) {
    pharmacy_main(argc, argv);
    exit(0);
  }

  pid_laboratory = fork();
  if (pid_laboratory == 0) {
    laboratory_main(argc, argv);
    exit(0);
  }

  log_event(LOG_INFO, "SYSTEM", "READY", "Processos iniciados.");
  printf("[Main] Sistema pronto. Pipe '%s' aberto.\n", PIPE_INPUT);

  int fd = open(PIPE_INPUT, O_RDWR | O_NONBLOCK);
  if (fd == -1) {
    perror("Erro pipe");
    graceful_shutdown();
  }

  char buffer[2048];
  while (!g_shutdown) {
    memset(buffer, 0, sizeof(buffer));
    int bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
      buffer[bytes] = '\0';
      char *saveptr;
      char *cmd = strtok_r(buffer, "\n", &saveptr);
      while (cmd != NULL) {
        if (strlen(cmd) > 3) { // Ignorar linhas vazias
          log_event(LOG_INFO, "INPUT", "CMD_RECV", cmd);
          printf("[Main] Recebido: %s\n", cmd);

          if (strncmp(cmd, "STATUS", 6) == 0) {
            raise(SIGUSR1);
          } else {
            // Delega para a thread de paciente
            start_patient_thread(cmd);
          }
        }
        cmd = strtok_r(NULL, "\n", &saveptr);
      }
    }
    usleep(100000); // 100ms sleep para não saturar CPU
  }

  close(fd);
  graceful_shutdown();
  return 0;
}