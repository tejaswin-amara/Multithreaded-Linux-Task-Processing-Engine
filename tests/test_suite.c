#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include "task_queue.h"
#include "history.h"
#include "common.h"


#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 4
#define TASKS_PER_PRODUCER 10000

task_queue_t q;
int tasks_consumed = 0;
pthread_mutex_t consume_lock = PTHREAD_MUTEX_INITIALIZER;

void *producer_func(void *arg) {
    (void)arg;
    for (int i = 0; i < TASKS_PER_PRODUCER; i++) {
        task_t *t = safe_calloc(1, sizeof(task_t));
        t->type = TASK_SLEEP;
        t->param = 0;
        queue_push(&q, t);
    }
    return NULL;
}

void *consumer_func(void *arg) {
    (void)arg;
    while (1) {
        task_t *t = queue_pop(&q);
        if (!t) break;
        free(t);
        pthread_mutex_lock(&consume_lock);
        tasks_consumed++;
        pthread_mutex_unlock(&consume_lock);
    }
    return NULL;
}

void test_high_contention() {
    printf("test_high_contention...\n");
    queue_init(&q, 100);
    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];

    for (int i = 0; i < NUM_CONSUMERS; i++) pthread_create(&consumers[i], NULL, consumer_func, NULL);
    for (int i = 0; i < NUM_PRODUCERS; i++) pthread_create(&producers[i], NULL, producer_func, NULL);

    for (int i = 0; i < NUM_PRODUCERS; i++) pthread_join(producers[i], NULL);
    queue_shutdown(&q);
    for (int i = 0; i < NUM_CONSUMERS; i++) pthread_join(consumers[i], NULL);

    assert(tasks_consumed == NUM_PRODUCERS * TASKS_PER_PRODUCER);
    queue_destroy(&q);
    printf("test_high_contention passed.\n");
}

void test_queue_boundaries() {
    printf("test_queue_boundaries...\n");
    task_queue_t q2;
    queue_init(&q2, 2);

    task_t *t1 = safe_calloc(1, sizeof(task_t));
    task_t *t2 = safe_calloc(1, sizeof(task_t));
    task_t *t3 = safe_calloc(1, sizeof(task_t));

    assert(queue_try_push(&q2, t1) == 0);
    assert(queue_try_push(&q2, t2) == 0);
    assert(queue_try_push(&q2, t3) == -1); // Should fail, queue is full

    double start = now_ms();
    assert(queue_push_timeout(&q2, t3, 100) == -1); // Should timeout
    double elapsed = now_ms() - start;
    assert(elapsed >= 90 && elapsed <= 200); // Allow some OS scheduling variance

    queue_shutdown(&q2);
    task_t *t4 = safe_calloc(1, sizeof(task_t));
    assert(queue_try_push(&q2, t4) == -1); // Should fail, queue is shutdown
    free(t4);
    free(t3);

    // Drain
    task_t *popped;
    int drained = 0;
    while ((popped = queue_pop(&q2)) != NULL) {
        free(popped);
        drained++;
    }
    assert(drained == 2);

    queue_destroy(&q2);
    printf("test_queue_boundaries passed.\n");
}

void test_history_rollover() {
    printf("test_history_rollover...\n");
    history_t h;
    history_init(&h);

    // Add more entries than capacity
    int total_to_add = LOOM_HISTORY_CAP + 20;
    for (int i = 0; i < total_to_add; i++) {
        task_t t;
        memset(&t, 0, sizeof(t));
        t.id = i;
        snprintf(t.result, sizeof(t.result), "Result %d", i);
        history_add(&h, &t, 10.0);
    }

    history_entry_t out[LOOM_HISTORY_CAP];
    int n = history_snapshot(&h, out, LOOM_HISTORY_CAP);
    assert(n == LOOM_HISTORY_CAP);

    // Check that the most recent ones are present
    for (int i = 0; i < LOOM_HISTORY_CAP; i++) {
        // The newest is at index 0, so id should be (total_to_add - 1 - i)
        assert(out[i].id == total_to_add - 1 - i);
    }
    printf("test_history_rollover passed.\n");
}

int main() {
    printf("Running unit tests...\n");
    test_high_contention();
    test_queue_boundaries();
    test_history_rollover();
    printf("All tests passed.\n");
    return 0;
}
