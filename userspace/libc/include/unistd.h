#pragma once
#include <stddef.h>

long write(int fd, const void *buf, size_t len);
void _exit(int code) __attribute__((noreturn));

int  open(const char *name, int flags);
long read(int fd, void *buf, size_t len);
int  close(int fd);
long sys_list(int idx, char *buf, size_t max);
void sleep(unsigned long ms);

long play_pcm(const void *buf, unsigned long samples);
int  kv_write(const char *key, const void *data, unsigned long len);
long kv_read (const char *key, void *out, unsigned long max);

struct ntrfs_inode_user {
    char     name[32];
    unsigned int size;
    unsigned int first_block;
    unsigned int block_count;
    unsigned char type;
    unsigned char reserved[19];
};

int  ntrfs_list(int idx, struct ntrfs_inode_user *out);
long ntrfs_read(const char *name, void *buf, unsigned long max);
int  ntrfs_write(const char *name, const void *buf, unsigned long len);

int  exec(const char *path);
int  fork(void);
int  sys_wait(int pid, int *status);

long ipc_create(void);
long ipc_send(long port, const void *buf, unsigned long len);
long ipc_recv(long port, void *buf, unsigned long max);

#define O_RDONLY 0