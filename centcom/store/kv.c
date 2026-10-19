#include "kv.h"
#include "../log/log.h"

typedef struct {
    char     name[KV_NAME_MAX];
    uint8_t  data[KV_DATA_MAX];
    uint64_t size;
    uint8_t  used;
} kv_entry_t;

static kv_entry_t g_kv[KV_MAX_ENTRIES];

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int kv_write(const char *key, const uint8_t *data, uint64_t len) {
    if (!key || !data || len > KV_DATA_MAX) return -1;

    int slot = -1;
    for (int i = 0; i < KV_MAX_ENTRIES; i++) {
        if (g_kv[i].used && streq(g_kv[i].name, key)) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < KV_MAX_ENTRIES; i++) {
            if (!g_kv[i].used) { slot = i; break; }
        }
    }
    if (slot < 0) return -1;

    int i = 0;
    while (key[i] && i < KV_NAME_MAX - 1) { g_kv[slot].name[i] = key[i]; i++; }
    g_kv[slot].name[i] = 0;

    for (uint64_t j = 0; j < len; j++) g_kv[slot].data[j] = data[j];
    g_kv[slot].size = len;
    g_kv[slot].used = 1;
    return 0;
}

int kv_read(const char *key, uint8_t *out, uint64_t max) {
    if (!key || !out) return -1;
    for (int i = 0; i < KV_MAX_ENTRIES; i++) {
        if (g_kv[i].used && streq(g_kv[i].name, key)) {
            uint64_t n = g_kv[i].size;
            if (n > max) n = max;
            for (uint64_t j = 0; j < n; j++) out[j] = g_kv[i].data[j];
            return (int)n;
        }
    }
    return -1;
}