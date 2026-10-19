#pragma once
#include <stdint.h>

#define KV_MAX_ENTRIES 8
#define KV_NAME_MAX    32
#define KV_DATA_MAX    8192

int kv_write(const char *key, const uint8_t *data, uint64_t len);
int kv_read (const char *key, uint8_t *out, uint64_t max);