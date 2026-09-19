#include <stdio.h>
#include <string.h>
#include "history.h"

void history_init(history_t *h) {
    pthread_mutex_init(&h->lock, NULL);
    memset(h->entries, 0, sizeof(h->entries));
    h->head = 0;
    h->count = 0;
}

void history_add(history_t *h, const task_t *t, double latency_ms) {
    pthread_mutex_lock(&h->lock);
    history_entry_t *e = &h->entries[h->head];
    e->id = t->id;
    e->type = t->type;
    e->status = t->status;
    e->param = t->param;
    e->latency_ms = latency_ms;
    snprintf(e->result, sizeof(e->result), "%s", t->result);
    e->result[sizeof(e->result) - 1] = '\0';
    h->head = (h->head + 1) % LOOM_HISTORY_CAP;
    if (h->count < LOOM_HISTORY_CAP) h->count++;
    pthread_mutex_unlock(&h->lock);
}

int history_snapshot(history_t *h, history_entry_t *out, int max) {
    pthread_mutex_lock(&h->lock);
    int n = h->count < max ? h->count : max;
    for (int i = 0; i < n; i++) {
        int idx = (h->head - 1 - i + LOOM_HISTORY_CAP) % LOOM_HISTORY_CAP;
        out[i] = h->entries[idx];
    }
    pthread_mutex_unlock(&h->lock);
    return n;
}
