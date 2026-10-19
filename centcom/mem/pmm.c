#include "pmm.h"
#include "../log/log.h"

#define MAX_PAGES   1048576                 /* 4 GB / 4 KB */
#define BITMAP_LEN  (MAX_PAGES / 8)         /* 128 KB */

static uint8_t  g_bitmap[BITMAP_LEN];
static uint64_t g_total_pages = 0;
static uint64_t g_free_pages  = 0;

static inline void bit_set(uint64_t i)   { g_bitmap[i >> 3] |=  (1 << (i & 7)); }
static inline void bit_clear(uint64_t i) { g_bitmap[i >> 3] &= ~(1 << (i & 7)); }
static inline int  bit_test(uint64_t i)  { return g_bitmap[i >> 3] & (1 << (i & 7)); }

static void mark_used(uint64_t base, uint64_t size) {
    uint64_t start = base / PAGE_SIZE;
    uint64_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t p = start + i;
        if (p >= MAX_PAGES) break;
        if (!bit_test(p)) { bit_set(p); g_free_pages--; }
    }
}

static void mark_free(uint64_t base, uint64_t size) {
    uint64_t start = base / PAGE_SIZE;
    uint64_t count = size / PAGE_SIZE;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t p = start + i;
        if (p >= MAX_PAGES) break;
        if (bit_test(p)) { bit_clear(p); g_free_pages++; }
    }
}

void pmm_init(bootinfo_t *bi) {
    for (uint64_t i = 0; i < BITMAP_LEN; i++) g_bitmap[i] = 0xFF;
    g_total_pages = 0;
    g_free_pages  = 0;

    EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)bi->mmap_addr;
    uint64_t n = bi->mmap_size / bi->mmap_desc_size;

    for (uint64_t i = 0; i < n; i++) {
        EFI_MEMORY_DESCRIPTOR *e =
            (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)d + i * bi->mmap_desc_size);

        if (e->Type == 7) {
            mark_free(e->PhysicalStart, e->NumberOfPages * PAGE_SIZE);
            uint64_t pages = e->NumberOfPages;
            if (e->PhysicalStart / PAGE_SIZE + pages > g_total_pages)
                g_total_pages = e->PhysicalStart / PAGE_SIZE + pages;
        }
    }

    mark_used(0, 0x400000);            /* первые 4 МБ — firmware */
    mark_used(0x4000000, 0x500000);    /* ядро + bootinfo */

    logi(S_PMM, "total RAM: %d MB", (g_total_pages * PAGE_SIZE) / (1024 * 1024));
    logi(S_PMM, "free pages: %d (%d MB)",
         g_free_pages, (g_free_pages * PAGE_SIZE) / (1024 * 1024));
}

uint64_t pmm_alloc(void) {
    for (uint64_t i = 0; i < MAX_PAGES; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            g_free_pages--;
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free(uint64_t phys) {
    uint64_t p = phys / PAGE_SIZE;
    if (p >= MAX_PAGES) return;
    if (bit_test(p)) { bit_clear(p); g_free_pages++; }
}

void pmm_reserve(uint64_t base, uint64_t size) {
    mark_used(base, size);
}

uint64_t pmm_total_pages(void) { return g_total_pages; }
uint64_t pmm_free_pages(void)  { return g_free_pages; }