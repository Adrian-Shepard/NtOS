#pragma once
#include <stdint.h>

typedef struct {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
} jmp_buf_t;

extern void user_setjmp(jmp_buf_t *b);
extern void user_longjmp(jmp_buf_t *b, int val);

extern jmp_buf_t g_user_ctx;
extern int       g_user_active;
extern int       g_user_exit;

/* запуск ELF с аргументами */
void usermode_run_elf(const uint8_t *data, uint64_t size,
                      int argc, const char **argv);

/* быстрый запуск hello.elf из ntmem_fs без аргументов */
void usermode_run_init(void);