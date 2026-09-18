#ifndef LOOM_THREAD_POOL_H
#define LOOM_THREAD_POOL_H

#include <pthread.h>
#include <stdio.h>
#include "task_queue.h"
#include "stats.h"
#include "history.h"

typedef struct {
    task_queue_t *queue;
    stats_t      *stats;
    history_t    *history;

    pthread_mutex_t log_lock;   /* guards the one shared append-only log file */
    FILE *log_fp;

    pthread_mutex_t count_lock; /* guards live_workers, target_workers, slot_used[] */
    pthread_cond_t  count_cond; /* signaled whenever live_workers changes */
    int live_workers;
    int target_workers;
    int slot_used[LOOM_MAX_WORKERS];
} thread_pool_t;

int  pool_init(thread_pool_t *pool, int num_workers, task_queue_t *queue,
               stats_t *stats, history_t *history, const char *log_path);

/* Grows or shrinks the pool to new_size. Growing spawns threads immediately;
 * shrinking pushes (old-new) TASK_POISON control tasks through the queue so
 * exactly that many workers retire once they reach the front -- no
 * pthread_cancel, so no risk of a worker being killed mid-task. */
int  pool_resize(thread_pool_t *pool, int new_size);

/* Shuts the queue down and blocks until every worker has retired. */
void pool_shutdown_and_wait(thread_pool_t *pool);
void pool_destroy(thread_pool_t *pool);

void *worker_main(void *arg);

#endif /* LOOM_THREAD_POOL_H */
