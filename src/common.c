#include <time.h>
#include "common.h"

/* Monotonic clock in milliseconds since an arbitrary but fixed reference
 * point. We only ever compare two now_ms() values against each other
 * (latency, uptime), so CLOCK_MONOTONIC is the right choice: it can't jump
 * backwards if the system clock is adjusted mid-run. */
double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

const char *task_type_name(task_type_t t) {
    switch (t) {
        case TASK_PRIME:  return "prime";
        case TASK_SLEEP:  return "sleep";
        case TASK_FILEIO: return "fileio";
        default:          return "poison";
    }
}

const char *task_status_name(task_status_t s) {
    switch (s) {
        case TASK_PENDING: return "pending";
        case TASK_RUNNING: return "running";
        case TASK_DONE:    return "done";
        default:           return "failed";
    }
}
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>

void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *safe_calloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr && nmemb > 0 && size > 0) {
        fprintf(stderr, "Out of memory allocating %zu elements of %zu bytes\n", nmemb, size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

void loom_log(const char *level, const char *fmt, ...) {
    pthread_mutex_lock(&g_log_lock);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm_info = localtime(&ts.tv_sec);
    char time_buf[26];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("%s.%03ld %s [Thread %lu] ", time_buf, ts.tv_nsec / 1000000, level, (unsigned long)pthread_self());

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);

    pthread_mutex_unlock(&g_log_lock);
}
