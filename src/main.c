#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>

#include "common.h"
#include "task_queue.h"
#include "stats.h"
#include "history.h"
#include "thread_pool.h"
#include "http_server.h"
#include "cli.h"

static task_queue_t   g_queue;
static stats_t        g_stats;
static history_t      g_history;
static thread_pool_t  g_pool;
static http_server_t  g_http;

static int g_next_id = 1;
static pthread_mutex_t g_id_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t g_shutdown_once_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_shutdown_done = 0;

/* Called from exactly one of two places: after cli_run() returns normally
 * ("quit"/"exit"/EOF), or from the signal-handling thread after sigwait()
 * catches SIGINT/SIGTERM. The once-guard makes it safe if both happen to
 * race (e.g. Ctrl-C right as someone types "quit"). */
static void graceful_shutdown(void) {
    pthread_mutex_lock(&g_shutdown_once_lock);
    if (g_shutdown_done) { pthread_mutex_unlock(&g_shutdown_once_lock); return; }
    g_shutdown_done = 1;
    pthread_mutex_unlock(&g_shutdown_once_lock);

    printf("\nstopping web server...\n");
    http_server_stop(&g_http);

    printf("draining task queue and stopping workers (pending tasks will finish first)...\n");
    pool_shutdown_and_wait(&g_pool);

    stats_snapshot_t snap;
    stats_snapshot(&g_stats, &snap, now_ms());
    printf("final stats -- submitted %ld | completed %ld | failed %ld | "
           "avg latency %.1fms | uptime %.1fs\n",
           snap.submitted, snap.completed, snap.failed,
           snap.avg_latency_ms, snap.uptime_ms / 1000.0);

    pool_destroy(&g_pool);
    queue_destroy(&g_queue);
    printf("loom stopped cleanly\n");
}

/* Runs on a dedicated thread that has SIGINT/SIGTERM unblocked nowhere else.
 * sigwait() is the async-signal-safe way to react to a signal: it delivers
 * control as ordinary, un-interrupted thread code, so graceful_shutdown()
 * can safely take locks, call printf, close sockets, etc. -- none of which
 * is safe to do from inside a traditional asynchronous signal handler. */
static void *signal_thread_main(void *arg) {
    sigset_t *set = (sigset_t *)arg;
    int caught = 0;
    sigwait(set, &caught);
    graceful_shutdown();
    exit(0);
    return NULL;
}

static void usage(const char *prog) {
    printf("usage: %s [-w workers] [-p port] [-q queue-size] [-d web-dir]\n\n"
           "  -w, --workers N     number of worker threads to start with (default %d)\n"
           "  -p, --port N        port for the web dashboard / API (default %d)\n"
           "  -q, --queue-size N  bounded task queue capacity (default %d)\n"
           "  -d, --web-dir DIR   directory containing dashboard.html (default \"web\")\n"
           "  -h, --help          show this message\n",
           prog, LOOM_DEFAULT_WORKERS, LOOM_DEFAULT_PORT, LOOM_DEFAULT_QUEUE_CAP);
}

int main(int argc, char **argv) {
    int workers = LOOM_DEFAULT_WORKERS;
    int port = LOOM_DEFAULT_PORT;
    int qcap = LOOM_DEFAULT_QUEUE_CAP;
    char web_dir[256] = "web";

    static struct option long_opts[] = {
        {"workers",    required_argument, 0, 'w'},
        {"port",       required_argument, 0, 'p'},
        {"queue-size", required_argument, 0, 'q'},
        {"web-dir",    required_argument, 0, 'd'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "w:p:q:d:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'w': workers = atoi(optarg); break;
            case 'p': port = atoi(optarg); break;
            case 'q': qcap = atoi(optarg); break;
            case 'd': strncpy(web_dir, optarg, sizeof(web_dir) - 1); break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }
    if (workers < 1 || workers > LOOM_MAX_WORKERS) {
        fprintf(stderr, "workers must be between 1 and %d\n", LOOM_MAX_WORKERS);
        return 1;
    }
    if (qcap < 1) {
        fprintf(stderr, "queue-size must be at least 1\n");
        return 1;
    }

    /* Block SIGINT/SIGTERM here, in the main thread, before any other thread
     * is created -- every thread inherits the calling thread's signal mask
     * at pthread_create() time, so this guarantees only signal_thread_main()
     * (which explicitly sigwait()s on them) ever "receives" these signals. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    double start = now_ms();
    stats_init(&g_stats, workers, start);
    history_init(&g_history);

    if (queue_init(&g_queue, qcap) != 0) {
        fprintf(stderr, "failed to allocate task queue\n");
        return 1;
    }
    if (pool_init(&g_pool, workers, &g_queue, &g_stats, &g_history, LOOM_LOG_PATH) != 0) {
        fprintf(stderr, "failed to start worker pool (could not open %s?)\n", LOOM_LOG_PATH);
        return 1;
    }

    g_http.port = port;
    g_http.web_dir = web_dir;
    g_http.queue = &g_queue;
    g_http.stats = &g_stats;
    g_http.history = &g_history;
    g_http.next_task_id = &g_next_id;
    g_http.id_lock = &g_id_lock;
    if (http_server_start(&g_http) != 0) {
        fprintf(stderr, "failed to start web server on port %d (already in use?)\n", port);
        return 1;
    }

    pthread_t sig_tid;
    pthread_create(&sig_tid, NULL, signal_thread_main, &set);
    pthread_detach(sig_tid);

    cli_ctx_t cli_ctx = {
        .queue = &g_queue,
        .stats = &g_stats,
        .history = &g_history,
        .pool = &g_pool,
        .next_task_id = &g_next_id,
        .id_lock = &g_id_lock,
        .port = port,
    };
    cli_run(&cli_ctx);       /* returns on "quit" / "exit" / EOF */
    graceful_shutdown();
    return 0;
}
