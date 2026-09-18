# Loom

A multithreaded Linux task-processing engine written in C, using POSIX threads,
mutexes, condition variables, and a read-write lock. It ships with two front
ends onto the same running engine: an interactive **CLI** and a **web
dashboard** served by an HTTP server embedded in the program itself.

Submit a task from either interface and watch a worker thread pick it up in
real time.

## Features

- Bounded, thread-safe producer-consumer **task queue** (mutex + two
  condition variables)
- A configurable **worker thread pool** that can be **resized live**, while
  running, without dropping in-flight work (via a poison-pill hand-off, not
  `pthread_cancel`)
- Three task types exercising different kinds of work: CPU-bound (`prime`),
  latency-bound (`sleep`), and real disk I/O (`fileio`)
- Thread-safe aggregate statistics behind a **read-write lock** (many
  readers, few writers)
- A shared append-only log file behind its own dedicated mutex
- An **embedded HTTP server** (raw POSIX sockets, no framework) serving a
  small JSON API and a single-page **live dashboard** (no external
  dependencies -- everything is inlined, so it works with no internet
  connection)
- **Graceful shutdown** on `Ctrl-C` or the `quit` command, using
  `pthread_sigmask` + `sigwait` rather than an async signal handler, so
  already-queued tasks finish before the process exits
- Zero third-party dependencies -- only the C standard library, POSIX
  threads, and POSIX sockets
- Verified race-free under concurrent CLI + web load with **ThreadSanitizer**
  (see [`docs/03-test-plan.md`](docs/03-test-plan.md) -- including a real
  bug it caught during development)

## Requirements

- Linux (uses POSIX sockets, `pthread`, `sigwait`)
- `gcc` with C11 support
- GNU Make

## Build

```bash
make            # normal optimized build -> ./loom
make tsan       # ThreadSanitizer build, for race-checking (see docs/03)
make clean      # remove build/, the binary, and loom.log
```

## Run

```bash
./loom                              # defaults: 4 workers, port 8080
./loom --workers 8 --port 9000      # override at startup
./loom --help                       # full flag reference
```

| Flag | Default | Meaning |
|---|---|---|
| `-w`, `--workers N` | 4 | worker threads to start with (1-64) |
| `-p`, `--port N` | 8080 | port for the dashboard / JSON API |
| `-q`, `--queue-size N` | 32 | bounded task queue capacity |
| `-d`, `--web-dir DIR` | `web` | directory containing `dashboard.html` |

Run it from the project root (so the default `web/` path resolves), or pass
`--web-dir` explicitly.

Once running, open **http://localhost:8080/** for the dashboard, or use the
CLI directly in the same terminal.

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
threads, the queue depth, aggregate stats, a small throughput sparkline, a
form to submit tasks, and a table of recent tasks.

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

## Project layout

```
loom/
├── include/        headers (one per module)
├── src/            implementation
│   ├── task_queue.c    bounded producer-consumer queue
│   ├── thread_pool.c   worker pool, task execution, resize
│   ├── stats.c          rwlock-protected aggregate stats
│   ├── history.c        mutex-protected recent-task ring buffer
│   ├── http_server.c    embedded HTTP server + JSON API
│   ├── cli.c             interactive command loop
│   └── main.c            wiring, arg parsing, signal handling
├── web/dashboard.html    the live dashboard (self-contained)
├── docs/                  requirements, architecture, test plan, user manual
├── tests/stress_test.sh   concurrency stress test (see docs/03)
└── Makefile
```

## Documentation

- [`docs/01-requirements.md`](docs/01-requirements.md) -- functional and
  non-functional requirements, scope
- [`docs/02-architecture.md`](docs/02-architecture.md) -- module breakdown,
  concurrency model, diagrams, design trade-offs
- [`docs/03-test-plan.md`](docs/03-test-plan.md) -- test cases, the
  ThreadSanitizer run (including a real race it caught), how to reproduce
- [`docs/04-user-manual.md`](docs/04-user-manual.md) -- walkthroughs for the
  CLI, the dashboard, and the API

## Testing for thread-safety

```bash
./tests/stress_test.sh          # normal build, functional + concurrency check
./tests/stress_test.sh --tsan   # rebuilds with ThreadSanitizer, fails on any race
```

Full details -- what the script does, and a real race it caught during
development -- are in [`docs/03-test-plan.md`](docs/03-test-plan.md).
