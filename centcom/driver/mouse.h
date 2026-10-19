#pragma once
#include <stdint.h>
#include "../irq/isr.h" // <-- Добавлено для regs_t

typedef struct {
    int x;
    int y;
    int left;
    int right;
    int middle;
} mouse_state_t;

void mouse_init(void);
void mouse_irq(regs_t *r); // <-- ИЗМЕНЕНО
mouse_state_t mouse_get_state(void);