#ifndef LOOM_TASK_QUEUE_H
#define LOOM_TASK_QUEUE_H

#include <pthread.h>
#include "common.h"

/* Fixed-capacity circular buffer of task_t*, safe for any number of
 * concurrent producers (CLI thread, HTTP handler threads) and consumers
 * (worker threads). One mutex guards the buffer; two condition variables
 * let producers block on "full" and consumers block on "empty" without
 * busy-waiting. */
typedef struct {
    task_t **items;
    int capacity;
    int head;       /* next slot to pop   */
    int tail;       /* next slot to push  */
    int count;
    int shutdown;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} task_queue_t;

int      queue_init(task_queue_t *q, int capacity);
void     queue_destroy(task_queue_t *q);

/* Blocks while the queue is full. Returns 0 on success, -1 if the queue
 * was shut down before room became available (ownership of *t stays with
 * the caller in that case). */
int      queue_push(task_queue_t *q, task_t *t);

/* Blocks while the queue is empty. Returns the next task, or NULL once the
 * queue has been shut down AND fully drained -- so a shutdown lets every
 * already-queued task still be popped and finished before consumers see
 * NULL and retire. */
task_t  *queue_pop(task_queue_t *q);

/* Wakes every blocked producer and consumer. Idempotent. */
void     queue_shutdown(task_queue_t *q);
int      queue_size(task_queue_t *q);

/* Fixed at queue_init() and never changed again, so unlike queue_size()
 * this is safe to read without the lock. */
int      queue_capacity(task_queue_t *q);

#endif /* LOOM_TASK_QUEUE_H */

/* Non-blocking push; returns 0 on success, -1 if the queue is full or shut down. */
int      queue_try_push(task_queue_t *q, task_t *t);

/* Timed wait push; returns 0 on success, -1 if the timeout expires or the queue is shut down. */
int      queue_push_timeout(task_queue_t *q, task_t *t, long timeout_ms);
