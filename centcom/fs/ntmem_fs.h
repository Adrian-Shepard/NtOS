#pragma once
#include <stdint.h>

#define NTMEM_MAX_FILES 64
#define NTMEM_NAME_MAX  128

typedef struct {
    char            name[NTMEM_NAME_MAX];
    const uint8_t  *data;
    uint64_t        size;
} ntmem_file_t;

void          ntmem_fs_init(const uint8_t *tar_data, uint64_t tar_size);
int           ntmem_fs_count(void);
ntmem_file_t *ntmem_fs_get(int i);
ntmem_file_t *ntmem_fs_find(const char *name);