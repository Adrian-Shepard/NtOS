#pragma once
#include "../irq/isr.h"

typedef enum {
    PANIC_CRITICAL,
    PANIC_CATASTROPHIC,
    PANIC_UNREVIVABLE,
    PANIC_MANUAL
} panic_status_t;

void panic_exception(regs_t *r);
void panic_unknown(regs_t *r);
void panic_hardware(const char *code, const char *component, const char *details);
void panic_filesystem(const char *code, const char *fs, const char *operation);
void centcom_panic(void);