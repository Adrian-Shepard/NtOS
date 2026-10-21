#pragma once
#include <stdint.h>

#define NTRFS_MAGIC      0x4652544EU
#define NTRFS_VERSION    1
#define NTRFS_BLOCK_SIZE 512
#define NTRFS_INODE_MAX  256
#define NTRFS_NAME_MAX   32

typedef struct {
    char     name[NTRFS_NAME_MAX];
    uint32_t size;
    uint32_t first_block;
    uint32_t block_count;
    uint8_t  type;
    uint8_t  reserved[19]; 
} __attribute__((packed)) ntrfs_inode_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_table;
    uint32_t data_start;
    uint32_t free_blocks;
    uint8_t  reserved[NTRFS_BLOCK_SIZE - 32];
} __attribute__((packed)) ntrfs_super_t;

int  ntrfs_format(void);
int  ntrfs_mount(void);
int  ntrfs_is_mounted(void);
int  ntrfs_create(const char *name, const uint8_t *data, uint32_t size);
int  ntrfs_read(const char *name, uint8_t *out, uint32_t max);
int  ntrfs_list(int idx, ntrfs_inode_t *out);