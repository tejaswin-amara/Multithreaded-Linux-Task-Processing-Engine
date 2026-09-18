#ifndef LOOM_COMMON_H
#define LOOM_COMMON_H

/* ---- Tunables / limits ------------------------------------------------- */
#define LOOM_MAX_WORKERS        64
#define LOOM_DEFAULT_WORKERS     4
#define LOOM_DEFAULT_QUEUE_CAP  32
#define LOOM_DEFAULT_PORT     8080
#define LOOM_HISTORY_CAP        50
#define LOOM_LOG_PATH   "loom.log"

/* A task is either real work or an internal control message.
 * TASK_POISON is never created by a user; the pool pushes it to itself
 * to tell exactly one worker to retire (used when shrinking the pool). */
typedef enum {
    TASK_PRIME = 0,
    TASK_SLEEP,
    TASK_FILEIO,
    TASK_POISON
} task_type_t;

typedef enum {
    TASK_PENDING = 0,
    TASK_RUNNING,
    TASK_DONE,
    TASK_FAILED
} task_status_t;

typedef struct task {
    int id;
    task_type_t type;
    long param;
    task_status_t status;
    double enqueue_ms;   /* now_ms() when queue_push() was called          */
    double start_ms;     /* now_ms() when a worker began executing it      */
    double end_ms;       /* now_ms() when execution finished               */
    char result[160];
} task_t;

double now_ms(void);
const char *task_type_name(task_type_t t);
const char *task_status_name(task_status_t s);

#endif /* LOOM_COMMON_H */
