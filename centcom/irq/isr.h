#pragma once
#include <stdint.h>

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
} regs_t;

typedef void (*irq_handler_t)(regs_t *r);

void irq_register(int irq, irq_handler_t h);
void isr_dispatch(regs_t *r);
void isr_irq_init(void);