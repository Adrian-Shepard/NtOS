#pragma once
#include <stdint.h>

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
} syscall_regs_t;

void     syscall_init(void);
uint64_t syscall_dispatch(syscall_regs_t *r);

#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_READ        3
#define SYS_CLOSE       4
#define SYS_LIST        5
#define SYS_SLEEP       6
#define SYS_PLAY_PCM    7
#define SYS_KV_WRITE    8
#define SYS_KV_READ     9
#define SYS_NTRFS_LIST  10
#define SYS_NTRFS_READ  11
#define SYS_NTRFS_WRITE 12
#define SYS_EXEC        13
#define SYS_FORK        14
#define SYS_WAIT        15

/* Для планировщика (per-process стеки) */
uint64_t syscall_get_kstack(void);
void     syscall_set_kstack(uint64_t top);

uint64_t syscall_get_user_rsp(void);
void     syscall_set_user_rsp(uint64_t rsp);