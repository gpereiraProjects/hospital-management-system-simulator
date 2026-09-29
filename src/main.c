#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../include/command.h"
#include "../include/config.h"
#include "../include/hospital.h"
#include "../include/ipc.h"
#include "../include/log.h"
#include "../include/patient_thread.h"
#include "../include/stats.h"

extern int triage_main(int argc, char *argv[]);
extern int surgery_main(int argc, char *argv[]);
extern int pharmacy_main(int argc, char *argv[]);
extern int laboratory_main(int argc, char *argv[]);

static volatile sig_atomic_t shutdown_requested;
static volatile sig_atomic_t stats_requested;
static volatile sig_atomic_t snapshot_requested;
static volatile sig_atomic_t child_changed;

system_config_t config;
global_statistics_t *g_stats_ptr;

pid_t pid_triage = -1;
pid_t pid_surgery = -1;
pid_t pid_pharmacy = -1;
pid_t pid_laboratory = -1;
int mq_urgent_id = -1;
int mq_normal_id = -1;
int mq_resp_id = -1;
int shm_stats_id = -1;
int shm_bo_id = -1;
int shm_pharm_id = -1;
int shm_lab_id = -1;
int shm_log_id = -1;

static surgery_block_shm_t *bo_ptr;
static pharmacy_shm_t *pharmacy_ptr;
static lab_queue_shm_t *lab_ptr;
static critical_log_shm_t *critical_log_ptr;
static int instance_lock_fd = -1;
static sem_t *created_semaphores[7];

static void signal_handler(int signal_number) {
  if (signal_number == SIGUSR1)
    stats_requested = 1;
  else if (signal_number == SIGUSR2)
    snapshot_requested = 1;
  else if (signal_number == SIGCHLD)
    child_changed = 1;
  else
    shutdown_requested = 1;
}

static int install_signal_handlers(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, NULL) == -1 || sigaction(SIGTERM, &action, NULL) == -1 ||
      sigaction(SIGUSR1, &action, NULL) == -1 || sigaction(SIGUSR2, &action, NULL) == -1 ||
      sigaction(SIGCHLD, &action, NULL) == -1)
    return -1;
  return 0;
}

static int acquire_instance_lock(void) {
  instance_lock_fd = open("hospital_system.lock", O_CREAT | O_RDWR, 0600);
  if (instance_lock_fd == -1)
    return -1;
  if (flock(instance_lock_fd, LOCK_EX | LOCK_NB) == -1) {
    close(instance_lock_fd);
    instance_lock_fd = -1;
    errno = EBUSY;
    return -1;
  }
  return 0;
}

static int init_mutex(pthread_mutex_t *mutex, pthread_mutexattr_t *attributes) {
  int error = pthread_mutex_init(mutex, attributes);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}

static int initialize_shared_memory(void) {
  g_stats_ptr = shmat(shm_stats_id, NULL, 0);
  bo_ptr = shmat(shm_bo_id, NULL, 0);
  pharmacy_ptr = shmat(shm_pharm_id, NULL, 0);
  lab_ptr = shmat(shm_lab_id, NULL, 0);
  critical_log_ptr = shmat(shm_log_id, NULL, 0);
  if (g_stats_ptr == (void *)-1 || bo_ptr == (void *)-1 || pharmacy_ptr == (void *)-1 ||
      lab_ptr == (void *)-1 || critical_log_ptr == (void *)-1)
    return -1;

  memset(g_stats_ptr, 0, sizeof(*g_stats_ptr));
  memset(bo_ptr, 0, sizeof(*bo_ptr));
  memset(pharmacy_ptr, 0, sizeof(*pharmacy_ptr));
  memset(lab_ptr, 0, sizeof(*lab_ptr));
  memset(critical_log_ptr, 0, sizeof(*critical_log_ptr));

  pthread_mutexattr_t attributes;
  if (pthread_mutexattr_init(&attributes) != 0 ||
      pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED) != 0)
    return -1;

  if (init_mutex(&g_stats_ptr->mutex, &attributes) != 0 ||
      init_mutex(&bo_ptr->teams_mutex, &attributes) != 0 ||
      init_mutex(&pharmacy_ptr->global_mutex, &attributes) != 0 ||
      init_mutex(&lab_ptr->lab1_mutex, &attributes) != 0 ||
      init_mutex(&lab_ptr->lab2_mutex, &attributes) != 0 ||
      init_mutex(&critical_log_ptr->mutex, &attributes) != 0) {
    pthread_mutexattr_destroy(&attributes);
    return -1;
  }

  for (int i = 0; i < MAX_ROOMS; i++) {
    bo_ptr->rooms[i].room_id = i + 1;
    if (init_mutex(&bo_ptr->rooms[i].mutex, &attributes) != 0) {
      pthread_mutexattr_destroy(&attributes);
      return -1;
    }
  }
  bo_ptr->medical_teams_available = config.max_medical_teams;

  for (int i = 0; i < MAX_MED_TYPES; i++) {
    medication_stock_t *medication = &pharmacy_ptr->medications[i];
    snprintf(medication->name, sizeof(medication->name), "%s", config.med_configs[i].name);
    medication->current_stock = config.med_configs[i].initial_stock;
    medication->threshold = config.med_configs[i].threshold;
    medication->max_capacity = config.med_configs[i].initial_stock;
    if (init_mutex(&medication->mutex, &attributes) != 0) {
      pthread_mutexattr_destroy(&attributes);
      return -1;
    }
  }
  lab_ptr->lab1_available_slots = config.max_tests_lab1;
  lab_ptr->lab2_available_slots = config.max_tests_lab2;
  g_stats_ptr->system_start_time = time(NULL);
  pthread_mutexattr_destroy(&attributes);
  stats_init_pointers(g_stats_ptr, bo_ptr, pharmacy_ptr, lab_ptr);
  return 0;
}

