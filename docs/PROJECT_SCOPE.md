# Portfolio Modernization Scope

## Objective

Turn the current academic prototype into a reproducible, technically accurate,
and publicly presentable Linux systems-programming project.

The project demonstrates process management, POSIX threads, System V IPC,
shared memory, named pipes, semaphores, scheduling, synchronization, graceful
shutdown, and runtime observability through a hospital simulation.

## Repository decisions

- This repository starts with a new Git history and has no inherited remote.
- Guilherme Lopes Pereira is identified as the author and maintainer.
- The previous academic report was removed from the portfolio repository.
- The original assignment PDF remains an external reference and is not
  redistributed by this repository.
- Generated binaries, object files, logs, results, and temporary files are not
  source artifacts and must remain outside version control.
- The project is distributed under the MIT License.

## Version 1.0 scope

The portfolio version will:

1. Implement the mandatory behavioral requirements that are currently missing.
2. Make IPC ownership, synchronization, and shutdown deterministic and safe.
3. Replace demonstration scripts with tests that assert observable behavior.
4. Provide reproducible builds and continuous integration on Linux.
5. Keep public documentation aligned with tested behavior.

## Out of scope for version 1.0

- A graphical user interface.
- Distributed execution across multiple machines.
- Durable database persistence.
- Publication of the original assignment statement.
- Recreating the removed academic report.

## Publication gates

The repository is ready for publication only when:

- a clean clone builds without warnings on supported Linux environments;
- automated tests pass and leave no processes or IPC resources behind;
- sanitizers and the selected Valgrind checks pass;
- every feature claimed in the README has a corresponding implementation and
  test;
- no generated output, private identifier, or unrelated academic artifact is
  tracked;
- the MIT license and copyright notice remain present.
