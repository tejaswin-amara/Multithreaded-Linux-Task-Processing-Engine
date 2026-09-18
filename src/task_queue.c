#include <stdlib.h>
#include "task_queue.h"

int queue_init(task_queue_t *q, int capacity) {
    q->items = safe_calloc((size_t)capacity, sizeof(task_t *));
    if (!q->items) return -1;
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    q->shutdown = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&q->not_empty, &attr);
    pthread_cond_init(&q->not_full, &attr);
    pthread_condattr_destroy(&attr);
    return 0;
}

void queue_destroy(task_queue_t *q) {
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    for (int i=0; i<q->count; i++) { free(q->items[(q->head + i) % q->capacity]); }
    free(q->items);
}

int queue_push(task_queue_t *q, task_t *t) {
    pthread_mutex_lock(&q->lock);
    /* Loop, not "if": pthread_cond_wait can wake spuriously, and another
     * producer may have refilled the slot we were signaled about. */
    while (q->count == q->capacity && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    if (q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

task_t *queue_pop(task_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->count == 0 && q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }
    task_t *t = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return t;
}

void queue_shutdown(task_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

int queue_size(task_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    int n = q->count;
    pthread_mutex_unlock(&q->lock);
    return n;
}

int queue_capacity(task_queue_t *q) {
    return q->capacity;
}

int queue_try_push(task_queue_t *q, task_t *t) {
    pthread_mutex_lock(&q->lock);
    if (q->count == q->capacity || q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

int queue_push_timeout(task_queue_t *q, task_t *t, long timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&q->lock);
    while (q->count == q->capacity && !q->shutdown) {
        int ret = pthread_cond_timedwait(&q->not_full, &q->lock, &ts);
        if (ret != 0) {
            pthread_mutex_unlock(&q->lock);
            return -1;
        }
    }
    if (q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}
