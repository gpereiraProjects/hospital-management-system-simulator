#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <pthread.h>
#include <time.h>

// --- Constantes Globais ---
#define MAX_PATIENT_ID 16
#define MAX_MED_NAME 30
#define MAX_ROOMS 3
#define MAX_MED_TYPES 15
#define LAB_QUEUE_SIZE 50
#define MAX_TEAMS 2

// --- SHM2: Estruturas dos Blocos Operatórios ---
typedef struct {
  int room_id; // ID da sala (1, 2, 3)
  int status;  // 0=FREE, 1=OCCUPIED, 2=CLEANING
  char current_patient[MAX_PATIENT_ID];
  int surgery_start_time;
  int estimated_end_time;
  pthread_mutex_t mutex; // Proteção da sala específica
} surgery_room_t;

typedef struct {
  surgery_room_t rooms[MAX_ROOMS];
  int medical_teams_available; // Semáforo lógico ou contador
  pthread_mutex_t teams_mutex; // Proteção do contador de equipas
} surgery_block_shm_t;

// --- SHM3: Estruturas da Farmácia ---
typedef struct {
  char name[MAX_MED_NAME];
  int current_stock;
  int reserved;
  int threshold;         // Limiar mínimo para aviso
  int max_capacity;      // Capacidade máxima para reposição
  pthread_mutex_t mutex; // Proteção por medicamento
} medication_stock_t;

typedef struct {
  medication_stock_t medications[MAX_MED_TYPES];
  int total_active_requests;
  pthread_mutex_t global_mutex;
} pharmacy_shm_t;

// --- SHM4: Estruturas dos Laboratórios  ---
typedef struct {
  char request_id[20];
  char patient_id[MAX_PATIENT_ID];
  int test_type; // Código do teste (HEMO, GLIC, etc.)
  int priority;  // URGENT vs NORMAL
  int status;    // 0=pending, 1=processing, 2=done
  time_t request_time;
  time_t completion_time;
} lab_request_entry_t;

typedef struct {
  lab_request_entry_t queue_lab1[LAB_QUEUE_SIZE]; // Fila Circular LAB1
  lab_request_entry_t queue_lab2[LAB_QUEUE_SIZE]; // Fila Circular LAB2
  int lab1_count;
  int lab2_count;
  int lab1_available_slots; // Capacidade (max 2)
  int lab2_available_slots; // Capacidade (max 2)

  // Índices para gestão de fila circular (sugestão de implementação)
  int lab1_head, lab1_tail;
  int lab2_head, lab2_tail;

  pthread_mutex_t lab1_mutex;
  pthread_mutex_t lab2_mutex;
} lab_queue_shm_t;

// Função de Simulação de Tempo (Implementada em src/time_simulation.c)
void wait_time_units(int units);

#endif
