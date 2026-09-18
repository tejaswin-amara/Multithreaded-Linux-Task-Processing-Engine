#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "thread_pool.h"
#include "common.h"

/* ---- Task execution ------------------------------------------------------
 * Each task type stands in for a class of real work: CPU-bound (prime),
 * I/O-latency-bound (sleep), and actual disk I/O (fileio). None of this
 * needs any lock -- a task_t is only ever touched by the one worker that
 * popped it, until it is hashed into shared state afterwards. */
static void execute_task(task_t *t) {
    switch (t->type) {
        case TASK_PRIME: {
            long n = t->param, count = 0;
            for (long i = 2; i <= n; i++) {
                int is_prime = 1;
                for (long d = 2; d * d <= i; d++) {
                    if (i % d == 0) { is_prime = 0; break; }
                }
                if (is_prime) count++;
            }
            snprintf(t->result, sizeof(t->result), "found %ld primes <= %ld", count, n);
            t->status = TASK_DONE;
            break;
        }
        case TASK_SLEEP: {
            /* nanosleep(), not usleep(): usleep() was dropped from
             * POSIX.1-2008, so it isn't declared under the strict
             * _POSIX_C_SOURCE=200809L this project builds with. */
            struct timespec req;
            req.tv_sec = t->param / 1000;
            req.tv_nsec = (t->param % 1000) * 1000000L;
            nanosleep(&req, NULL);
            snprintf(t->result, sizeof(t->result), "slept %ld ms", t->param);
            t->status = TASK_DONE;
            break;
        }
        case TASK_FILEIO: {
            char path[64];
            snprintf(path, sizeof(path), "/tmp/loom_task_%d.tmp", t->id);
            FILE *f = fopen(path, "wb");
            if (!f) {
                snprintf(t->result, sizeof(t->result), "fopen failed for %s", path);
                t->status = TASK_FAILED;
                break;
            }
            long bytes = t->param * 1024;
            unsigned char buf[4096];
            long written = 0;
            unsigned int seed = (unsigned int)t->id * 2654435761u + 1u;
            while (written < bytes) {
                size_t chunk = sizeof(buf);
                if (bytes - written < (long)chunk) chunk = (size_t)(bytes - written);
                for (size_t i = 0; i < chunk; i++) {
                    seed = seed * 1103515245u + 12345u;
                    buf[i] = (unsigned char)(seed >> 16);
                }
                fwrite(buf, 1, chunk, f);
                written += (long)chunk;
            }
            fflush(f);
            fclose(f);

            f = fopen(path, "rb");
            unsigned long checksum = 0;
            size_t r;
            while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
                for (size_t i = 0; i < r; i++) checksum += buf[i];
            }
            fclose(f);
            remove(path);
            snprintf(t->result, sizeof(t->result), "wrote/read %ldKB, checksum %lu", t->param, checksum);
            t->status = TASK_DONE;
            break;
        }
        default:
            snprintf(t->result, sizeof(t->result), "unrecognized task type");
            t->status = TASK_FAILED;
    }
}

static void log_line(thread_pool_t *pool, const char *line) {
    pthread_mutex_lock(&pool->log_lock);
    if (pool->log_fp) {
        fputs(line, pool->log_fp);
        fflush(pool->log_fp);
    }
    pthread_mutex_unlock(&pool->log_lock);
}

/* Every worker claims one slot index for its entire lifetime (used only to
 * decide which "worker N" box the dashboard lights up) and releases it on
 * exit so a later-spawned thread can reuse it. */
static int claim_slot(thread_pool_t *pool) {
    int slot = -1;
    pthread_mutex_lock(&pool->count_lock);
    for (int i = 0; i < LOOM_MAX_WORKERS; i++) {
        if (!pool->slot_used[i]) { pool->slot_used[i] = 1; slot = i; break; }
    }
    pthread_mutex_unlock(&pool->count_lock);
    return slot;
}

