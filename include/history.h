#ifndef LOOM_HISTORY_H
#define LOOM_HISTORY_H

#include <pthread.h>
#include "common.h"

typedef struct {
    int id;
    task_type_t type;
    task_status_t status;
    long param;
    double latency_ms;
    char result[160];
} history_entry_t;

/* Simple mutex-guarded circular buffer -- a second, independent shared
 * resource alongside the queue and the stats block, each with its own
 * lock so contention on one never blocks the others. */
typedef struct {
    pthread_mutex_t lock;
    history_entry_t entries[LOOM_HISTORY_CAP];
    int head;    /* next write position   */
    int count;   /* entries filled so far, saturating at LOOM_HISTORY_CAP */
} history_t;

void history_init(history_t *h);
void history_add(history_t *h, const task_t *t, double latency_ms);

/* Writes up to `max` entries into out[], most recent first. Returns the
 * number actually written. */
int  history_snapshot(history_t *h, history_entry_t *out, int max);

#endif /* LOOM_HISTORY_H */
