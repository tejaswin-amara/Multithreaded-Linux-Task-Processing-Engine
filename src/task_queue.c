#include <stdlib.h>
#include "task_queue.h"

int queue_init(task_queue_t *q, int capacity) {
    q->items = calloc((size_t)capacity, sizeof(task_t *));
    if (!q->items) return -1;
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    q->shutdown = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return 0;
}

void queue_destroy(task_queue_t *q) {
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
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
