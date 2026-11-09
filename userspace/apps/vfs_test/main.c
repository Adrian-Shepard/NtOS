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
    long vfs_port = 0;
    if (kv_read("vfs_port", &vfs_port, sizeof(vfs_port)) != sizeof(vfs_port)) {
        printf("[vfs_test] vfs_server not running!\n");
        return 1;
    }
    
    long my_port = ipc_create();
    if (my_port < 0) {
        printf("[vfs_test] failed to create reply port\n");
        return 1;
    }
    
    vfs_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.reply_port = (int)my_port;
    
    // 1. Записываем файл на диск
    printf("[vfs_test] writing 'hello.txt'...\n");
    msg.op = VFS_OP_WRITE;
    strcpy(msg.name, "hello.txt");
    strcpy((char*)msg.data, "Hello from VFS server!");
    msg.size = strlen((char*)msg.data) + 1;
    ipc_send(vfs_port, &msg, sizeof(msg));
    ipc_recv(my_port, &msg, sizeof(msg));
    printf("[vfs_test] write result: %d\n", msg.size);
    
    // 2. Читаем файл обратно
    printf("[vfs_test] reading 'hello.txt'...\n");
    msg.op = VFS_OP_READ;
    strcpy(msg.name, "hello.txt");
    msg.size = 0;
    ipc_send(vfs_port, &msg, sizeof(msg));
    ipc_recv(my_port, &msg, sizeof(msg));
    printf("[vfs_test] read %d bytes: %s\n", msg.size, (char*)msg.data);
    
    printf("[vfs_test] done\n");
    return 0;
}