static key_t project_key(int identifier) {
  key_t key = ftok(IPC_CONFIG_FILE, identifier);
  if (key == (key_t)-1)
    perror("ftok");
  return key;
}

static int create_ipc_resources(void) {
  if (cleanup_project_ipc() != 0)
    return -1;

  key_t keys[] = {project_key(KEY_MQ_URGENT), project_key(KEY_MQ_NORMAL),
                  project_key(KEY_MQ_RESP),   project_key(KEY_SHM_STATS),
                  project_key(KEY_SHM_BO),    project_key(KEY_SHM_PHARM),
                  project_key(KEY_SHM_LAB),   project_key(KEY_SHM_LOG)};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    if (keys[i] == (key_t)-1)
      return -1;

  mq_urgent_id = create_msg_queue(keys[0]);
  mq_normal_id = create_msg_queue(keys[1]);
  mq_resp_id = create_msg_queue(keys[2]);
  shm_stats_id = create_shm(keys[3], sizeof(global_statistics_t));
  shm_bo_id = create_shm(keys[4], sizeof(surgery_block_shm_t));
  shm_pharm_id = create_shm(keys[5], sizeof(pharmacy_shm_t));
  shm_lab_id = create_shm(keys[6], sizeof(lab_queue_shm_t));
  shm_log_id = create_shm(keys[7], sizeof(critical_log_shm_t));
  if (mq_urgent_id == -1 || mq_normal_id == -1 || mq_resp_id == -1 || shm_stats_id == -1 ||
      shm_bo_id == -1 || shm_pharm_id == -1 || shm_lab_id == -1 || shm_log_id == -1)
    return -1;

  const char *const names[] = {SEM_BO1_NAME,  SEM_BO2_NAME,  SEM_BO3_NAME,  SEM_TEAMS_NAME,
                               SEM_LAB1_NAME, SEM_LAB2_NAME, SEM_PHARM_NAME};
  int values[] = {1, 1, 1, config.max_medical_teams, config.max_tests_lab1, config.max_tests_lab2,
                  4};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    created_semaphores[i] = create_sem(names[i], values[i]);
    if (created_semaphores[i] == SEM_FAILED)
      return -1;
  }

  const char *const fifos[] = {PIPE_INPUT, PIPE_TRIAGE, PIPE_SURGERY, PIPE_PHARMACY, PIPE_LAB};
  for (size_t i = 0; i < sizeof(fifos) / sizeof(fifos[0]); i++)
    if (create_fifo(fifos[i]) != 0)
      return -1;
  return initialize_shared_memory();
}

