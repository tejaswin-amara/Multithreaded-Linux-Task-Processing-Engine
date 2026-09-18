#ifndef LOOM_STATS_H
#define LOOM_STATS_H

#include <pthread.h>
#include "common.h"

typedef struct {
    int active;            /* 0 = idle, 1 = busy */
    int current_task_id;   /* -1 when idle */
} worker_state_t;

/* Read far more often (every CLI 'status', every /api/status poll) than
 * written (once per task start/finish), so a read-write lock lets all the
 * readers proceed concurrently instead of serializing behind a mutex. */
typedef struct {
    pthread_rwlock_t lock;
    long   submitted;
    long   completed;
    long   failed;
    double total_latency_ms;
    double start_ms;
    int    num_workers;
    worker_state_t workers[LOOM_MAX_WORKERS];
} stats_t;

typedef struct {
    long   submitted, completed, failed;
    double avg_latency_ms;
    double uptime_ms;
    int    num_workers;
    worker_state_t workers[LOOM_MAX_WORKERS];
} stats_snapshot_t;

void stats_init(stats_t *s, int num_workers, double start_ms);
void stats_set_num_workers(stats_t *s, int n);
void stats_task_submitted(stats_t *s);
void stats_task_started(stats_t *s, int worker_slot, int task_id);
void stats_task_finished(stats_t *s, int worker_slot, double latency_ms, int ok);
void stats_snapshot(stats_t *s, stats_snapshot_t *out, double now_ms_val);

#endif /* LOOM_STATS_H */
