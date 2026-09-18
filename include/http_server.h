#ifndef LOOM_HTTP_SERVER_H
#define LOOM_HTTP_SERVER_H

#include <pthread.h>
#include "task_queue.h"
#include "stats.h"
#include "history.h"

/* Endpoints:
 *   GET  /              -> web/dashboard.html
 *   GET  /api/status    -> {"submitted":..,"completed":..,"failed":..,
 *                            "avg_latency_ms":..,"uptime_ms":..,
 *                            "num_workers":..,"queue_depth":..,
 *                            "workers":[{"active":0|1,"task_id":n}, ...]}
 *   GET  /api/tasks     -> [{"id":..,"type":"..","status":"..",
 *                             "param":..,"latency_ms":..,"result":".."}, ...]
 *   POST /api/submit    -> body {"type":"prime|sleep|fileio","param":n}
 *                          -> {"id":n,"queued":true}
 * One acceptor thread blocks in accept(); each connection is handled on
 * its own short-lived detached thread so a slow client can never stall
 * the others. */
typedef struct {
    int port;
    const char *web_dir;
    task_queue_t *queue;
    stats_t *stats;
    history_t *history;
    int *next_task_id;
    pthread_mutex_t *id_lock;   /* protects *next_task_id, shared with the CLI */

    int listen_fd;
    pthread_t acceptor_thread;
} http_server_t;

int  http_server_start(http_server_t *srv);
void http_server_stop(http_server_t *srv);

#endif /* LOOM_HTTP_SERVER_H */
