#include "idt.h"
#include "../log/log.h"

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry g_idt[256];
static struct idt_ptr   g_idtp;

extern uint64_t isr_stub_table[256];
extern void idt_flush(struct idt_ptr*);

static void set_gate(int n, uint64_t handler) {
    g_idt[n].offset_lo  = handler & 0xFFFF;
    g_idt[n].selector   = 0x08;
    g_idt[n].ist        = 0;
    g_idt[n].type_attr  = 0x8E;
    g_idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    g_idt[n].offset_hi  = (handler >> 32) & 0xFFFFFFFF;
    g_idt[n].zero       = 0;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        set_gate(i, isr_stub_table[i]);

    /* int3 доступен из ring3 — DPL=3 (0xEE вместо 0x8E) */
    g_idt[3].type_attr = 0xEE;

    g_idtp.limit = sizeof(g_idt) - 1;
    g_idtp.base  = (uint64_t)&g_idt;

    idt_flush(&g_idtp);
    logi(S_CPU, "IDT loaded: 256 vectors (int3 DPL=3)");
}
uint32_t idt_count(void) {
    return 256;
}