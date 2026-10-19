#include "heap.h"
#include "pmm.h"
#include "../log/log.h"

#define HEAP_START 0x5000000ULL          /* 80 MB, identity-mapped */
#define HEAP_SIZE  (16ULL * 1024 * 1024) /* 16 MB */

static uint64_t g_heap_start;
static uint64_t g_heap_end;
static uint64_t g_heap_cur;

void heap_init(void) {
    /* Занимаем физические страницы под heap, чтобы PMM их не отдал другим */
    pmm_reserve(HEAP_START, HEAP_SIZE);

    g_heap_start = HEAP_START;
    g_heap_end   = HEAP_START + HEAP_SIZE;
    g_heap_cur   = HEAP_START;

    logi(S_HEP, "heap ready: %x..%x (%d MB)",
         g_heap_start, g_heap_end, HEAP_SIZE / (1024 * 1024));
}

void *kmalloc(uint64_t size) {
    if (size == 0) return 0;

    /* выравнивание на 16 байт */
    uint64_t aligned = (g_heap_cur + 15) & ~15ULL;

    if (aligned + size > g_heap_end) {
        loge(S_HEP, "out of memory (want %d, have %d)",
             size, g_heap_end - aligned);
        return 0;
    }

    void *p = (void*)aligned;
    g_heap_cur = aligned + size;
    return p;
}

void kfree(void *p) {
    /* bump-allocator: free — no-op. Реализация позже, в slab. */
    (void)p;
}

uint64_t heap_used(void)  { return g_heap_cur - g_heap_start; }
uint64_t heap_total(void) { return HEAP_SIZE; }