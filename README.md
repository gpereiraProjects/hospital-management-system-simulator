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

The implementation already demonstrates the process hierarchy, message queues,
shared statistics, resource semaphores, triage workers, and conditional waiting
for surgery dependencies. Several assignment requirements remain partial or
missing; see [requirements traceability](docs/REQUIREMENTS_TRACEABILITY.md) for
the audited status and verification target of each requirement.

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
make clean          # remove build products only
make clean-runtime  # remove this project's generated runtime files
```

The old broad IPC cleanup command is deliberately disabled until cleanup is
made instance-specific. Do not remove global `/dev/shm` or System V resources
belonging to other applications.

The existing shell scenarios are retained as a baseline, but are not yet a
reliable automated test suite. Assertion-based tests are tracked in the
[implementation roadmap](docs/REQUIREMENTS_TRACEABILITY.md).

## Documentation

- [Portfolio scope](docs/PROJECT_SCOPE.md)
- [Requirements traceability](docs/REQUIREMENTS_TRACEABILITY.md)
- [Development environment](docs/DEVELOPMENT.md)

## Author

Guilherme Lopes Pereira

## License

Distributed under the [MIT License](LICENSE).
