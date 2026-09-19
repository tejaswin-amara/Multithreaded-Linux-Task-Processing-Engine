# Loom — Multithreaded Linux Task Processing Engine

A multithreaded task-processing engine written in C for Linux, built around
POSIX threads, mutexes, condition variables, and a read-write lock. It ships
with two front ends onto the same running engine — an interactive **CLI**
and a **web dashboard** — plus a systemd deployment path and an
operational runbook for running it as a real service.

Submit a task from the CLI or the browser and watch a worker thread pick it
up in real time.

![C11](https://img.shields.io/badge/C-11-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen.svg)
![Thread safety](https://img.shields.io/badge/thread--safety-ThreadSanitizer%20verified-success.svg)
![Version](https://img.shields.io/badge/version-1.0.0-informational.svg)

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Build](#build)
- [Run](#run)
- [CLI reference](#cli-reference)
- [Web dashboard & API](#web-dashboard--api)
- [Testing](#testing)
- [Deployment](#deployment)
- [Project layout](#project-layout)
- [Documentation](#documentation)

## Features

**Core engine**
- Bounded, thread-safe producer-consumer **task queue** (mutex + two
  monotonic-clock condition variables), with blocking, non-blocking
  (`queue_try_push`), and timed (`queue_push_timeout`) push variants
- A configurable **worker thread pool**, **resizable live** while running,
  without dropping in-flight work — grown by spawning threads, shrunk with
  a poison-pill hand-off rather than `pthread_cancel`
- Three task types exercising different kinds of work: CPU-bound (`prime`),
  latency-bound (`sleep`), and real disk I/O (`fileio`)
- Thread-safe aggregate statistics behind a **read-write lock** (many
  readers, few writers), plus a mutex-protected ring buffer of recent tasks
- **Graceful shutdown** on `Ctrl-C`, `SIGTERM`, or the `quit` command, via
  `pthread_sigmask` + `sigwait` rather than an async signal handler, so
  already-queued tasks finish before the process exits

**Hardening**
- `safe_malloc` / `safe_calloc` wrappers: fail fast with a clear message on
  out-of-memory instead of risking a null-pointer dereference later
- `loom_log`: a thread-safe, leveled logger (`INFO` / `WARN` / `ERROR`)
  stamping every line with a millisecond timestamp and the emitting
  thread's ID
- Zero third-party dependencies — only the C standard library, POSIX
  threads, and POSIX sockets

**Interfaces**
- An **embedded HTTP server** (raw POSIX sockets, no framework) serving a
  small JSON API and a single-page **live dashboard** with no external
  asset dependencies, so it works with no internet connection
- An interactive CLI for submitting tasks, checking status, and resizing
  the pool from the same terminal `task_engine` is running in

**Verification**
- A standalone **unit test suite** (`bin/test_suite`) covering
  high-contention queue dispatch, queue capacity/timeout boundaries, and
  history ring-buffer rollover
- A **concurrency stress test** (`tests/stress_test.sh`) driving the queue
  from the CLI and concurrent HTTP submissions at once
- Verified race-free under that load with **ThreadSanitizer**, including a
  real use-after-free it caught during development — see
  [`docs/03-test-plan.md`](docs/03-test-plan.md)
- `AddressSanitizer` + `UndefinedBehaviorSanitizer` and `valgrind` build
  targets for memory correctness on top of the thread-safety checks

## Requirements

- Ubuntu Linux 22.04 LTS or 24.04 LTS (relies on POSIX sockets, `pthread`,
  `sigwait`, and `CLOCK_MONOTONIC`; other modern Linux distributions with
  glibc should also work)
- `gcc` with C11 support
- GNU Make
- `valgrind`, if you plan to use `make valgrind`

## Build

```bash
make            # optimized build (-O3 -Werror) -> bin/task_engine
make debug      # debug build: -g3 -O0 -DDEBUG, plus the test binary
make asan       # AddressSanitizer + UndefinedBehaviorSanitizer build
make tsan       # ThreadSanitizer build, for race-checking
make test       # build and run the unit test suite (bin/test_suite)
make valgrind   # run the unit test suite under valgrind's full leak check
make dist       # stripped release tarball + sha256 checksum
make clean      # remove bin/, obj/, and loom.log
```

`asan`, `tsan`, and `debug` also build and run `bin/test_suite` when test
sources are present, so a single target catches both correctness and
memory/thread-safety regressions in the same run.

## Run

```bash
bin/task_engine                              # defaults: 4 workers, port 8080
bin/task_engine --workers 8 --port 9000      # override at startup
bin/task_engine --help                       # full flag reference
```

| Flag | Default | Meaning |
|---|---|---|
| `-w`, `--workers N` | 4 | worker threads to start with (1-64) |
| `-p`, `--port N` | 8080 | port for the dashboard / JSON API |
| `-q`, `--queue-size N` | 32 | bounded task queue capacity |
| `-d`, `--web-dir DIR` | `web` | directory containing `dashboard.html` |

Run it from the project root (so the default `web/` path resolves), or pass
`--web-dir` explicitly. Once running, open **http://localhost:8080/** for
the dashboard, or use the CLI directly in the same terminal.

## CLI reference

| Command | Description |
|---|---|
| `submit prime <n>` | queue a CPU-bound task: count primes up to `n` |
| `submit sleep <ms>` | queue a task that waits `ms` milliseconds |
| `submit fileio <kb>` | queue a task that writes and reads back `kb` kilobytes on disk |
| `batch <n>` | submit `n` random tasks at once (stress test) |
| `status` | queue depth, worker states, aggregate stats |
| `tasks` | the most recently completed tasks |
| `workers <n>` | resize the pool to `n` threads, live |
| `help` | show the command list |
| `quit` | drain the queue, shut down cleanly, exit |

## Web dashboard & API

The dashboard polls the JSON API once a second: a live grid of worker
threads, the queue depth, aggregate stats, a throughput sparkline, a form
to submit tasks, and a table of recent tasks.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | the dashboard page |
| `GET` | `/api/status` | queue depth/capacity, per-worker state, aggregate stats |
| `GET` | `/api/tasks` | the last 50 completed tasks |
| `POST` | `/api/submit` | body `{"type":"prime\|sleep\|fileio","param":N}` |

```bash
curl -X POST localhost:8080/api/submit \
  -H 'Content-Type: application/json' \
  -d '{"type":"prime","param":100000}'
```

## Testing

| Layer | Command | What it checks |
|---|---|---|
| Unit tests | `make test` | queue correctness under 4 producers × 4 consumers × 10k tasks, `queue_try_push`/`queue_push_timeout` boundaries, history rollover |
| Concurrency stress | `tests/stress_test.sh` | mixed CLI + concurrent HTTP submissions, live pool resizes, clean shutdown |
| Race detection | `tests/stress_test.sh --tsan` (or `make tsan`) | the same load, instrumented with ThreadSanitizer |
| Memory correctness | `make asan` · `make valgrind` | out-of-bounds access, undefined behavior, and leaks |

See [`docs/03-test-plan.md`](docs/03-test-plan.md) for full test cases and
a real ThreadSanitizer-caught bug (a use-after-free from reading a task's
id right after handing it to the queue) with root cause, fix, and
re-verification.

## Deployment

A `systemd` unit (`task_engine.service`) is provided for running the
engine as a sandboxed, non-root service (`ProtectSystem=strict`,
`NoNewPrivileges=true`, a dedicated `taskengine` user, an open-file limit
for the socket-per-connection HTTP server). Step-by-step setup, and triage
steps for queue saturation, socket exhaustion, and suspected deadlocks, are
in the runbooks:

- [`docs/runbooks/deployment.md`](docs/runbooks/deployment.md)
- [`docs/runbooks/incident-response.md`](docs/runbooks/incident-response.md)

`make dist` produces a stripped, checksummed release tarball
(`task_engine-v1.0.0-linux-amd64.tar.gz`) containing the binary, the
dashboard, the README, and the service file.

## Project layout

```
.
├── include/                headers (one per module)
├── src/
│   ├── task_queue.c             bounded producer-consumer queue
│   ├── thread_pool.c            worker pool, task execution, live resize
│   ├── stats.c                   rwlock-protected aggregate stats
│   ├── history.c                 mutex-protected recent-task ring buffer
│   ├── http_server.c             embedded HTTP server + JSON API
│   ├── cli.c                      interactive command loop
│   ├── common.c                   safe_malloc/safe_calloc, loom_log, helpers
│   └── main.c                     wiring, arg parsing, signal handling
├── tests/
│   ├── test_suite.c               unit tests (`make test`)
│   └── stress_test.sh             concurrency stress test
├── web/dashboard.html        the live dashboard (self-contained)
├── docs/
│   ├── 01-requirements.md
│   ├── 02-architecture.md
│   ├── 03-test-plan.md
│   ├── 04-user-manual.md
│   └── runbooks/
│       ├── deployment.md
│       └── incident-response.md
├── task_engine.service        systemd unit for production deployment
├── CHANGELOG.md
└── Makefile
```

## Documentation

| Doc | Covers |
|---|---|
| [`docs/01-requirements.md`](docs/01-requirements.md) | functional and non-functional requirements, scope |
| [`docs/02-architecture.md`](docs/02-architecture.md) | module breakdown, concurrency model, diagrams, design trade-offs |
| [`docs/03-test-plan.md`](docs/03-test-plan.md) | test cases, the ThreadSanitizer run, how to reproduce |
| [`docs/04-user-manual.md`](docs/04-user-manual.md) | walkthroughs for the CLI, the dashboard, and the API |
| [`docs/runbooks/deployment.md`](docs/runbooks/deployment.md) | systemd install steps |
| [`docs/runbooks/incident-response.md`](docs/runbooks/incident-response.md) | triage for saturation, socket exhaustion, deadlocks |
| [`CHANGELOG.md`](CHANGELOG.md) | release history |
