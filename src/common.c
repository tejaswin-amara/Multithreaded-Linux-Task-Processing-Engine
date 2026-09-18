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