static void detach_shared_memory(void) {
  if (g_stats_ptr != NULL && g_stats_ptr != (void *)-1)
    shmdt(g_stats_ptr);
  if (bo_ptr != NULL && bo_ptr != (void *)-1)
    shmdt(bo_ptr);
  if (pharmacy_ptr != NULL && pharmacy_ptr != (void *)-1)
    shmdt(pharmacy_ptr);
  if (lab_ptr != NULL && lab_ptr != (void *)-1)
    shmdt(lab_ptr);
  if (critical_log_ptr != NULL && critical_log_ptr != (void *)-1)
    shmdt(critical_log_ptr);
  g_stats_ptr = NULL;
  bo_ptr = NULL;
  pharmacy_ptr = NULL;
  lab_ptr = NULL;
  critical_log_ptr = NULL;
}

static void remove_message_queues(void) {
  remove_msg_queue(mq_urgent_id);
  remove_msg_queue(mq_normal_id);
  remove_msg_queue(mq_resp_id);
  mq_urgent_id = mq_normal_id = mq_resp_id = -1;
}

static void cleanup_resources(void) {
  remove_message_queues();
  detach_shared_memory();
  remove_shm(shm_stats_id);
  remove_shm(shm_bo_id);
  remove_shm(shm_pharm_id);
  remove_shm(shm_lab_id);
  remove_shm(shm_log_id);
  shm_stats_id = shm_bo_id = shm_pharm_id = shm_lab_id = shm_log_id = -1;
  for (size_t i = 0; i < sizeof(created_semaphores) / sizeof(created_semaphores[0]); i++)
    close_sem(created_semaphores[i]);
  cleanup_project_ipc();
  if (instance_lock_fd != -1) {
    flock(instance_lock_fd, LOCK_UN);
    close(instance_lock_fd);
    instance_lock_fd = -1;
    unlink("hospital_system.lock");
  }
}

static pid_t start_child(int (*entry)(int, char **), int argc, char **argv) {
  pid_t child = fork();
  if (child == 0)
    _exit(entry(argc, argv) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
  return child;
}

static void request_children_to_stop(void) {
  pid_t children[] = {pid_triage, pid_surgery, pid_pharmacy, pid_laboratory};
  for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++)
    if (children[i] > 0)
      kill(children[i], SIGTERM);
}

static void wait_for_child(pid_t child) {
  if (child <= 0)
    return;
  while (waitpid(child, NULL, 0) == -1 && errno == EINTR) {
  }
}

static void graceful_shutdown(void) {
  log_event(LOG_INFO, "SYSTEM", "SHUTDOWN", "A iniciar encerramento gracioso");
  wait_for_patient_threads();
  request_children_to_stop();
  remove_message_queues();
  wait_for_child(pid_triage);
  wait_for_child(pid_surgery);
  wait_for_child(pid_pharmacy);
  wait_for_child(pid_laboratory);
  cleanup_resources();
  log_event(LOG_INFO, "SYSTEM", "STOPPED", "Sistema encerrado com sucesso");
  log_close();
}

static void process_command_line(char *line) {
  size_t length = strlen(line);
  while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n'))
    line[--length] = '\0';

  command_t command;
  char error[160] = "";
  command_parse_result_t result = command_parse(line, &command, error, sizeof(error));
  if (result == COMMAND_PARSE_IGNORED)
    return;
  if (result == COMMAND_PARSE_ERROR) {
    log_event(LOG_WARNING, "INPUT", "CMD_REJECT", error);
    fprintf(stderr, "[Main] Comando rejeitado: %s\n", error);
    return;
  }

  log_event(LOG_INFO, "INPUT", "CMD_ACCEPT", command_kind_name(command.kind));
  if (command.kind == COMMAND_STATUS) {
    print_stats(stdout);
    return;
  }
  if (start_patient_thread(&command) != 0)
    log_event(LOG_ERROR, "INPUT", "THREAD_CREATE_FAIL", strerror(errno));
}

static void consume_input(int fd) {
  static char pending[4096];
  static size_t pending_length;
  char incoming[1024];
  ssize_t count = read(fd, incoming, sizeof(incoming));
  if (count <= 0)
    return;

  if (pending_length + (size_t)count > sizeof(pending)) {
    pending_length = 0;
    log_event(LOG_WARNING, "INPUT", "CMD_REJECT", "input buffer overflow");
    return;
  }
  memcpy(pending + pending_length, incoming, (size_t)count);
  pending_length += (size_t)count;

  size_t consumed = 0;
  for (;;) {
    char *newline = memchr(pending + consumed, '\n', pending_length - consumed);
    if (newline == NULL)
      break;
    size_t line_length = (size_t)(newline - (pending + consumed));
    if (line_length >= MAX_COMMAND_LENGTH) {
      log_event(LOG_WARNING, "INPUT", "CMD_REJECT", "command line too long");
    } else {
      char line[MAX_COMMAND_LENGTH];
      memcpy(line, pending + consumed, line_length);
      line[line_length] = '\0';
      process_command_line(line);
    }
    consumed += line_length + 1;
  }
  if (consumed > 0) {
    memmove(pending, pending + consumed, pending_length - consumed);
    pending_length -= consumed;
  }
  if (pending_length >= MAX_COMMAND_LENGTH) {
    pending_length = 0;
    log_event(LOG_WARNING, "INPUT", "CMD_REJECT", "unterminated command line too long");
  }
}

