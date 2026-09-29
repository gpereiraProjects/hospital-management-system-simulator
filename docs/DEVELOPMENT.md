# Development Environment

The simulator targets Linux because it uses System V IPC, POSIX named
semaphores, `fork()`, Unix signals, and named pipes.

## Ubuntu or WSL 2

The container reference environment is Ubuntu 24.04. The build matrix has also
been verified locally on Ubuntu 26.04 under WSL 2 with GCC 15 and Clang 21.
Install the toolchain:

```bash
sudo apt-get update
sudo apt-get install --yes \
  build-essential \
  clang \
  clang-format \
  valgrind
```

From the repository root, build one or more profiles:

```bash
make release
make debug
make sanitize
```

The output of each profile is isolated below `build/<profile>/`. Header
dependencies are generated automatically as `.d` files alongside object files.

Override the compiler when needed:

```bash
make clean
make release CC=clang
```

Named pipes are not supported by every Windows-mounted filesystem in WSL. If
`mkfifo` reports `Operation not supported`, clone/copy the repository into the
native WSL filesystem (for example below `/home`) or run it through Docker. The
automated integration tests stage the executable in a native Linux temporary
directory for this reason.

## Docker

Docker provides a disposable Ubuntu 24.04 environment containing GCC, Clang,
ClangFormat, GNU Make, and Valgrind:

```bash
docker build --tag hospital-system .
docker run --rm --interactive --tty hospital-system
```

To use the image as an interactive development shell:

```bash
docker run --rm --interactive --tty \
  --volume "$(pwd):/workspace" \
  hospital-system bash
```

The image build runs the release compilation, so a successful `docker build`
also verifies that the checked-out source compiles in the reference environment.

## Build profiles

| Profile | Command | Purpose |
|---|---|---|
| Release | `make release` | Optimized build with assertions disabled |
| Debug | `make debug` | Symbols, no optimization, and `DEBUG` enabled |
| Sanitize | `make sanitize` | AddressSanitizer and UndefinedBehaviorSanitizer instrumentation |

## Formatting

The repository uses the checked-in `.clang-format` and `.editorconfig` files:

```bash
make format
make format-check
```

All C sources and headers are kept formatted; `make format-check` is part of the
pre-publication validation.

## Automated validation

Run the complete functional suite with:

```bash
make test
```

The tests stage the executable and configuration in a fresh native-Linux
temporary directory. They use bounded condition-based waits rather than fixed
long sleeps, assert logs, generated reports, exact known-workload statistics,
100-command completion, and cleanup after shutdown under load.

Run the dynamic-analysis workload with:

```bash
make check_runtime
```

This executes Memcheck, Helgrind, and DRD against the debug build and fails on
reported leaks, races, or synchronization errors. `tests/valgrind.supp` contains
only two documented tool limitations: a glibc TLS stack-cache report and DRD's
loss of named-semaphore identity when a child reopens a semaphore inherited
through `fork()`. Application stacks outside those narrow patterns remain
visible and fail the target.

## Runtime artifacts

The executable creates logs, results, IPC key material, and named pipes at
runtime. These files are ignored by Git.

```bash
make clean          # build products only
make clean-runtime  # project runtime files only
```

The legacy broad `ipc_clean` behavior is disabled. It must not be replaced by
commands that delete unrelated resources from `/dev/shm` or global System V IPC
tables.
