#include "ntrfs.h"
#include "../driver/ahci.h"
#include "../log/log.h"
#include "../log/serial.h"

static ntrfs_super_t g_super;
static int           g_mounted = 0;

static uint8_t       g_block[NTRFS_BLOCK_SIZE];

/* inode helpers: 8 inode per sector */
static inline uint32_t inode_sector(uint32_t i) { return g_super.inode_table + i / 8; }
static inline uint32_t inode_index(uint32_t i)  { return i % 8; }

static int read_inode(uint32_t i, ntrfs_inode_t *out) {
    if (i >= g_super.inode_count) return 0;
    if (!ahci_read_sector(inode_sector(i), g_block)) return 0;
    ntrfs_inode_t *arr = (ntrfs_inode_t*)g_block;
    *out = arr[inode_index(i)];
    return 1;
}

static int write_inode(uint32_t i, const ntrfs_inode_t *in) {
    if (i >= g_super.inode_count) return 0;
    if (!ahci_read_sector(inode_sector(i), g_block)) return 0;
    ntrfs_inode_t *arr = (ntrfs_inode_t*)g_block;
    arr[inode_index(i)] = *in;
    return ahci_write_sector(inode_sector(i), g_block);
}

static int write_super(void) {
    uint8_t buf[NTRFS_BLOCK_SIZE];
    for (int i = 0; i < NTRFS_BLOCK_SIZE; i++) buf[i] = 0;
    ntrfs_super_t *s = (ntrfs_super_t*)buf;
    *s = g_super;
    return ahci_write_sector(0, buf);
}

int ntrfs_is_mounted(void) { return g_mounted; }

int ntrfs_format(void) {
    /* Заполняем суперблок */
    g_super.magic        = NTRFS_MAGIC;
    g_super.version      = NTRFS_VERSION;
    g_super.block_size   = NTRFS_BLOCK_SIZE;
    g_super.total_blocks = 32768;       /* 16 MiB / 512 */
    g_super.inode_count  = NTRFS_INODE_MAX;
    g_super.inode_table  = 1;           /* сектор 1 */
    g_super.data_start   = 33;          /* сектор 33 */
    g_super.free_blocks  = 32768 - 33;  /* 32735 */

    if (!write_super()) {
        loge(S_NTM, "format: write super failed");
        return 0;
    }

    /* Обнуляем inode table (32 сектора = 256 inode) */
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    for (uint32_t s = g_super.inode_table; s < g_super.data_start; s++) {
        if (!ahci_write_sector(s, zero)) {
            loge(S_NTM, "format: inode table write failed at %d", s);
            return 0;
        }
    }

    g_mounted = 1;
    logi(S_NTM, "formatted: %d inodes, %d blocks free",
         g_super.inode_count, g_super.free_blocks);
    return 1;
}

int ntrfs_mount(void) {
    uint8_t buf[NTRFS_BLOCK_SIZE];
    if (!ahci_read_sector(0, buf)) {
        loge(S_NTM, "mount: read super failed");
        return 0;
    }
    ntrfs_super_t *s = (ntrfs_super_t*)buf;
    if (s->magic != NTRFS_MAGIC) {
        loge(S_NTM, "mount: bad magic (%x)", s->magic);
        return 0;
    }
    g_super = *s;
    g_mounted = 1;
    logi(S_NTM, "mounted: %d inodes, %d blocks free",
         g_super.inode_count, g_super.free_blocks);
    return 1;
}

