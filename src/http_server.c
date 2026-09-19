#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "http_server.h"
#include "common.h"

typedef struct {
    http_server_t *srv;
    int client_fd;
} conn_ctx_t;

/* Small localhost responses fit in one write() in practice; we still check
 * the return value (rather than retry a short write in a loop) since this
 * is a monitoring dashboard, not a hardened public-facing server -- see
 * docs/02-architecture.md "Known limitations". */
static void send_response(int fd, int status, const char *status_text,
                           const char *content_type, const char *body, size_t body_len) {
    char header[256];
    int hn = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    if (hn > 0 && write(fd, header, (size_t)hn) < 0) return;
    if (body_len && write(fd, body, body_len) < 0) return;
}

static void send_file(int fd, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        const char *msg = "dashboard.html not found -- run loom from the project root, "
                           "or pass --web-dir <path>\n";
        send_response(fd, 404, "Not Found", "text/plain", msg, strlen(msg));
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = safe_malloc((size_t)sz > 0 ? (size_t)sz : 1);
    size_t got = buf ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    if (!buf) {
        const char *msg = "server out of memory\n";
        send_response(fd, 500, "Internal Server Error", "text/plain", msg, strlen(msg));
        return;
    }
    send_response(fd, 200, "OK", "text/html; charset=utf-8", buf, got);
    free(buf);
}

static int next_task_id(http_server_t *srv) {
    pthread_mutex_lock(srv->id_lock);
    int id = (*srv->next_task_id)++;
    pthread_mutex_unlock(srv->id_lock);
    return id;
}

static void handle_status(http_server_t *srv, int fd) {
    stats_snapshot_t snap;
    stats_snapshot(srv->stats, &snap, now_ms());

    char body[2048];
    int n = snprintf(body, sizeof(body),
        "{\"submitted\":%ld,\"completed\":%ld,\"failed\":%ld,"
        "\"avg_latency_ms\":%.2f,\"uptime_ms\":%.0f,"
        "\"num_workers\":%d,\"queue_depth\":%d,\"queue_capacity\":%d,\"workers\":[",
        snap.submitted, snap.completed, snap.failed,
        snap.avg_latency_ms, snap.uptime_ms,
        snap.num_workers, queue_size(srv->queue), queue_capacity(srv->queue));

    int cap = snap.num_workers < LOOM_MAX_WORKERS ? snap.num_workers : LOOM_MAX_WORKERS;
    for (int i = 0; i < cap; i++) {
        n += snprintf(body + n, sizeof(body) - (size_t)n, "%s{\"active\":%d,\"task_id\":%d}",
                      i ? "," : "", snap.workers[i].active, snap.workers[i].current_task_id);
    }
    n += snprintf(body + n, sizeof(body) - (size_t)n, "]}");
    send_response(fd, 200, "OK", "application/json", body, (size_t)n);
}

static void handle_tasks(http_server_t *srv, int fd) {
    history_entry_t entries[LOOM_HISTORY_CAP];
    int n = history_snapshot(srv->history, entries, LOOM_HISTORY_CAP);

    size_t cap = 8192;
    char *body = safe_malloc(cap);
    int off = snprintf(body, cap, "[");
    for (int i = 0; i < n; i++) {
        off += snprintf(body + off, cap - (size_t)off,
            "%s{\"id\":%d,\"type\":\"%s\",\"status\":\"%s\",\"param\":%ld,"
            "\"latency_ms\":%.1f,\"result\":\"%s\"}",
            i ? "," : "", entries[i].id, task_type_name(entries[i].type),
            task_status_name(entries[i].status), entries[i].param,
            entries[i].latency_ms, entries[i].result);
    }
    off += snprintf(body + off, cap - (size_t)off, "]");
    send_response(fd, 200, "OK", "application/json", body, (size_t)off);
    free(body);
}

/* Deliberately not a general JSON parser -- our request schema is exactly
 * {"type":"...","param":N}, so a couple of strstr() calls are enough and
 * keep the project free of a third-party JSON dependency. See docs/02. */
