#include "vmm.h"
#include "pmm.h"
#include "../log/log.h"

static uint64_t *g_pml4;
static uint64_t  g_pml4_phys;
static uint64_t *g_active_pml4;
static uint64_t  g_active_cr3;

static inline uint64_t *phys_to_virt(uint64_t p) { return (uint64_t*)p; }

static inline void zero_page(uint64_t phys) {
    uint64_t *p = phys_to_virt(phys);
    for (int i = 0; i < 512; i++) p[i] = 0;
}

static uint64_t *get_or_alloc(uint64_t *table, uint64_t idx) {
    if (!(table[idx] & PTE_PRESENT)) {
        uint64_t p = pmm_alloc();
        if (!p) return 0;
        zero_page(p);
        table[idx] = p | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }
    return phys_to_virt(table[idx] & ~0xFFFULL);
}

uint64_t vmm_get_cr3(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

uint64_t vmm_get_current_pml4(void) { return g_active_cr3; }

void vmm_switch(uint64_t pml4_phys) {
    g_active_cr3  = pml4_phys;
    g_active_pml4 = phys_to_virt(pml4_phys);
    __asm__ volatile("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

void vmm_init(bootinfo_t *bi) {
    (void)bi;

    logi(S_VMM, "old CR3 = %x", vmm_get_cr3());

    g_pml4_phys = pmm_alloc();
    if (!g_pml4_phys) { loge(S_VMM, "cannot allocate PML4"); return; }
    zero_page(g_pml4_phys);
    g_pml4 = phys_to_virt(g_pml4_phys);

    for (uint64_t addr = 0; addr < 0x100000000ULL; addr += 0x200000ULL) {
        uint64_t pml4_i = (addr >> 39) & 0x1FF;
        uint64_t pdpt_i = (addr >> 30) & 0x1FF;
        uint64_t pd_i   = (addr >> 21) & 0x1FF;

        uint64_t *pdpt = get_or_alloc(g_pml4, pml4_i);
        if (!pdpt) { loge(S_VMM, "pdpt alloc failed at %x", addr); return; }
        uint64_t *pd = get_or_alloc(pdpt, pdpt_i);
        if (!pd) { loge(S_VMM, "pd alloc failed at %x", addr); return; }

        pd[pd_i] = addr | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
    }

    g_active_pml4 = g_pml4;
    g_active_cr3  = g_pml4_phys;

    __asm__ volatile("mov %0, %%cr3" :: "r"(g_pml4_phys) : "memory");

    logi(S_VMM, "new CR3 = %x", g_pml4_phys);
}

uint64_t vmm_alloc_pml4(void) {
    uint64_t p = pmm_alloc();
    if (!p) return 0;
    uint64_t *npml4 = phys_to_virt(p);
    for (int i = 0; i < 512; i++) npml4[i] = 0;
    npml4[0] = g_pml4[0];
    return p;
}

void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_alloc(g_active_pml4, pml4_i);
    if (!pdpt) return;
    uint64_t *pd   = get_or_alloc(pdpt, pdpt_i);
    if (!pd) return;
    uint64_t *pt   = get_or_alloc(pd, pd_i);
    if (!pt) return;

    pt[pt_i] = (phys & ~0xFFFULL) | flags | PTE_PRESENT;

    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap(uint64_t virt) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    if (!(g_active_pml4[pml4_i] & PTE_PRESENT)) return;
    uint64_t *pdpt = phys_to_virt(g_active_pml4[pml4_i] & ~0xFFFULL);
    if (!(pdpt[pdpt_i] & PTE_PRESENT)) return;
    uint64_t *pd = phys_to_virt(pdpt[pdpt_i] & ~0xFFFULL);
    if (!(pd[pd_i] & PTE_PRESENT)) return;
    uint64_t *pt = phys_to_virt(pd[pd_i] & ~0xFFFULL);

    pt[pt_i] = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    if (!(g_active_pml4[pml4_i] & PTE_PRESENT)) return 0;
    uint64_t *pdpt = phys_to_virt(g_active_pml4[pml4_i] & ~0xFFFULL);
    if (!(pdpt[pdpt_i] & PTE_PRESENT)) return 0;

    if (pdpt[pdpt_i] & PTE_HUGE) {
        return (pdpt[pdpt_i] & ~0x3FFFFFFFULL) | (virt & 0x3FFFFFFF);
    }

    uint64_t *pd = phys_to_virt(pdpt[pdpt_i] & ~0xFFFULL);
    if (!(pd[pd_i] & PTE_PRESENT)) return 0;

    if (pd[pd_i] & PTE_HUGE) {
        return (pd[pd_i] & ~0x1FFFFFULL) | (virt & 0x1FFFFF);
    }

    uint64_t *pt = phys_to_virt(pd[pd_i] & ~0xFFFULL);
    if (!(pt[pt_i] & PTE_PRESENT)) return 0;
    return (pt[pt_i] & ~0xFFFULL) | (virt & 0xFFF);
}

uint64_t vmm_clone_address_space(uint64_t src_cr3) {
    uint64_t dst_cr3 = vmm_alloc_pml4();
    if (!dst_cr3) return 0;
    
    uint64_t *src_pml4 = (uint64_t*)src_cr3;
    uint64_t *dst_pml4 = (uint64_t*)dst_cr3;

    dst_pml4[0] = src_pml4[0];

    for (int i = 1; i < 512; i++) {
        if (src_pml4[i] & PTE_PRESENT) {
            uint64_t dst_pdpt_phys = pmm_alloc();
            if (!dst_pdpt_phys) return 0;
            zero_page(dst_pdpt_phys);
            dst_pml4[i] = dst_pdpt_phys | (src_pml4[i] & 0xFFF);
            
            uint64_t *src_pdpt = (uint64_t*)(src_pml4[i] & ~0xFFF);
            uint64_t *dst_pdpt = (uint64_t*)dst_pdpt_phys;
            
            for (int j = 0; j < 512; j++) {
                if (src_pdpt[j] & PTE_PRESENT) {
                    uint64_t dst_pd_phys = pmm_alloc();
                    if (!dst_pd_phys) return 0;
                    zero_page(dst_pd_phys);
                    dst_pdpt[j] = dst_pd_phys | (src_pdpt[j] & 0xFFF);
                    
                    uint64_t *src_pd = (uint64_t*)(src_pdpt[j] & ~0xFFF);
                    uint64_t *dst_pd = (uint64_t*)dst_pd_phys;
                    
                    for (int k = 0; k < 512; k++) {
                        if (src_pd[k] & PTE_PRESENT) {
                            if (src_pd[k] & PTE_HUGE) continue;
                            
                            uint64_t dst_pt_phys = pmm_alloc();
                            if (!dst_pt_phys) return 0;
                            zero_page(dst_pt_phys);
                            dst_pd[k] = dst_pt_phys | (src_pd[k] & 0xFFF);
                            
                            uint64_t *src_pt = (uint64_t*)(src_pd[k] & ~0xFFF);
                            uint64_t *dst_pt = (uint64_t*)dst_pt_phys;
                            
                            for (int l = 0; l < 512; l++) {
                                if (src_pt[l] & PTE_PRESENT) {
                                    uint64_t new_page = pmm_alloc();
                                    if (!new_page) return 0;
                                    zero_page(new_page);
                                    uint8_t *src_data = (uint8_t*)(src_pt[l] & ~0xFFF);
                                    uint8_t *dst_data = (uint8_t*)new_page;
                                    for (int m = 0; m < 4096; m++) dst_data[m] = src_data[m];
                                    dst_pt[l] = new_page | (src_pt[l] & 0xFFF);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return dst_cr3;
}