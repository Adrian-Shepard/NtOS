#pragma once
#include <stdint.h>

void heap_init(void);
void *kmalloc(uint64_t size);
void  kfree(void *p);
uint64_t heap_used(void);
uint64_t heap_total(void);