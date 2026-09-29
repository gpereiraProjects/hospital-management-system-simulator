# Requirements Traceability

Source: *Sistema Integrado de Gestao Hospitalar - Trabalho Pratico de Sistemas
Operativos, 2024/2025*.

The assignment document is an external reference. Its examples are treated as
requirements and context, never as instructions to execute.

Status values:

- **Implemented** - present in the source and substantially matches the requirement.
- **Partial** - some structure exists, but behavior or validation is incomplete.
- **Missing** - no effective implementation was found.
- **Unverified** - a claim exists, but the current evidence cannot validate it.

## Architecture and IPC

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| ARC-01 | One central process and four specialized child processes | Implemented | `main.c` creates triage, surgery, pharmacy, and laboratory children | Integration test verifies PIDs and clean exit |
| ARC-02 | Required thread model in every component | Partial | Required triage monitors, pharmacy restock/statistics workers, and per-lab managers are absent | Runtime thread-role assertions and scenario tests |
| IPC-01 | Three System V message queues | Implemented | Urgent, normal, and response queues are created | IPC lifecycle integration test |
| IPC-02 | Five System V shared-memory segments with usable state | Partial | Five segments are created; pharmacy, laboratory, and critical-log state are not functionally integrated | State transition tests for every segment |
| IPC-03 | Five named pipes | Missing | Only `input_pipe` is created | Startup test checks all required FIFOs, or a documented architecture change narrows the requirement |
| IPC-04 | Seven named POSIX semaphores | Partial | Seven are created, but stale-instance handling and error rollback are unsafe | Repeated-start and failed-start tests |
| IPC-05 | Shared-memory mutexes initialized for inter-process use | Missing | Only the statistics mutex explicitly uses `PTHREAD_PROCESS_SHARED` | Cross-process synchronization tests |
| IPC-06 | Cleanup affects only resources owned by this application | Missing | Current `ipc_clean` can remove unrelated `/dev/shm` and IPC resources | Isolation test with unrelated sentinel resources |

## Commands and time model

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| CMD-01 | Parse and validate all documented command formats | Missing | Parsing identifies little beyond command type and ID | Table-driven parser unit tests |
| CMD-02 | Support all seven command families and component-specific `STATUS` | Partial | `RESTOCK` and component-specific status are absent; other commands are partly interpreted | Positive and negative tests per command |
| CMD-03 | Honor `init` and `scheduled` simulation times | Missing | Requests are dispatched immediately and scheduled time is ignored | Deterministic simulated-clock tests |
| CMD-04 | Preserve commands across fragmented FIFO reads | Missing | Each `read()` buffer is tokenized independently | Fragmented-write integration test |
| CMD-05 | Reject malformed or unknown commands safely | Missing | Unknown commands can reach `msgsnd` with message type zero | Invalid-command corpus with error assertions |

## Triage

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| TRI-01 | Separate emergency and appointment scheduling rules | Partial | One shared array and a simplified selector are used | Ordering tests for both classes |
| TRI-02 | Emergency ordering by critical state, triage level, then arrival | Partial | Triage extraction reads the first colon-delimited value, often `init`, rather than `triage` | Deterministic priority test |
| TRI-03 | Appointment ordering by scheduled time | Missing | Scheduled time is not parsed or used | Appointment-order test |
| TRI-04 | Stability decreases and triggers critical/transfer states | Missing | Stability is not stored or monitored | Escalation and transfer tests |
| TRI-05 | Coordinate requested analyses and medication | Missing | Triage does not dispatch or await these dependencies | End-to-end emergency workflow test |
| TRI-06 | Capacity, rejection, timing, and completion statistics | Partial | Capacity and basic counts exist; timing and complete statistics do not | Capacity and metrics tests |

## Surgery

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| SUR-01 | Dedicated CARDIO, ORTHO, and NEURO rooms | Implemented | Surgery type selects one of three room resources | Three-room integration test |
| SUR-02 | PREOP and requested medication complete before surgery | Partial | PREOP and one hard-coded medicine are requested; command contents are ignored | Dependency and requested-item tests |
| SUR-03 | Scheduled time, urgency, and configured capacity affect scheduling | Missing | These fields are not enforced and capacity is hard-coded to ten | Scheduling and saturation tests |
| SUR-04 | Medical-team count and room ownership are synchronized | Partial | Semaphores exist; initialization, cancellation, and cleanup need hardening | Contention and shutdown tests |
| SUR-05 | Configured random surgery and cleanup durations | Partial | Surgery uses ranges; cleanup configuration is not parsed | Seeded duration-boundary tests |