static void handle_submit(http_server_t *srv, int fd, const char *body_json) {
    char type_buf[16] = {0};
    long param = 0;

    const char *tpos = strstr(body_json, "\"type\"");
    if (tpos) {
        const char *q1 = strchr(tpos + 6, '"');
        if (q1) {
            const char *q2 = strchr(q1 + 1, '"');
            if (q2 && (size_t)(q2 - q1 - 1) < sizeof(type_buf)) {
                memcpy(type_buf, q1 + 1, (size_t)(q2 - q1 - 1));
            }
        }
    }
    const char *ppos = strstr(body_json, "\"param\"");
    if (ppos) {
        const char *colon = strchr(ppos, ':');
        if (colon) param = atol(colon + 1);
    }

    task_type_t type;
    if (strcmp(type_buf, "prime") == 0) type = TASK_PRIME;
    else if (strcmp(type_buf, "sleep") == 0) type = TASK_SLEEP;
    else if (strcmp(type_buf, "fileio") == 0) type = TASK_FILEIO;
    else {
        const char *err = "{\"error\":\"type must be prime, sleep, or fileio\"}";
        send_response(fd, 400, "Bad Request", "application/json", err, strlen(err));
        return;
    }
    if (param <= 0) param = 1;

    task_t *t = safe_calloc(1, sizeof(task_t));
    t->id = next_task_id(srv);
    t->type = type;
    t->param = param;
    t->status = TASK_PENDING;
    t->enqueue_ms = now_ms();
    stats_task_submitted(srv->stats);

    /* Capture the id before handing t to the queue -- see the matching
     * comment in cli.c's submit_task() for why *t is unsafe to touch after
     * a successful push. */
    int id = t->id;
    if (queue_push(srv->queue, t) != 0) {
        free(t);
        const char *err = "{\"error\":\"server is shutting down\"}";
        send_response(fd, 503, "Service Unavailable", "application/json", err, strlen(err));
        return;
    }
    char body[128];
    int n = snprintf(body, sizeof(body), "{\"id\":%d,\"queued\":true}", id);
    send_response(fd, 200, "OK", "application/json", body, (size_t)n);
}

static void *handle_conn(void *arg) {
    conn_ctx_t *ctx = (conn_ctx_t *)arg;
    char buf[8192];
    ssize_t n = read(ctx->client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(ctx->client_fd); free(ctx); return NULL; }
    buf[n] = '\0';

    char method[8] = {0}, path[256] = {0};
    sscanf(buf, "%7s %255s", method, path);

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        char full[512];
        snprintf(full, sizeof(full), "%s/dashboard.html", ctx->srv->web_dir);
        send_file(ctx->client_fd, full);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
        handle_status(ctx->srv, ctx->client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/tasks") == 0) {
        handle_tasks(ctx->srv, ctx->client_fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/submit") == 0) {
        char *body_start = strstr(buf, "\r\n\r\n");
        handle_submit(ctx->srv, ctx->client_fd, body_start ? body_start + 4 : "");
    } else if (strcmp(method, "OPTIONS") == 0) {
        send_response(ctx->client_fd, 204, "No Content", "text/plain", "", 0);
    } else {
        const char *msg = "not found\n";
        send_response(ctx->client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
    }

    close(ctx->client_fd);
    free(ctx);
    return NULL;
}

static void *acceptor_main(void *arg) {
    http_server_t *srv = (http_server_t *)arg;
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int cfd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &len);
        if (cfd < 0) break;   /* listen_fd was closed by http_server_stop() */

        conn_ctx_t *ctx = safe_malloc(sizeof(conn_ctx_t));
        ctx->srv = srv;
        ctx->client_fd = cfd;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_conn, ctx) != 0) {
            close(cfd);
            free(ctx);
            continue;
        }
        pthread_detach(tid);
    }
    return NULL;
}

int http_server_start(http_server_t *srv) {
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)srv->port);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(srv->listen_fd);
        return -1;
    }
    if (listen(srv->listen_fd, 64) < 0) {
        close(srv->listen_fd);
        return -1;
    }
    if (pthread_create(&srv->acceptor_thread, NULL, acceptor_main, srv) != 0) {
        close(srv->listen_fd);
        return -1;
    }
    return 0;
}

void http_server_stop(http_server_t *srv) {
    shutdown(srv->listen_fd, SHUT_RDWR);
    close(srv->listen_fd);
    pthread_join(srv->acceptor_thread, NULL);
}
