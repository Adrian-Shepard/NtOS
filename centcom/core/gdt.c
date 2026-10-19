#include "gdt.h"
#include "../log/log.h"

struct gdt_entry8 {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed));

struct gdt_entry16 {
    struct gdt_entry8 low;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_table {
    struct gdt_entry8  e0_null;
    struct gdt_entry8  e1_kcode;   /* 0x08 */
    struct gdt_entry8  e2_kdata;   /* 0x10 */
    struct gdt_entry8  e3_udata;   /* 0x18 — user data СНАЧАЛА */
    struct gdt_entry8  e4_ucode;   /* 0x20 — user code ПОСЛЕ */
    struct gdt_entry16 e5_tss;     /* 0x28 */
} __attribute__((packed));

static struct gdt_table g_gdt;
static struct gdt_ptr   g_gdtp;
static struct tss_entry g_tss;

extern void gdt_flush(struct gdt_ptr*);

static void set8(struct gdt_entry8 *e, uint32_t base, uint32_t limit,
                 uint8_t access, uint8_t gran) {
    e->limit_lo = limit & 0xFFFF;
    e->base_lo  = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->access   = access;
    e->gran     = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    e->base_hi  = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    set8(&g_gdt.e0_null,  0, 0,       0x00, 0x00);
    set8(&g_gdt.e1_kcode, 0, 0xFFFFF, 0x9A, 0xA0);
    set8(&g_gdt.e2_kdata, 0, 0xFFFFF, 0x92, 0xC0);
    set8(&g_gdt.e3_udata, 0, 0xFFFFF, 0xF2, 0xC0);
    set8(&g_gdt.e4_ucode, 0, 0xFFFFF, 0xFA, 0xA0);

    for (unsigned i = 0; i < sizeof(g_tss); i++) ((uint8_t*)&g_tss)[i] = 0;
    g_tss.iomap_base = sizeof(g_tss);

    uint64_t tss_a = (uint64_t)&g_tss;
    uint32_t tss_lim = sizeof(g_tss) - 1;
    set8(&g_gdt.e5_tss.low, (uint32_t)tss_a, tss_lim, 0x89, 0x00);
    g_gdt.e5_tss.base_upper = (uint32_t)(tss_a >> 32);
    g_gdt.e5_tss.reserved   = 0;

    g_gdtp.limit = sizeof(g_gdt) - 1;
    g_gdtp.base  = (uint64_t)&g_gdt;

    gdt_flush(&g_gdtp);
    logi(S_CPU, "GDT loaded: 7 entries, TSS at %x", tss_a);
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}
uint32_t gdt_count(void) {
    return 7;   /* мы знаем точно: null, kcode, kdata, udata, ucode, tss_lo, tss_hi */
}

uint64_t gdt_tss_base(void) {
    return (uint64_t)&g_tss;
}