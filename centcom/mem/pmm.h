#pragma once
#include <stdint.h>
#include "../../bootinfo.h"

#define PAGE_SIZE 4096

void     pmm_init(bootinfo_t *bi);
uint64_t pmm_alloc(void);
void     pmm_free(uint64_t phys);
void     pmm_reserve(uint64_t base, uint64_t size);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);