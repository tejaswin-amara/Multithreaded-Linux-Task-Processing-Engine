# Test Plan

## 1. Approach

Four layers, in order:

1. **Functional smoke test** -- build, start the server, drive it from both
   the CLI and `curl`, confirm expected output.
2. **Concurrency stress test** -- many producers (parallel `curl` requests
   *and* CLI `batch` commands) hitting the queue, stats, and log at once,
   plus live resizes, while the server is under load.
3. **Race detection** -- the same stress test, rebuilt with
   `make tsan` (ThreadSanitizer), checking the output for
   `WARNING: ThreadSanitizer`.
4. **Manual dashboard check** -- load `/` in a browser, submit a task from
   the form, confirm the worker grid, queue bar, stats, and table all
   update live.

Layers 1-3 are scripted and reproducible; commands are given per test case
below so they can be re-run after any change.

## 2. Functional test cases

| ID | Case | Steps | Expected | Result |
|---|---|---|---|---|
| TC-1 | Dashboard serves | `curl http://localhost:8080/` | `200`, HTML starting with `<!DOCTYPE html>` | **Pass** |
| TC-2 | Status endpoint shape | `curl .../api/status` on a fresh start | JSON with `submitted:0, completed:0`, `workers` array length equal to `--workers` | **Pass** |
| TC-3 | Submit via HTTP | `POST /api/submit {"type":"prime","param":200000}` | `200 {"id":1,"queued":true}`; task later appears `done` in `/api/tasks` with a correct prime count | **Pass** -- returned `found 17984 primes <= 200000` |
| TC-4 | Submit via CLI | `submit sleep 150`, `submit fileio 64` | Both printed as `queued task #N`, both later `done` | **Pass** |
| TC-5 | `batch` under load | `batch 6` while other tasks are in flight | All 6 accepted, unique sequential IDs, all eventually `done` | **Pass** |
| TC-6 | Resize up | `workers 6` while tasks are queued | `num_workers` becomes 6; new workers pick up queued work | **Pass** |
| TC-7 | Resize down | `workers 2` | `num_workers` becomes 2; the retiring workers finish their current task first, no crash | **Pass** |
| TC-8 | Graceful shutdown (`quit`) | submit several tasks, immediately `quit` | Pending tasks still complete; "final stats" line printed; process exits `0` | **Pass** -- `submitted 9 \| completed 9 \| failed 0`, exit code `0` |
| TC-9 | Log file matches history | compare `loom.log` tail against `/api/tasks` after a run | Same task IDs, same results | **Pass** |

## 3. Concurrency stress test

Packaged as [`tests/stress_test.sh`](../tests/stress_test.sh) (run it with
`--tsan` to rebuild under ThreadSanitizer first). It is the same script
used to find the bug in section 4:

```bash
( sleep 0.4; echo "batch 10"; sleep 0.2; echo "workers 8"; sleep 0.2
  echo "batch 10"; sleep 0.2; echo "workers 3"; sleep 1.5
  echo "status"; sleep 0.3; echo "quit"
) | timeout 30 ./loom -w 4 -p 8081 > run.out 2>&1 &
LOOM_PID=$!
sleep 0.3
for i in $(seq 1 15); do
  curl -s --max-time 3 -X POST http://localhost:8081/api/submit \
    -H 'Content-Type: application/json' \
    -d "{\"type\":\"prime\",\"param\":$((5000 + RANDOM % 20000))}" > /dev/null &
done
wait
wait $LOOM_PID
```

This fires 20 CLI-submitted tasks and 15 concurrent HTTP-submitted tasks
(35 total) at a pool that is resized twice while they are in flight, then
shuts down while some may still be queued.

| ID | Case | Expected | Result |
|---|---|---|---|
| TC-10 | No lost or duplicate task IDs under mixed concurrent producers | `submitted == completed + failed`, IDs 1..35 each appear exactly once | **Pass** -- `submitted 35 \| completed 35 \| failed 0` |
| TC-11 | Resize during active load doesn't crash or hang | Clean shutdown message, exit code `0` | **Pass** |

## 4. A real bug found by ThreadSanitizer

**Root cause.** Once `queue_push()` returns success, ownership of that
`task_t` has passed to whichever worker pops it next -- and a worker can
pop, finish, and `free()` a small task in well under a millisecond. Both
`submit_task()` (`src/cli.c`) and `handle_submit()` (`src/http_server.c`)
originally read `t->id` **after** the push had already succeeded, to build
their confirmation message. Under load, that read could land after the
worker had already freed the same memory.

ThreadSanitizer caught it immediately on the first stress run, reporting
both a data race and a heap-use-after-free, both rooted at the same line in
each file:

```
WARNING: ThreadSanitizer: heap-use-after-free
  Read of size 4 ... by main thread:
    #0 submit_task src/cli.c:36
  Previous write of size 8 ... by thread T1:
    #0 free ...
    #1 worker_main src/thread_pool.c:141
```

**Fix.** Capture the id into a local variable *before* the push, and use
the local afterward -- nothing may dereference `t` once a push has
succeeded:

```c
int id = t->id;                 /* capture first: t may be freed the
                                  * instant queue_push() returns */
if (queue_push(ctx->queue, t) != 0) {
    printf("... task #%d was not accepted\n", id);
    free(t);
    return;
}
printf("queued task #%d ...\n", id);
```

The same fix was applied in `handle_submit()`.

**Verification.** Re-ran the stress test in section 3 under `make tsan`:

```bash
make tsan
# ... same script as above ...
grep -c "WARNING: ThreadSanitizer" run.out
```

| ID | Case | Expected | Result |
|---|---|---|---|
| TC-12 | ThreadSanitizer, before fix | -- | **3 warnings** (1 unique race, reported from both sides) |
| TC-13 | ThreadSanitizer, after fix, same stress script | `0` warnings | **Pass** -- `0` warnings, `submitted 35 \| completed 35 \| failed 0`, exit `0` |

This is left in the test plan rather than quietly fixed, because it is a
good example of the kind of bug locks alone do not prevent: every access to
`t` before the fix *was* properly synchronized by the queue's own mutex --
the bug was about **which thread was allowed to touch the pointer at all**,
not about a missing lock.

## 5. Reproducing this test plan

```bash
make && ./loom -w 3 -p 8080     # functional tests (section 2): drive
                                 # manually via curl / the CLI

./tests/stress_test.sh          # concurrency stress test (section 3)
./tests/stress_test.sh --tsan   # + race detection (section 4); exits
                                 # non-zero if ThreadSanitizer reports
                                 # anything
```

## 6. Known limitations

- The stress test above is a fixed, hand-written scenario, not a fuzzer or
  a property-based test -- it increases confidence, it does not prove the
  absence of races in paths it doesn't exercise.
- ThreadSanitizer only reports races it actually observes at runtime; a
  timing-dependent bug on a path this script doesn't hit could still exist.
- There is no automated regression suite (no CI); this plan is meant to be
  re-run by hand after changes, particularly to `task_queue.c`,
  `thread_pool.c`, or anywhere a `task_t*` crosses a thread boundary.
