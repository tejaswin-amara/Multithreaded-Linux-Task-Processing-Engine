# Requirements Specification

## 1. Purpose

Loom demonstrates core POSIX concurrency primitives -- threads, mutexes,
condition variables, and a read-write lock -- inside one realistic system: a
task-processing engine with two independent, concurrently-used front ends
(a CLI and a web dashboard) driving the same shared state.

## 2. Scope

The system accepts small units of work ("tasks") of a few predefined types,
queues them, executes them across a pool of worker threads, and reports live
status through both a terminal interface and a browser dashboard. The
worker pool can be resized while running.

## 3. Functional requirements

| ID | Requirement |
|---|---|
| FR-1 | The system shall provide a thread-safe, bounded FIFO task queue shared by every producer and consumer. |
| FR-2 | The system shall support at least three task types, covering CPU-bound, latency-bound, and disk-I/O-bound work. |
| FR-3 | The system shall run a configurable pool of POSIX worker threads that concurrently pull tasks from the queue and execute them. |
| FR-4 | The system shall allow the worker pool to be resized at runtime, growing or shrinking, without restarting the process or discarding in-flight work. |
| FR-5 | The system shall provide an interactive command-line interface for submitting tasks, checking status, listing recent tasks, and resizing the pool. |
| FR-6 | The system shall provide an HTTP API and a browser dashboard that reflect live queue depth, worker state, and aggregate statistics. |
| FR-7 | The system shall accept task submissions from the CLI and the web dashboard at the same time without corrupting shared state or issuing duplicate task IDs. |
| FR-8 | The system shall track aggregate statistics (submitted, completed, failed, average latency) and a history of recently completed tasks. |
| FR-9 | The system shall shut down gracefully on `SIGINT`, `SIGTERM`, or the CLI `quit` command: already-queued tasks finish before the process exits. |
| FR-10 | The system shall append a line to a shared log file for every completed task. |

## 4. Non-functional requirements

| Category | Requirement |
|---|---|
| Concurrency correctness | No data races or use-after-free under concurrent multi-producer, multi-consumer load, verified with ThreadSanitizer. |
| Portability | Builds and runs on any mainstream Linux distribution with glibc; no non-POSIX dependencies. |
| Dependency footprint | No third-party libraries -- only the C standard library, POSIX threads, and POSIX sockets. |
| Responsiveness | The dashboard reflects worker and queue state within one polling interval (1 second). |
| Resource bounds | Queue capacity and worker count are both configurable and capped, so load cannot grow memory or thread count without bound. |
| Maintainability | Every concurrency-sensitive resource (queue, stats, history, log) is isolated behind its own lock and its own module, so no two locks are ever held at once (see `docs/02-architecture.md`). |

## 5. Assumptions and constraints

- Single machine, single process -- not a distributed system.
- Linux target: relies on POSIX sockets, `pthread`, and `sigwait`.
- The HTTP server has no authentication or TLS; it is meant for
  localhost or a trusted local network, not the open internet.
- State (queue, stats, history) lives in memory only and resets on restart.
- The queue depth and per-task metrics are process-local; there is no
  cross-machine or cross-process coordination.

## 6. Out of scope

- Distributed or multi-machine task processing.
- Authentication, TLS, or rate limiting on the HTTP server.
- Task priorities, cancellation of an individual queued task, or
  dependencies between tasks.
- Persistent storage of task history across restarts (the log file is
  append-only and human-readable, but it is not re-parsed on startup).
