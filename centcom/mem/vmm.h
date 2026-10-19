#pragma once
#include <stdint.h>
#include "../../bootinfo.h"

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_HUGE     (1ULL << 7)
#define PTE_NX       (1ULL << 63)

void     vmm_init(bootinfo_t *bi);
void     vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
uint64_t vmm_get_cr3(void);

uint64_t vmm_alloc_pml4(void);
void     vmm_switch(uint64_t pml4_phys);
uint64_t vmm_get_current_pml4(void);
uint64_t vmm_clone_address_space(uint64_t src_cr3);