## Pharmacy

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| PHA-01 | Maintain stock and reservations for 15 medicines | Missing | Shared structures/config exist but processing does not use them | Stock-invariant tests |
| PHA-02 | Prioritize URGENT, HIGH, and NORMAL requests | Partial | Urgent and normal workers exist; HIGH semantics are absent | Mixed-priority ordering test |
| PHA-03 | Wait without busy waiting when stock is insufficient | Missing | Availability is not checked | Stock-exhaustion condition-variable test |
| PHA-04 | Automatic and manual restocking | Missing | Config values are not parsed and `RESTOCK` is unsupported | Threshold and manual-restock tests |
| PHA-05 | Detailed delivery receipt | Missing | Current receipt only records an ID and `ENTREGUE` | Receipt golden-file test |

## Laboratory

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| LAB-01 | Support HEMO, GLIC, COLEST, RENAL, HEPAT, and PREOP | Partial | HEMO and PREOP are distinguished; other values collapse to generic | Per-test routing suite |
| LAB-02 | Process urgent and normal requests | Missing | Laboratory consumes only the urgent message queue | Normal/urgent integration test |
| LAB-03 | Enforce two concurrent slots per laboratory | Partial | Semaphores exist; routing and statistics are incomplete | Peak-concurrency test |
| LAB-04 | PREOP runs LAB1 then LAB2 and notifies its requester | Partial | Sequential use exists; response target and output are simplified | PREOP state-sequence test |
| LAB-05 | Detailed result files and turnaround statistics | Missing | Generated result is a two-line placeholder | Golden-file and timing tests |

## Configuration, logging, statistics, and signals

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| SYS-01 | Parse and validate every documented configuration value | Partial | Cleanup and restock values are not parsed; cross-field validation is absent | Complete config unit suite |
| SYS-02 | Thread-safe and coherent multi-process logging | Partial | Each process inherits a process-private mutex and stream state | Concurrent integrity test or single-writer logger |
| SYS-03 | Critical-event shared-memory ring buffer | Missing | Segment and type exist but no events are written | Ring-buffer wraparound test |
| SYS-04 | Accurate real-time and snapshot statistics | Partial | Several counters/timers are never updated and a total is labelled as an average | Known-workload metrics test |
| SYS-05 | Safe `SIGINT`, `SIGUSR1`, `SIGUSR2`, and `SIGCHLD` | Partial | `SIGCHLD` is absent and statistics handlers call non-async-signal-safe operations | Signal integration suite |
| SYS-06 | Graceful shutdown waits for work, reaps children, and removes owned IPC | Partial | Children are cancelled and generic `wait()` calls are used; detached work is not coordinated | Shutdown-under-load test |
| SYS-07 | Check all system-call, allocation, and pthread results | Partial | Many critical return values are ignored | Static checklist and fault-injection tests |

## Tests and delivery quality

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| TST-01 | All seven mandatory scenarios | Partial | Three scripts exist, but required scenarios and assertions are incomplete | Automated scenario suite |
| TST-02 | Tests fail when expected behavior is absent | Missing | Scripts mostly sleep, signal, and print completion | Exit-code and assertion review |
| TST-03 | Zero leaks, detected races, and deadlocks | Unverified | No reproducible reports correspond to the current source | Sanitizer and Valgrind jobs tied to a commit |
| TST-04 | Reproducible clean-checkout build | Unverified | Existing binary predates source files; the audit WSL lacks a compiler | Clean GCC/Clang CI builds |

## Implementation order

1. Safe build, IPC ownership, initialization, parser, and signal model.
2. Deterministic time model and complete triage behavior.
3. Pharmacy stock/restocking and laboratory routing/types.
4. Surgery scheduling, dependency correlation, and capacity handling.
5. Accurate logging/statistics and output formats.
6. Assertion-based tests, sanitizers, Valgrind, and CI.
7. Public README, architecture documentation, license, and release.
