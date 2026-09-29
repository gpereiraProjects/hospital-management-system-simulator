# Hospital Management System Simulator

A Linux systems-programming project in C that models concurrent hospital
services. It began as an Operating Systems practical assignment and has been
rebuilt as a reproducible, assertion-tested portfolio project.

The code explores process creation, POSIX threads, System V message queues and
shared memory, POSIX semaphores, named pipes, signals, scheduling, and
synchronization.

> **Scope:** this is an educational concurrency simulator, not software for
> clinical use. Feature claims are tied to automated evidence in the
> [requirements traceability](docs/REQUIREMENTS_TRACEABILITY.md).

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

The full process, thread, ownership, and shutdown design is documented in the
[architecture guide](docs/ARCHITECTURE.md).

## Requirements

- Linux (Ubuntu 24.04 or a compatible distribution)
- GCC or Clang with C11 support
- GNU Make
- POSIX threads and realtime libraries
- Optional: ClangFormat and Valgrind

Windows users should build through WSL 2 or Docker. Detailed setup is available
in [the development guide](docs/DEVELOPMENT.md).

## Quick start

Build and start the simulator:

```bash
make release
./build/release/bin/hospital_system
```

In a second terminal, send the basic scenario:

```bash
cat tests/sample_commands/commands_basic.txt > input_pipe
```

Stop cleanly with `Ctrl+C`. Generated logs and result files are placed below
`logs/` and `results/`. See the [command reference](docs/COMMANDS.md) for every
accepted command and signal.

## Build profiles

Release build:

```bash
make release
```

Debug and sanitizer builds:

```bash
make debug
make sanitize
```

Build products are isolated by profile:

```text
build/
├── release/
├── debug/
└── sanitize/
```

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
make test_sanitize  # execute the main workflow under ASan and UBSan
make check_runtime  # run Memcheck, Helgrind, and DRD on a real workload
make ci             # run the complete local publication gate
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

## Continuous integration

The GitHub Actions workflow runs on every pull request and push to `main`. It
checks formatting and repository hygiene, builds release/debug profiles with
GCC and Clang, runs the complete functional and stress suite, executes ASan and
UBSan, performs the three Valgrind analyses, and builds the Ubuntu 24.04
container image.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Command reference](docs/COMMANDS.md)
- [Portfolio scope](docs/PROJECT_SCOPE.md)
- [Requirements traceability](docs/REQUIREMENTS_TRACEABILITY.md)
- [Development environment](docs/DEVELOPMENT.md)
- [Contributing](CONTRIBUTING.md)

## Author

Guilherme Lopes Pereira

## License

Distributed under the [MIT License](LICENSE).
