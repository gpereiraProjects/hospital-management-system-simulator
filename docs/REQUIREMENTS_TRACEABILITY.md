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
| ARC-01 | One central process and four specialized child processes | Implemented | `main.c` creates triage, surgery, pharmacy, and laboratory children | `test_shutdown.sh` verifies that captured children are reaped on clean exit |
| ARC-02 | Required thread model in every component | Partial | Triage, pharmacy, and laboratory roles are explicit; surgery uses a central priority scheduler plus operation threads rather than three long-lived room managers | Runtime thread-role assertions and architecture rationale |
| IPC-01 | Three System V message queues | Implemented | Urgent, normal, and response queues are created | IPC lifecycle integration test |
| IPC-02 | Five System V shared-memory segments with usable state | Implemented | Statistics, rooms, stock, laboratory state, and the critical-event ring buffer are initialized and used | Functional integration and status assertions |
| IPC-03 | Five named pipes | Implemented | All five project FIFOs are created privately and removed on shutdown | Startup and shutdown checks in `test_phase2.sh` |
| IPC-04 | Seven named POSIX semaphores | Implemented | Exclusive creation, stale-resource recovery under an instance lock, checked failures, and rollback are in place | Repeated-start check in `test_phase2.sh` |
| IPC-05 | Shared-memory mutexes initialized for inter-process use | Implemented | Statistics, rooms, teams, medicines, laboratories, and critical-log mutexes use `PTHREAD_PROCESS_SHARED` | Startup succeeds with every shared structure initialized |
| IPC-06 | Cleanup affects only resources owned by this application | Implemented | Cleanup resolves only this project's `ftok` keys, semaphore names, and FIFO paths while holding an instance lock | Exact-resource cleanup and second-instance checks |

## Commands and time model

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| CMD-01 | Parse and validate all documented command formats | Implemented | A typed parser validates identifiers, ranges, enums, lists, medicines, tests, and cross-field rules | Table-driven tests in `tests/unit/test_command_parser.c` |
| CMD-02 | Support all seven command families and component-specific `STATUS` | Implemented | All families execute; manual restock and filtered status are covered by integration tests | `test_phase3.sh` |
| CMD-03 | Honor `init` and `scheduled` simulation times | Implemented | A simulation clock delays dispatch until `init`; appointments and surgeries wait for `scheduled` | Fast deterministic runtime configuration in `test_phase3.sh` |
| CMD-04 | Preserve commands across fragmented FIFO reads | Implemented | A persistent bounded input buffer retains incomplete lines across reads | Fragmented-write check in `test_phase2.sh` |
| CMD-05 | Reject malformed or unknown commands safely | Implemented | Invalid commands are logged and never enter a message queue | Parser corpus and integration assertion |

## Triage

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| TRI-01 | Separate emergency and appointment scheduling rules | Implemented | Dedicated queue managers feed emergency-priority and scheduled-appointment selection | Ordering tests remain desirable |
| TRI-02 | Emergency ordering by critical state, triage level, then arrival | Implemented | Selection compares critical state, triage level, and simulated arrival time | Deterministic priority test remains desirable |
| TRI-03 | Appointment ordering by scheduled time | Implemented | Doctors select only due appointments, ordered by scheduled time | Functional integration |
| TRI-04 | Stability decreases and triggers critical/transfer states | Implemented | A monitor decrements waiting emergencies and records critical transitions/transfers | Transfer timing test remains desirable |
| TRI-05 | Coordinate requested analyses and medication | Implemented | Typed requests are dispatched and treatment waits on correlated laboratory/pharmacy responses | Emergency integration in `test_phase3.sh` |
| TRI-06 | Capacity, rejection, timing, and completion statistics | Implemented | Per-class limits, duplicate rejection, wait totals, and completion counters are maintained | Exact known-workload counters in `test_basic.sh` and `test_stress.sh` |

## Surgery

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| SUR-01 | Dedicated CARDIO, ORTHO, and NEURO rooms | Implemented | Surgery type selects one of three room resources | Three-room integration test |
| SUR-02 | PREOP and requested medication complete before surgery | Implemented | The validated command's complete test/medicine lists are sent and both correlated responses are required | Surgery integration in `test_phase3.sh` |
| SUR-03 | Scheduled time, urgency, and configured capacity affect scheduling | Implemented | Ready operations are ordered per room by HIGH/MEDIUM/LOW, scheduled time, and stable slot order; configured capacity is enforced | Mixed-urgency order asserted in `test_phase3.sh` |
| SUR-04 | Medical-team count and room ownership are synchronized | Implemented | Room/team semaphores, shared room mutexes, and joinable operation threads protect lifecycle and ownership | Contention test remains desirable |
| SUR-05 | Configured random surgery and cleanup durations | Implemented | Surgery and cleanup ranges are both loaded from configuration and used | Seeded duration-boundary tests |

