CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -lrt -g -O2
LDFLAGS = -pthread -lrt
INCLUDES = -Iinclude

# Objetos (Inclui o patient_thread.o essencial)
OBJS = src/main.o src/triage.o src/surgery.o src/pharmacy.o \
       src/laboratory.o src/patient_thread.o src/ipc_utils.o \
       src/sync_utils.o src/log_manager.o src/stats_manager.o \
       src/config_parser.o src/time_simulation.o

# Target principal (Binário na pasta bin para organização)
TARGET = bin/hospital_system

# Regra principal
all: directories $(TARGET)

# Linkagem
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Compilação dos objetos
src/%.o: src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Criação da estrutura de diretórios necessária
directories:
	@mkdir -p bin logs results/stats_snapshots results/lab_results results/pharmacy_deliveries

# Debug build
debug: CFLAGS += -DDEBUG -O0 -ggdb3
debug: clean $(TARGET)

# Cleanup simples
clean:
	rm -f src/*.o $(TARGET)
	rm -f logs/*.txt
	rm -f results/*.txt
	rm -f results/*/*.txt

# Limpeza completa de recursos IPC (SHM, Semáforos, Pipes) 
ipc_clean:
	@echo "Removendo recursos IPC..."
	@rm -f input_pipe triage_pipe surgery_pipe pharmacy_pipe lab_pipe
	@rm -f /dev/shm/* 2>/dev/null || true
	@ipcs -q | awk '$$3 ~ /^[0-9]/ {print $$2}' | xargs -r ipcrm -q 2>/dev/null || true
	@ipcs -m | awk '$$3 ~ /^[0-9]/ {print $$2}' | xargs -r ipcrm -m 2>/dev/null || true
	@ipcs -s | awk '$$3 ~ /^[0-9]/ {print $$2}' | xargs -r ipcrm -s 2>/dev/null || true
	@echo "Recursos IPC removidos."

# Testes
test_basic: $(TARGET)
	./tests/test_basic.sh

test_concurrent: $(TARGET)
	./tests/test_concurrent.sh

test_stress: $(TARGET)
	./tests/test_stress.sh

test: test_basic test_concurrent test_stress

# Verificações com Valgrind 
check_memory: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

check_threads: $(TARGET)
	valgrind --tool=helgrind ./$(TARGET)

check_deadlock: $(TARGET)
	valgrind --tool=drd ./$(TARGET)

.PHONY: all clean debug test check_memory check_threads check_deadlock ipc_clean directories