static void release_slot(thread_pool_t *pool, int slot) {
    pthread_mutex_lock(&pool->count_lock);
    if (slot >= 0) pool->slot_used[slot] = 0;
    pool->live_workers--;
    pthread_cond_broadcast(&pool->count_cond);
    pthread_mutex_unlock(&pool->count_lock);
}

void *worker_main(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;
    int slot = claim_slot(pool);

    for (;;) {
        task_t *t = queue_pop(pool->queue);
        if (t == NULL) break;                 /* queue shut down and drained */
        if (t->type == TASK_POISON) { free(t); break; }  /* asked to retire */

        stats_task_started(pool->stats, slot, t->id);
        t->start_ms = now_ms();
        t->status = TASK_RUNNING;

        execute_task(t);

        t->end_ms = now_ms();
        double latency = t->end_ms - t->enqueue_ms;   /* queue wait + execution */
        int ok = (t->status == TASK_DONE);
        stats_task_finished(pool->stats, slot, latency, ok);
        history_add(pool->history, t, latency);

        char line[256];
        snprintf(line, sizeof(line), "[%.0fms] task #%d (%s) -> %s | %s\n",
                 t->end_ms, t->id, task_type_name(t->type), task_status_name(t->status), t->result);
        log_line(pool, line);

        free(t);
    }

    release_slot(pool, slot);
    return NULL;
}

int pool_init(thread_pool_t *pool, int num_workers, task_queue_t *queue,
              stats_t *stats, history_t *history, const char *log_path) {
    pool->queue = queue;
    pool->stats = stats;
    pool->history = history;
    pthread_mutex_init(&pool->log_lock, NULL);
    pthread_mutex_init(&pool->count_lock, NULL);
    pthread_cond_init(&pool->count_cond, NULL);
    pool->live_workers = 0;
    pool->target_workers = num_workers;
    memset(pool->slot_used, 0, sizeof(pool->slot_used));

    pool->log_fp = fopen(log_path, "a");
    if (!pool->log_fp) return -1;

    for (int i = 0; i < num_workers; i++) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_main, pool) != 0) return -1;
        pthread_detach(tid);
        pool->live_workers++;
    }
    return 0;
}

int pool_resize(thread_pool_t *pool, int new_size) {
    if (new_size < 1 || new_size > LOOM_MAX_WORKERS) return -1;

    pthread_mutex_lock(&pool->count_lock);
    int current = pool->target_workers;
    pool->target_workers = new_size;
    pthread_mutex_unlock(&pool->count_lock);

    if (new_size > current) {
        for (int i = 0; i < new_size - current; i++) {
            pthread_t tid;
            if (pthread_create(&tid, NULL, worker_main, pool) != 0) return -1;
            pthread_detach(tid);
            pthread_mutex_lock(&pool->count_lock);
            pool->live_workers++;
            pthread_mutex_unlock(&pool->count_lock);
        }
    } else if (new_size < current) {
        for (int i = 0; i < current - new_size; i++) {
            task_t *poison = safe_calloc(1, sizeof(task_t));
            poison->type = TASK_POISON;
            queue_push(pool->queue, poison);
        }
    }
    stats_set_num_workers(pool->stats, new_size);
    return 0;
}

void pool_shutdown_and_wait(thread_pool_t *pool) {
    queue_shutdown(pool->queue);
    pthread_mutex_lock(&pool->count_lock);
    while (pool->live_workers > 0) {
        pthread_cond_wait(&pool->count_cond, &pool->count_lock);
    }
    pthread_mutex_unlock(&pool->count_lock);
}

void pool_destroy(thread_pool_t *pool) {
    if (pool->log_fp) fclose(pool->log_fp);
    pthread_mutex_destroy(&pool->log_lock);
    pthread_mutex_destroy(&pool->count_lock);
    pthread_cond_destroy(&pool->count_cond);
}
