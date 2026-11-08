#include "heap.h"
#include "pmm.h"
#include "../log/log.h"

#define HEAP_START 0x5000000ULL
#define HEAP_SIZE  (16ULL * 1024 * 1024)
#define MIN_BLOCK  32

typedef struct block {
    uint64_t size;       // размер данных (без заголовка)
    struct block *next;  // следующий свободный блок
    uint8_t  free;       // 1 = свободен, 0 = занят
} block_t;

#define HDR_SIZE 24      // sizeof(block_t) с выравниванием
#define ALIGN16(x) (((x) + 15) & ~15ULL)

static uint64_t g_heap_start;
static uint64_t g_heap_end;
static uint64_t g_heap_cur;
static block_t *g_free_list = 0;

void heap_init(void) {
    pmm_reserve(HEAP_START, HEAP_SIZE);
    g_heap_start = HEAP_START;
    g_heap_end   = HEAP_START + HEAP_SIZE;
    g_heap_cur   = HEAP_START;
    g_free_list  = 0;
    logi(S_HEP, "heap ready: %x..%x (%d MB) [free-list]",
         g_heap_start, g_heap_end, HEAP_SIZE / (1024 * 1024));
}

// Разбить блок на два, если свободного места много
static void split_block(block_t *b, uint64_t need) {
    if (b->size < need + HDR_SIZE + MIN_BLOCK) return;
    block_t *nb = (block_t*)((uint8_t*)b + HDR_SIZE + need);
    nb->size = b->size - need - HDR_SIZE;
    nb->next = b->next;
    nb->free = 1;
    b->size  = need;
    b->next  = nb;
}

void *kmalloc(uint64_t size) {
    if (size == 0) return 0;
    size = ALIGN16(size);

    // 1. Поиск в free-list (first-fit)
    block_t *prev = 0;
    block_t *cur  = g_free_list;
    while (cur) {
        if (cur->size >= size) {
            if (prev) prev->next = cur->next;
            else      g_free_list = cur->next;
            cur->free = 0;
            cur->next = 0;
            split_block(cur, size);
            return (uint8_t*)cur + HDR_SIZE;
        }
        prev = cur;
        cur  = cur->next;
    }

    // 2. Новый блок из кучи (bump)
    uint64_t need = HDR_SIZE + size;
    if (g_heap_cur + need > g_heap_end) {
        loge(S_HEP, "kmalloc: out of memory (want %d, have %d)",
             size, g_heap_end - g_heap_cur);
        return 0;
    }

    block_t *b = (block_t*)g_heap_cur;
    b->size = size;
    b->free = 0;
    b->next = 0;
    g_heap_cur += need;
    return (uint8_t*)b + HDR_SIZE;
}

void kfree(void *p) {
    if (!p) return;
    block_t *b = (block_t*)((uint8_t*)p - HDR_SIZE);
    if (b->free) return;   // double-free защита
    b->free = 1;

    // Возврат в free-list (сортированно по адресу, чтобы потом coalesce)
    block_t *prev = 0;
    block_t *cur  = g_free_list;
    while (cur && (uint8_t*)cur < (uint8_t*)b) {
        prev = cur;
        cur  = cur->next;
    }
    b->next = cur;
    if (prev) prev->next = b;
    else      g_free_list = b;

    // Coalesce с соседом справа
    if (b->next && (uint8_t*)b + HDR_SIZE + b->size == (uint8_t*)b->next) {
        b->size += HDR_SIZE + b->next->size;
        b->next  = b->next->next;
    }
    // Coalesce с соседом слева
    if (prev && (uint8_t*)prev + HDR_SIZE + prev->size == (uint8_t*)b) {
        prev->size += HDR_SIZE + b->size;
        prev->next  = b->next;
    }
}

uint64_t heap_used(void)  { return g_heap_cur - g_heap_start; }
uint64_t heap_total(void) { return HEAP_SIZE; }