## Pharmacy

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| PHA-01 | Maintain stock and reservations for 15 medicines | Implemented | Multi-item requests atomically validate, reserve, consume, and release shared stock state | Depletion integration in `test_phase3.sh` |
| PHA-02 | Prioritize URGENT, HIGH, and NORMAL requests | Implemented | Distinct message types let the priority worker drain URGENT before HIGH, while NORMAL has its own worker/queue | Mixed-priority saturation test remains desirable |
| PHA-03 | Wait without busy waiting when stock is insufficient | Implemented | Workers use a condition variable with timed shutdown checks and wake on restock | Stock-exhaustion integration |
| PHA-04 | Automatic and manual restocking | Implemented | Manual aliases mutate canonical stock; the monitor automatically replenishes values below threshold | Both paths asserted in `test_phase3.sh` |
| PHA-05 | Detailed delivery receipt | Implemented | Receipts include request, destination, priority, quantities, resulting stock, duration, and state | Content assertions in `test_phase3.sh` |

## Laboratory

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| LAB-01 | Support HEMO, GLIC, COLEST, RENAL, HEPAT, and PREOP | Implemented | Every test is routed to its specified laboratory; incompatible direct requests are rejected | Parser and functional integration tests |
| LAB-02 | Process urgent and normal requests | Implemented | Dedicated managers consume both queues without stealing other component messages | Normal/urgent integration in `test_phase3.sh` |
| LAB-03 | Enforce two concurrent slots per laboratory | Implemented | Per-lab semaphores enforce configured capacity and shared state exposes active/available counts | Peak-concurrency test remains desirable |
| LAB-04 | PREOP runs LAB1 then LAB2 and notifies its requester | Implemented | PREOP acquires LAB1 then LAB2 and sends a destination-specific correlated response | Surgery integration in `test_phase3.sh` |
| LAB-05 | Detailed result files and turnaround statistics | Implemented | Reports contain every test, representative results, routing, priority, duration, and validation state | Content assertions in `test_phase3.sh` |

## Configuration, logging, statistics, and signals

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| SYS-01 | Parse and validate every documented configuration value | Implemented | Strict integer parsing, required medicine count, booleans, positive capacities/durations, min/max relations, and stock thresholds are validated | Invalid configuration startup assertion in `test_phase3.sh` |
| SYS-02 | Thread-safe and coherent multi-process logging | Implemented | Each record is emitted as one append-only write; local mutexes serialize threads without inherited buffered streams | Concurrent and 100-command tests assert complete, parseable event counts |
| SYS-03 | Critical-event shared-memory ring buffer | Implemented | Warning/error/critical events are stored in the process-shared circular buffer | Wraparound test remains desirable |
| SYS-04 | Accurate real-time and snapshot statistics | Partial | Several counters/timers are never updated and a total is labelled as an average | Known-workload metrics test |
| SYS-05 | Safe `SIGINT`, `SIGUSR1`, `SIGUSR2`, and `SIGCHLD` | Implemented | Handlers only set atomic flags; reporting, snapshots, child reaping, and cleanup run in normal control flow | Signal checks in `test_phase2.sh` |
| SYS-06 | Graceful shutdown waits for work, reaps children, and removes owned IPC | Partial | `test_shutdown.sh` proves bounded exit, child reaping, final snapshot, and FIFO/lock cleanup under active work; requests awaiting dependencies may still be aborted by the defined shutdown sequence | Define and test drain-versus-cancel semantics for every accepted request |
| SYS-07 | Check all system-call, allocation, and pthread results | Partial | Many critical return values are ignored | Static checklist and fault-injection tests |

## Tests and delivery quality

| ID | Requirement | Status | Evidence / gap | Target verification |
|---|---|---|---|---|
| TST-01 | All seven mandatory scenarios | Implemented | Parser, safety, stock depletion/restock, transfer, priorities, dependencies, outputs, concurrency, exact statistics, shutdown under load, and a deterministic 100-command workload are asserted | Preserve in CI |
| TST-02 | Tests fail when expected behavior is absent | Implemented | Parser, Phase 2, and Phase 3 suites use bounded waits and content/state assertions | Preserve in CI |
| TST-03 | Zero leaks, detected races, and deadlocks | Implemented | Sanitizers pass; `test_instrumented.sh` runs a real workload under Memcheck, Helgrind, and DRD. Signal state is C11-atomic. Narrow suppressions document glibc TLS and DRD post-fork named-semaphore modelling limitations | Preserve all analyzers in CI |
| TST-04 | Reproducible clean-checkout build | Implemented | Release, debug, and sanitizer profiles build without warnings using GCC 15 and Clang 21 on Ubuntu/WSL; Dockerfile targets Ubuntu 24.04 | Preserve the matrix in continuous integration |

## Remaining implementation order

1. Complete utilization/throughput statistics and define drain-versus-cancel shutdown semantics.
2. Add the existing functional and dynamic-analysis targets to continuous integration.
3. Finish public architecture documentation and prepare the first release.
