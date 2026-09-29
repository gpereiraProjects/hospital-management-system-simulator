# Hospital Management System Simulator

A Linux systems-programming project in C that models concurrent hospital
services. It was created from an Operating Systems practical assignment and is
currently being hardened into a reproducible portfolio project.

The code explores process creation, POSIX threads, System V message queues and
shared memory, POSIX semaphores, named pipes, signals, scheduling, and
synchronization.

> **Project status:** active modernization. The current source is an academic
> prototype, not a production hospital system. Features are documented as
> complete only after they have an automated verification.

## Current architecture

The central process creates four specialized child processes:

- triage;
- surgery;
- pharmacy;
- laboratory.

Requests enter through `input_pipe` and are routed through System V message
queues. Shared memory stores global statistics and the structures intended for
room, pharmacy, laboratory, and critical-log state.

The simulator now executes end-to-end triage, surgery, pharmacy, and laboratory
flows. Requests retain their validated typed fields across IPC; stock and
restocking are shared and synchronized; laboratory and pharmacy outputs contain
the processed items; and surgery/triage wait for correlated dependencies. See
[requirements traceability](docs/REQUIREMENTS_TRACEABILITY.md) for the few
remaining partial requirements and their verification targets.

## Requirements

- Linux (Ubuntu 24.04 or a compatible distribution)
- GCC or Clang with C11 support
- GNU Make
- POSIX threads and realtime libraries
- Optional: ClangFormat and Valgrind

Windows users should build through WSL 2 or Docker. Detailed setup is available
in [the development guide](docs/DEVELOPMENT.md).

## Build

Release build:

```bash
make release
```

Debug build:

```bash
make debug
```

AddressSanitizer and UndefinedBehaviorSanitizer build:

```bash
make sanitize
```

Build products are isolated by profile:

```text
build/
├── release/
├── debug/
└── sanitize/
```

Run the release build from the repository root:

```bash
make run
```

In another terminal, send a sample workload:

```bash
cat tests/sample_commands/commands_basic.txt > input_pipe
```

Stop the simulator with `Ctrl+C`.

## Docker build

Build the reproducible Linux environment and compile the release profile:

```bash
docker build -t hospital-system .
```

The image build itself runs `make release`. Interactive execution can be
started with:

```bash
docker run --rm -it hospital-system
```

## Developer commands

```bash
make format         # format C sources and headers
make format-check   # verify formatting without modifying files
make test           # run the reliable automated suite
make test_parser    # run the typed-command parser unit suite
make test_phase2    # run parser, FIFO, signal, lock, and cleanup assertions
make test_phase3    # run end-to-end clinical, stock, lab, and surgery workflows
make test_scenarios # run basic, concurrent, 100-command, and shutdown scenarios
make check_runtime  # run Memcheck, Helgrind, and DRD on a real workload
make clean          # remove build products only
make clean-runtime  # remove this project's generated runtime files
```

Runtime cleanup is instance-specific and only resolves this application's known
keys and names. The old standalone broad cleanup command remains disabled so it
can never remove `/dev/shm` or System V resources belonging to other programs.

Every automated scenario is assertion-based and time-bounded. Tests run from
isolated native-Linux temporary directories, verify generated files and exact
statistics, and clean up their processes, pipes, locks, and IPC resources. The
stress scenario sends exactly 100 validated commands and proves that every
request is accepted and completed. Dynamic-analysis targets exercise a real
multi-component workload under Memcheck, Helgrind, and DRD.

## Documentation

- [Portfolio scope](docs/PROJECT_SCOPE.md)
- [Requirements traceability](docs/REQUIREMENTS_TRACEABILITY.md)
- [Development environment](docs/DEVELOPMENT.md)

## Author

Guilherme Lopes Pereira

## License

Distributed under the [MIT License](LICENSE).