static void handle_deferred_events(void) {
  if (stats_requested) {
    stats_requested = 0;
    print_stats(stdout);
  }
  if (snapshot_requested) {
    snapshot_requested = 0;
    char path[128];
    if (save_stats_snapshot(path, sizeof(path)) == 0)
      printf("[Main] Snapshot criado: %s\n", path);
    else
      log_event(LOG_ERROR, "STATS", "SNAPSHOT_FAIL", strerror(errno));
  }
  if (child_changed && !shutdown_requested) {
    child_changed = 0;
    int status;
    pid_t child;
    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
      if (child == pid_triage)
        pid_triage = -1;
      else if (child == pid_surgery)
        pid_surgery = -1;
      else if (child == pid_pharmacy)
        pid_pharmacy = -1;
      else if (child == pid_laboratory)
        pid_laboratory = -1;
      log_event(LOG_ERROR, "SYSTEM", "CHILD_EXIT", "Um processo terminou inesperadamente");
      shutdown_requested = 1;
    }
  }
}

int main(int argc, char *argv[]) {
  umask(0077);
  if ((mkdir("logs", 0700) == -1 && errno != EEXIST) ||
      (mkdir("results", 0700) == -1 && errno != EEXIST) ||
      (mkdir("results/stats_snapshots", 0700) == -1 && errno != EEXIST) ||
      (mkdir("results/lab_results", 0700) == -1 && errno != EEXIST) ||
      (mkdir("results/pharmacy_deliveries", 0700) == -1 && errno != EEXIST)) {
    perror("mkdir");
    return EXIT_FAILURE;
  }

  log_init("logs/hospital_log.txt");
  if (load_config(IPC_CONFIG_FILE, &config) != 0) {
    log_close();
    return EXIT_FAILURE;
  }
  if (acquire_instance_lock() != 0) {
    fprintf(stderr, "Ja existe uma instancia ativa neste diretorio.\n");
    log_close();
    return EXIT_FAILURE;
  }
  if (install_signal_handlers() != 0 || create_ipc_resources() != 0) {
    perror("Falha ao inicializar o sistema");
    cleanup_resources();
    log_close();
    return EXIT_FAILURE;
  }

  pid_triage = start_child(triage_main, argc, argv);
  pid_surgery = start_child(surgery_main, argc, argv);
  pid_pharmacy = start_child(pharmacy_main, argc, argv);
  pid_laboratory = start_child(laboratory_main, argc, argv);
  if (pid_triage < 0 || pid_surgery < 0 || pid_pharmacy < 0 || pid_laboratory < 0) {
    perror("fork");
    shutdown_requested = 1;
  }

  int input_fd = open(PIPE_INPUT, O_RDWR | O_NONBLOCK);
  if (input_fd == -1) {
    perror("open input pipe");
    shutdown_requested = 1;
  } else {
    log_event(LOG_INFO, "SYSTEM", "READY", "Processos e IPC iniciados");
    printf("[Main] Sistema pronto. Pipe '%s' aberto.\n", PIPE_INPUT);
  }

  while (!shutdown_requested) {
    struct pollfd input = {.fd = input_fd, .events = POLLIN, .revents = 0};
    int ready = poll(&input, 1, 250);
    if (ready > 0 && (input.revents & POLLIN) != 0)
      consume_input(input_fd);
    else if (ready == -1 && errno != EINTR) {
      log_event(LOG_ERROR, "INPUT", "POLL_FAIL", strerror(errno));
      shutdown_requested = 1;
    }
    handle_deferred_events();
  }

  if (input_fd != -1)
    close(input_fd);
  graceful_shutdown();
  printf("[Main] Sistema encerrado com sucesso.\n");
  return EXIT_SUCCESS;
}
