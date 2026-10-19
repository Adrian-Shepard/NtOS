#pragma once
#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
} regs_t;

typedef void (*irq_handler_t)(regs_t *r);

void irq_register(int irq, irq_handler_t h);
void isr_dispatch(regs_t *r);
void isr_irq_init(void);