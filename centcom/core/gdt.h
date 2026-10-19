#pragma once
#include <stdint.h>

void     gdt_init(void);
void     tss_set_rsp0(uint64_t rsp0);
uint32_t gdt_count(void);       /* реальное число записей в GDT */
uint64_t gdt_tss_base(void);    /* физический адрес TSS */