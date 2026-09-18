#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "common.h"

static void print_help(void) {
    printf(
        "  submit prime <n>    queue a CPU-bound task: count primes up to n\n"
        "  submit sleep <ms>   queue an I/O-latency-style task that waits ms milliseconds\n"
        "  submit fileio <kb>  queue a task that writes and reads back kb kilobytes on disk\n"
        "  batch <n>           submit n random tasks at once (stress test the queue)\n"
        "  status              show queue depth, worker states, and aggregate stats\n"
        "  tasks               list the most recently completed tasks\n"
        "  workers <n>         resize the worker pool to n threads\n"
        "  help                show this message\n"
        "  quit                drain the queue, shut down cleanly, and exit\n");
}

static void submit_task(cli_ctx_t *ctx, task_type_t type, long param) {
    task_t *t = calloc(1, sizeof(task_t));
    pthread_mutex_lock(ctx->id_lock);
    t->id = (*ctx->next_task_id)++;
    pthread_mutex_unlock(ctx->id_lock);
    t->type = type;
    t->param = param;
    t->status = TASK_PENDING;
    t->enqueue_ms = now_ms();
    stats_task_submitted(ctx->stats);

    /* Capture the id before handing t to the queue: as soon as queue_push()
     * returns success, a worker thread can pop, finish, and free(t) before
     * this function's next line runs -- so nothing may read *t after a
     * successful push. (Caught by a ThreadSanitizer run; see docs/03.) */
    int id = t->id;
    if (queue_push(ctx->queue, t) != 0) {
        printf("queue is shutting down; task #%d was not accepted\n", id);
        free(t);
        return;
    }
    printf("queued task #%d (%s, param=%ld)\n", id, task_type_name(type), param);
}

static void cmd_status(cli_ctx_t *ctx) {
    stats_snapshot_t snap;
    stats_snapshot(ctx->stats, &snap, now_ms());
    printf("uptime %.1fs | workers %d | queue depth %d/%d\n",
           snap.uptime_ms / 1000.0, snap.num_workers,
           queue_size(ctx->queue), queue_capacity(ctx->queue));
    printf("submitted %ld | completed %ld | failed %ld | avg latency %.1fms\n",
           snap.submitted, snap.completed, snap.failed, snap.avg_latency_ms);
    for (int i = 0; i < snap.num_workers && i < LOOM_MAX_WORKERS; i++) {
        if (snap.workers[i].active)
            printf("  worker %2d: busy on task #%d\n", i, snap.workers[i].current_task_id);
        else
            printf("  worker %2d: idle\n", i);
    }
}

static void cmd_tasks(cli_ctx_t *ctx) {
    history_entry_t entries[LOOM_HISTORY_CAP];
    int n = history_snapshot(ctx->history, entries, LOOM_HISTORY_CAP);
    if (n == 0) { printf("no completed tasks yet\n"); return; }
    for (int i = 0; i < n; i++) {
        printf("  #%-4d %-7s %-6s %7.1fms  %s\n",
               entries[i].id, task_type_name(entries[i].type),
               task_status_name(entries[i].status), entries[i].latency_ms, entries[i].result);
    }
}

static void cmd_batch(cli_ctx_t *ctx, int n) {
    if (n < 1) n = 1;
    for (int i = 0; i < n; i++) {
        int pick = rand() % 3;
        if (pick == 0) submit_task(ctx, TASK_PRIME, 20000 + rand() % 60000);
        else if (pick == 1) submit_task(ctx, TASK_SLEEP, 50 + rand() % 300);
        else submit_task(ctx, TASK_FILEIO, 16 + rand() % 128);
    }
}

void cli_run(cli_ctx_t *ctx) {
    printf("loom ready -- %d worker(s), dashboard at http://localhost:%d/\n",
           ctx->stats->num_workers, ctx->port);
    printf("type 'help' for commands\n");

    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        char cmd[32] = {0};
        sscanf(line, "%31s", cmd);
        if (cmd[0] == '\0') continue;

        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "submit") == 0) {
            char sub[16] = {0};
            long param = 0;
            sscanf(line, "%*s %15s %ld", sub, &param);
            if (strcmp(sub, "prime") == 0) submit_task(ctx, TASK_PRIME, param > 0 ? param : 10000);
            else if (strcmp(sub, "sleep") == 0) submit_task(ctx, TASK_SLEEP, param > 0 ? param : 100);
            else if (strcmp(sub, "fileio") == 0) submit_task(ctx, TASK_FILEIO, param > 0 ? param : 64);
            else printf("unknown task type '%s' -- use prime, sleep, or fileio\n", sub);
        } else if (strcmp(cmd, "batch") == 0) {
            int n = 5;
            sscanf(line, "%*s %d", &n);
            cmd_batch(ctx, n);
        } else if (strcmp(cmd, "status") == 0) {
            cmd_status(ctx);
        } else if (strcmp(cmd, "tasks") == 0) {
            cmd_tasks(ctx);
        } else if (strcmp(cmd, "workers") == 0) {
            int n = 0;
            sscanf(line, "%*s %d", &n);
            if (n < 1) printf("usage: workers <n>\n");
            else if (pool_resize(ctx->pool, n) == 0) printf("resizing pool to %d worker(s)\n", n);
            else printf("resize failed -- n must be between 1 and %d\n", LOOM_MAX_WORKERS);
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            printf("shutting down...\n");
            return;
        } else {
            printf("unknown command '%s' -- type 'help'\n", cmd);
        }
    }
    /* EOF on stdin (e.g. piped input, or the terminal was closed): shut down
     * exactly the same way "quit" does. */
}
