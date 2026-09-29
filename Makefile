CC ?= gcc
BUILD_TYPE ?= release

PROJECT := hospital_system
BUILD_ROOT := build
BUILD_DIR := $(BUILD_ROOT)/$(BUILD_TYPE)
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
TARGET := $(BIN_DIR)/$(PROJECT)

SOURCES := \
	src/main.c \
	src/triage.c \
	src/surgery.c \
	src/pharmacy.c \
	src/laboratory.c \
	src/patient_thread.c \
	src/ipc_utils.c \
	src/sync_utils.c \
	src/log_manager.c \
	src/stats_manager.c \
	src/config_parser.c \
	src/command_parser.c \
	src/time_simulation.c

OBJECTS := $(SOURCES:src/%.c=$(OBJ_DIR)/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

CPPFLAGS := -Iinclude -D_DEFAULT_SOURCE -MMD -MP
BASE_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread
BASE_LDFLAGS := -pthread
LDLIBS := -lrt

ifeq ($(BUILD_TYPE),release)
  PROFILE_CFLAGS := -O2 -DNDEBUG
else ifeq ($(BUILD_TYPE),debug)
  PROFILE_CFLAGS := -O0 -g3 -DDEBUG
else ifeq ($(BUILD_TYPE),sanitize)
  PROFILE_CFLAGS := -O1 -g3 -DDEBUG -fno-omit-frame-pointer -fsanitize=address,undefined
  PROFILE_LDFLAGS := -fsanitize=address,undefined
else
  $(error Unsupported BUILD_TYPE '$(BUILD_TYPE)'; use release, debug, or sanitize)
endif

CFLAGS := $(BASE_CFLAGS) $(PROFILE_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS := $(BASE_LDFLAGS) $(PROFILE_LDFLAGS) $(EXTRA_LDFLAGS)

.DEFAULT_GOAL := release

release:
	$(MAKE) BUILD_TYPE=release build

debug:
	$(MAKE) BUILD_TYPE=debug build

sanitize:
	$(MAKE) BUILD_TYPE=sanitize build

build: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $@

run: release
	./$(BUILD_ROOT)/release/bin/$(PROJECT)

test: test_parser test_phase2 test_phase3 test_scenarios

test_scenarios: test_basic test_concurrent test_stress test_shutdown

test_basic: release
	BINARY=$(BUILD_ROOT)/release/bin/$(PROJECT) bash ./tests/test_basic.sh

test_concurrent: release
	BINARY=$(BUILD_ROOT)/release/bin/$(PROJECT) bash ./tests/test_concurrent.sh

test_stress: release
	BINARY=$(BUILD_ROOT)/release/bin/$(PROJECT) bash ./tests/test_stress.sh

test_shutdown: release
	BINARY=$(BUILD_ROOT)/release/bin/$(PROJECT) bash ./tests/test_shutdown.sh

test_parser:
	@mkdir -p $(BUILD_ROOT)/tests
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/unit/test_command_parser.c \
		src/command_parser.c -o $(BUILD_ROOT)/tests/test_command_parser
	./$(BUILD_ROOT)/tests/test_command_parser

test_phase2: release test_parser
	BINARY=$(BUILD_ROOT)/release/bin/$(PROJECT) bash ./tests/test_phase2.sh

test_phase3: release test_parser
	BINARY=$(BUILD_ROOT)/release/bin/$(PROJECT) bash ./tests/test_phase3.sh

test_sanitize: sanitize
	BINARY=$(BUILD_ROOT)/sanitize/bin/$(PROJECT) bash ./tests/test_phase3.sh

test_repository:
	bash ./tests/test_repository.sh

ci: format-check test_repository test test_sanitize check_runtime

format:
	clang-format -i $(SOURCES) include/*.h

format-check:
	clang-format --dry-run --Werror $(SOURCES) include/*.h

check_memory: debug
	BINARY=$(BUILD_ROOT)/debug/bin/$(PROJECT) bash ./tests/test_instrumented.sh memcheck

check_threads: debug
	BINARY=$(BUILD_ROOT)/debug/bin/$(PROJECT) bash ./tests/test_instrumented.sh helgrind

check_deadlock: debug
	BINARY=$(BUILD_ROOT)/debug/bin/$(PROJECT) bash ./tests/test_instrumented.sh drd

check_runtime: check_memory check_threads check_deadlock

clean:
	rm -rf $(BUILD_ROOT)

clean-runtime:
	rm -f input_pipe triage_pipe surgery_pipe pharmacy_pipe lab_pipe
	rm -f logs/*.txt results/*.txt results/*/*.txt

ipc_clean:
	@echo "ipc_clean is disabled until project-scoped IPC cleanup is implemented safely."
	@exit 1

-include $(DEPENDENCIES)

.PHONY: release debug sanitize build run test test_scenarios test_basic test_concurrent \
	test_stress test_shutdown test_parser test_phase2 test_phase3 test_sanitize test_repository \
	format format-check check_memory check_threads check_deadlock check_runtime ci \
	clean clean-runtime ipc_clean
