#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define VFS_OP_READ  0
#define VFS_OP_WRITE 1
#define VFS_OP_LIST  2

typedef struct {
    int reply_port;
    int op;
    char name[32];
    int size;
    uint8_t data[512];
} vfs_msg_t;

int main(void) {
    printf("[vfs_server] starting...\n");
    
    if (ntrfs_mount() <= 0) {
        printf("[vfs_server] disk not mounted, formatting...\n");
        if (ntrfs_format() <= 0) {
            printf("[vfs_server] FATAL: cannot format disk!\n");
            return 1;
        }
    }
    printf("[vfs_server] disk ready\n");
    
    long port = ipc_create();
    if (port < 0) {
        printf("[vfs_server] failed to create port\n");
        return 1;
    }
    
    kv_write("vfs_port", &port, sizeof(port));
    printf("[vfs_server] listening on port %d\n", (int)port);
    
    static vfs_msg_t msg;
    for (;;) {
        long n = ipc_recv(port, &msg, sizeof(msg));
        if (n != sizeof(msg)) continue;
        
        printf("[vfs_server] got request op=%d name=%s\n", msg.op, msg.name);
        
        if (msg.op == VFS_OP_READ) {
            int r = ntrfs_read(msg.name, msg.data, sizeof(msg.data));
            if (r < 0) r = 0;
            msg.size = r;
        } 
        else if (msg.op == VFS_OP_WRITE) {
            // ntrfs_write это обертка над ntrfs_create
            int r = ntrfs_write(msg.name, msg.data, msg.size);
            msg.size = (r >= 0) ? 1 : 0;
        }
        else if (msg.op == VFS_OP_LIST) {
            // В этой версии просто проверяем, есть ли файлы
            msg.size = 0;
            msg.name[0] = 0;
        }
        
        // Отправляем ответ клиенту
        ipc_send(msg.reply_port, &msg, sizeof(msg));
    }
    return 0;
}