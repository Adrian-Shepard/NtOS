#include "ntmem_fs.h"
#include "tar.h"
#include "../mem/heap.h"
#include "../log/log.h"

static ntmem_file_t g_files[NTMEM_MAX_FILES];
static int          g_count = 0;

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < NTMEM_NAME_MAX - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* callback из tar_parse */
static void on_file(const char *name, const uint8_t *data, uint64_t size) {
    if (g_count >= NTMEM_MAX_FILES) {
        logw(S_NTM, "too many files, skipping %s", name);
        return;
    }
    ntmem_file_t *f = &g_files[g_count];
    copy_name(f->name, name);

    /* Копируем данные в кучу, чтобы не зависеть от tar_data */
    uint8_t *copy = (uint8_t*)kmalloc(size + 1);
    if (!copy) {
        logw(S_NTM, "kmalloc failed for %s (%d bytes)", name, size);
        return;
    }
    for (uint64_t i = 0; i < size; i++) copy[i] = data[i];
    copy[size] = 0;

    f->data = copy;
    f->size = size;
    g_count++;
}

void ntmem_fs_init(const uint8_t *tar_data, uint64_t tar_size) {
    g_count = 0;
    for (int i = 0; i < NTMEM_MAX_FILES; i++) {
        g_files[i].name[0] = 0;
        g_files[i].data = 0;
        g_files[i].size = 0;
    }

    tar_parse(tar_data, tar_size, on_file);

    logi(S_NTM, "ntmem_fs: %d files mounted", g_count);
    for (int i = 0; i < g_count; i++) {
        logi(S_NTM, "  %s (%d bytes)", g_files[i].name, g_files[i].size);
    }
}

int ntmem_fs_count(void) { return g_count; }

ntmem_file_t *ntmem_fs_get(int i) {
    if (i < 0 || i >= g_count) return 0;
    return &g_files[i];
}

ntmem_file_t *ntmem_fs_find(const char *name) {
    for (int i = 0; i < g_count; i++) {
        if (streq(g_files[i].name, name)) return &g_files[i];
    }
    return 0;
}