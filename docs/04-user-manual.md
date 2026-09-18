# User Manual

## 1. Installation Prerequisites

On Ubuntu 22.04 LTS or 24.04 LTS, ensure you have the required build tools:
```bash
sudo apt update
sudo apt install build-essential valgrind curl
```

## 2. Quick start

```bash
make
bin/task_engine
```

Then, in the same terminal, try:

```
submit prime 100000
status
```

...and open **http://localhost:8080/** in a browser to watch it happen live.

## 3. The CLI

Every command is typed at the `task_engine ready --` prompt and takes effect
immediately; nothing needs a restart.

| Command | What it does | Example |
|---|---|---|
| `submit prime <n>` | Queue a CPU-bound task that counts primes up to `n` | `submit prime 500000` |
| `submit sleep <ms>` | Queue a task that waits `ms` milliseconds (stands in for I/O latency) | `submit sleep 300` |
| `submit fileio <kb>` | Queue a task that writes `kb` kilobytes to a temp file, reads it back, and checksums it | `submit fileio 128` |
| `batch <n>` | Submit `n` random tasks at once -- the fastest way to load-test the queue | `batch 20` |
| `status` | Uptime, worker count, queue depth/capacity, aggregate stats, and each worker's current state | `status` |
| `tasks` | The most recently completed tasks, most recent first | `tasks` |
| `workers <n>` | Resize the pool to `n` threads while running | `workers 8` |
| `help` | List all commands | `help` |
| `quit` | Stop accepting new work from the CLI path, let already-queued tasks finish, then exit | `quit` |

`Ctrl-C` does the same clean shutdown as `quit` -- it does not abandon
in-flight work.

## 4. The web dashboard

Open the port `loom` printed at startup (default `http://localhost:8080/`).
Everything on the page refreshes once a second automatically.

- **Worker threads** -- one card per worker. A card lights up and briefly
  pulses the moment that worker picks up a task, showing its task ID; it
  goes back to "idle" the moment the task finishes.
- **Task queue** -- how many tasks are currently waiting versus the queue's
  capacity.
- **Throughput panel** -- submitted / completed / failed counts, the
  running average latency, and a small sparkline of completed tasks per
  second over roughly the last 30 seconds.
- **Submit a task** -- pick a type, set the parameter (its label changes to
  match: "Count up to" for `prime`, "Wait (ms)" for `sleep`, "Size (KB)" for
  `fileio`), and submit. A confirmation appears under the button; if the
  page can't reach `loom`, it says so there instead.
- **Recent tasks** -- the same data as the CLI's `tasks` command, updated
  live, with failures shown in a different color from completions.

The connection dot next to the title turns from "connecting" to "live" once
the first poll succeeds, and to "reconnecting" if the server becomes
unreachable (for example, while it's mid-restart).

## 5. The API

All four endpoints are plain HTTP/1.1, no authentication, JSON in and out.

### `GET /api/status`

```bash
curl http://localhost:8080/api/status
```

```json
{
  "submitted": 12, "completed": 11, "failed": 0,
  "avg_latency_ms": 84.2, "uptime_ms": 15302,
  "num_workers": 4, "queue_depth": 1, "queue_capacity": 32,
  "workers": [
    {"active": 1, "task_id": 12},
    {"active": 0, "task_id": -1},
    {"active": 0, "task_id": -1},
    {"active": 0, "task_id": -1}
  ]
}
```

### `GET /api/tasks`

```bash
curl http://localhost:8080/api/tasks
```

Returns up to the 50 most recently completed tasks, most recent first, each
shaped like:

```json
{"id": 12, "type": "prime", "status": "done", "param": 500000,
 "latency_ms": 84.2, "result": "found 41538 primes <= 500000"}
```

### `POST /api/submit`

```bash
curl -X POST http://localhost:8080/api/submit \
  -H 'Content-Type: application/json' \
  -d '{"type":"sleep","param":250}'
```

`type` must be `prime`, `sleep`, or `fileio`; anything else returns
`400` with `{"error": "..."}`. Successful submission returns
`200 {"id": N, "queued": true}`.

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `failed to start web server on port 8080 (already in use?)` | something else is already listening on that port | pick another with `-p`, or stop the other process |
| Dashboard 404s / "dashboard.html not found" | `loom` wasn't started from the project root | run it from the project root, or pass `--web-dir /path/to/web` |
| `workers <n>` says "resize failed" | `n` was `< 1` or `> 64` | pick a value in that range |
| CLI seems to hang after `submit ...` | it isn't -- the command returns immediately after queuing; check `status` to see the task run | -- |
| Dashboard shows "reconnecting…" | `loom` was stopped or is restarting | restart it; the page resumes polling on its own |
