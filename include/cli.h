#ifndef LOOM_CLI_H
#define LOOM_CLI_H

#include <pthread.h>
#include "task_queue.h"
#include "stats.h"
#include "history.h"
#include "thread_pool.h"

typedef struct {
    task_queue_t  *queue;
    stats_t       *stats;
    history_t     *history;
    thread_pool_t *pool;
    int           *next_task_id;
    pthread_mutex_t *id_lock;
    int port;
} cli_ctx_t;

/* Blocks reading stdin, one command per line, until "quit"/"exit" or EOF. */
void cli_run(cli_ctx_t *ctx);

#endif /* LOOM_CLI_H */