int ntrfs_create(const char *name, const uint8_t *data, uint32_t size) {
    if (!g_mounted) return 0;
    if (size > 255 * NTRFS_BLOCK_SIZE) {
        loge(S_NTM, "create: file too big");
        return 0;
    }

    uint32_t blocks_needed = (size + NTRFS_BLOCK_SIZE - 1) / NTRFS_BLOCK_SIZE;
    if (blocks_needed > g_super.free_blocks) {
        loge(S_NTM, "create: no free blocks");
        return 0;
    }

    /* Ищем свободный inode или перезаписываем существующий */
    uint32_t slot = NTRFS_INODE_MAX;
    uint32_t free_block = g_super.data_start;
    for (uint32_t i = 0; i < g_super.inode_count; i++) {
        ntrfs_inode_t in;
        if (!read_inode(i, &in)) break;
        if (in.type == 1) {
            int match = 1;
            for (int k = 0; k < NTRFS_NAME_MAX; k++) {
                if (in.name[k] != name[k]) { match = 0; break; }
                if (name[k] == 0) break;
            }
            if (match) {
                slot = i;
                free_block = in.first_block;
                break;
            }
        } else if (slot == NTRFS_INODE_MAX) {
            slot = i;
        }
        if (in.type == 1 && in.first_block + in.block_count > free_block)
            free_block = in.first_block + in.block_count;
    }

    if (slot == NTRFS_INODE_MAX) {
        loge(S_NTM, "create: no free inode");
        return 0;
    }

    /* Пишем данные */
    uint8_t block_buf[NTRFS_BLOCK_SIZE];
    uint32_t written = 0;
    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint32_t chunk = size - written;
        if (chunk > NTRFS_BLOCK_SIZE) chunk = NTRFS_BLOCK_SIZE;
        for (uint32_t i = 0; i < NTRFS_BLOCK_SIZE; i++)
            block_buf[i] = (i < chunk) ? data[written + i] : 0;
        if (!ahci_write_sector(free_block + b, block_buf)) {
            loge(S_NTM, "create: data write failed at %d", free_block + b);
            return 0;
        }
        written += chunk;
    }

    /* Записываем inode */
    ntrfs_inode_t in;
    for (int i = 0; i < NTRFS_NAME_MAX; i++) in.name[i] = 0;
    int i = 0;
    while (name[i] && i < NTRFS_NAME_MAX - 1) { in.name[i] = name[i]; i++; }
    in.size        = size;
    in.first_block = free_block;
    in.block_count = blocks_needed;
    in.type        = 1;
    for (int j = 0; j < 23; j++) in.reserved[j] = 0;

    if (!write_inode(slot, &in)) return 0;

    g_super.free_blocks -= blocks_needed;
    write_super();

    logi(S_NTM, "created: '%s' size=%d inode=%d block=%d",
         name, size, slot, free_block);
    return 1;
}

int ntrfs_read(const char *name, uint8_t *out, uint32_t max) {
    if (!g_mounted) return -1;

    for (uint32_t i = 0; i < g_super.inode_count; i++) {
        ntrfs_inode_t in;
        if (!read_inode(i, &in)) return -1;
        if (in.type != 1) continue;

        int match = 1;
        for (int k = 0; k < NTRFS_NAME_MAX; k++) {
            if (in.name[k] != name[k]) { match = 0; break; }
            if (name[k] == 0) break;
        }
        if (!match) continue;

        uint32_t n = in.size;
        if (n > max) n = max;
        uint32_t read_total = 0;
        uint8_t block_buf[NTRFS_BLOCK_SIZE];
        for (uint32_t b = 0; b < in.block_count && read_total < n; b++) {
            if (!ahci_read_sector(in.first_block + b, block_buf)) return -1;
            uint32_t chunk = n - read_total;
            if (chunk > NTRFS_BLOCK_SIZE) chunk = NTRFS_BLOCK_SIZE;
            for (uint32_t j = 0; j < chunk; j++)
                out[read_total + j] = block_buf[j];
            read_total += chunk;
        }
        return (int)read_total;
    }
    return -1;
}

int ntrfs_list(int idx, ntrfs_inode_t *out) {
    if (!g_mounted) return -1;
    int count = 0;
    for (uint32_t i = 0; i < g_super.inode_count; i++) {
        ntrfs_inode_t in;
        if (!read_inode(i, &in)) return -1;
        if (in.type != 1) continue;
        if (count == idx) {
            *out = in;
            return 1;
        }
        count++;
    }
    return 0;
}