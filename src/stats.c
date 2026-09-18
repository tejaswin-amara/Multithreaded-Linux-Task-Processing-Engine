#include <string.h>
#include "stats.h"

void stats_init(stats_t *s, int num_workers, double start_ms) {
    pthread_rwlock_init(&s->lock, NULL);
    s->submitted = s->completed = s->failed = 0;
    s->total_latency_ms = 0.0;
    s->start_ms = start_ms;
    s->num_workers = num_workers;
    for (int i = 0; i < LOOM_MAX_WORKERS; i++) {
        s->workers[i].active = 0;
        s->workers[i].current_task_id = -1;
    }
}

void stats_set_num_workers(stats_t *s, int n) {
    pthread_rwlock_wrlock(&s->lock);
    s->num_workers = n;
    pthread_rwlock_unlock(&s->lock);
}

void stats_task_submitted(stats_t *s) {
    pthread_rwlock_wrlock(&s->lock);
    s->submitted++;
    pthread_rwlock_unlock(&s->lock);
}

void stats_task_started(stats_t *s, int worker_slot, int task_id) {
    pthread_rwlock_wrlock(&s->lock);
    if (worker_slot >= 0 && worker_slot < LOOM_MAX_WORKERS) {
        s->workers[worker_slot].active = 1;
        s->workers[worker_slot].current_task_id = task_id;
    }
    pthread_rwlock_unlock(&s->lock);
}

void stats_task_finished(stats_t *s, int worker_slot, double latency_ms, int ok) {
    pthread_rwlock_wrlock(&s->lock);
    if (ok) { s->completed++; s->total_latency_ms += latency_ms; }
    else    { s->failed++; }
    if (worker_slot >= 0 && worker_slot < LOOM_MAX_WORKERS) {
        s->workers[worker_slot].active = 0;
        s->workers[worker_slot].current_task_id = -1;
    }
    pthread_rwlock_unlock(&s->lock);
}

void stats_snapshot(stats_t *s, stats_snapshot_t *out, double now_ms_val) {
    pthread_rwlock_rdlock(&s->lock);
    out->submitted = s->submitted;
    out->completed = s->completed;
    out->failed = s->failed;
    out->avg_latency_ms = s->completed ? (s->total_latency_ms / (double)s->completed) : 0.0;
    out->uptime_ms = now_ms_val - s->start_ms;
    out->num_workers = s->num_workers;
    memcpy(out->workers, s->workers, sizeof(out->workers));
    pthread_rwlock_unlock(&s->lock);
}
