#pragma once
#include <stdint.h>
#include "../core/syscall.h"

#define PROC_READY     0
#define PROC_RUNNING   1
#define PROC_BLOCKED   2
#define PROC_ZOMBIE    3
#define PROC_FREE      4

#define PROC_MAX       16
#define PROC_NAME_LEN  32

typedef struct process {
    uint64_t pid;
    uint64_t parent_pid;
    uint64_t rsp;
    uint64_t user_rsp;
    uint64_t cr3;
    uint64_t kernel_stack_top;
    uint64_t user_entry;
    uint64_t stack_phys;
    uint64_t stack_size;
    uint64_t wake_tick;
    int      exit_code;
    uint8_t  state;
    uint8_t  is_user;
    char     name[PROC_NAME_LEN];
} process_t;

void        sched_init(void);
process_t  *process_create_kernel(void (*entry)(void), const char *name);
process_t  *process_create_user(const uint8_t *elf, uint64_t elf_size, int argc, const char **argv);
process_t  *process_fork(process_t *parent, syscall_regs_t *r);
void        process_exit(void) __attribute__((noreturn));
void        process_kill(uint64_t pid);
void        process_reap(process_t *p);
void        sched_tick(void);
void        schedule(void);

process_t  *process_current(void);
int         process_count(void);
process_t  *process_get(int i);