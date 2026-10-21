#pragma once
#include <stdint.h>

#define IPC_MAX_PORTS 32
#define IPC_MSG_MAX   256
#define IPC_QUEUE_MAX 16

typedef struct {
    int      is_used;
    uint64_t owner_pid;
    uint8_t  queue[IPC_QUEUE_MAX][IPC_MSG_MAX];
    uint32_t queue_len[IPC_QUEUE_MAX];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} ipc_port_t;

void ipc_init(void);
int  ipc_create_port(uint64_t pid);
int  ipc_send_to_port(uint64_t port_id, const uint8_t *data, uint32_t len);
int  ipc_recv_from_port(uint64_t port_id, uint8_t *out, uint32_t max, int block);