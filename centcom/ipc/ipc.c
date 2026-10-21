#include "ipc.h"
#include "../sched/sched.h"
#include "../log/log.h"

static ipc_port_t g_ports[IPC_MAX_PORTS];

void ipc_init(void) {
    for (int i = 0; i < IPC_MAX_PORTS; i++) g_ports[i].is_used = 0;
    logi(S_SYC, "IPC subsystem initialized (%d ports)", IPC_MAX_PORTS);
}

int ipc_create_port(uint64_t pid) {
    for (int i = 0; i < IPC_MAX_PORTS; i++) {
        if (!g_ports[i].is_used) {
            g_ports[i].is_used = 1;
            g_ports[i].owner_pid = pid;
            g_ports[i].head = 0;
            g_ports[i].tail = 0;
            g_ports[i].count = 0;
            return 100 + i; // ID порта начинается с 100
        }
    }
    return -1;
}

int ipc_send_to_port(uint64_t port_id, const uint8_t *data, uint32_t len) {
    int idx = (int)(port_id - 100);
    if (idx < 0 || idx >= IPC_MAX_PORTS || !g_ports[idx].is_used) return -1;
    if (len > IPC_MSG_MAX) len = IPC_MSG_MAX;

    ipc_port_t *p = &g_ports[idx];
    if (p->count >= IPC_QUEUE_MAX) return -1; // Очередь переполнена

    int slot = p->tail;
    for (uint32_t i = 0; i < len; i++) p->queue[slot][i] = data[i];
    p->queue_len[slot] = len;
    p->tail = (p->tail + 1) % IPC_QUEUE_MAX;
    p->count++;

    // Будим процесс-владелец, если он ждет сообщения
    for (int i = 0; i < PROC_MAX; i++) {
        process_t *pr = process_get(i);
        if (pr && pr->pid == p->owner_pid && pr->state == PROC_BLOCKED) {
            pr->state = PROC_READY;
            pr->wake_tick = 0;
        }
    }

    return 0;
}

int ipc_recv_from_port(uint64_t port_id, uint8_t *out, uint32_t max, int block) {
    int idx = (int)(port_id - 100);
    if (idx < 0 || idx >= IPC_MAX_PORTS || !g_ports[idx].is_used) return -1;

    ipc_port_t *p = &g_ports[idx];
    while (p->count == 0) {
        if (!block) return 0;
        process_t *curr = process_current();
        if (!curr) return -1;
        curr->state = PROC_BLOCKED;
        curr->wake_tick = 0;
        schedule();
    }

    int slot = p->head;
    uint32_t len = p->queue_len[slot];
    if (len > max) len = max;
    for (uint32_t i = 0; i < len; i++) out[i] = p->queue[slot][i];
    p->head = (p->head + 1) % IPC_QUEUE_MAX;
    p->count--;
    return (int)len;
}