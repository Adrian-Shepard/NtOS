#include "sched.h"
#include "context.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../core/gdt.h"
#include "../core/syscall.h"
#include "../elf/elf.h"
#include "../time/pit.h"
#include "../log/log.h"
#include "../log/serial.h"

#define USER_CODE_VADDR  0x0000010000000000ULL
#define USER_STACK_VADDR 0x0000020000000000ULL

extern void usermode_enter(uint64_t entry, uint64_t user_rsp);
extern uint8_t stack_top[];
extern void syscall_fork_ret(void);

static process_t  g_procs[PROC_MAX];
static process_t *g_current = 0;
static uint64_t   g_next_pid = 1;

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void irq_restore(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < PROC_NAME_LEN - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void sched_init(void) {
    for (int i = 0; i < PROC_MAX; i++) g_procs[i].state = PROC_FREE;
    process_t *m = &g_procs[0];
    m->pid              = 0;
    m->parent_pid       = 0;
    m->rsp              = 0;
    m->user_rsp         = 0;
    m->cr3              = vmm_get_current_pml4();
    m->kernel_stack_top = (uint64_t)stack_top;
    m->user_entry       = 0;
    m->stack_phys       = 0;
    m->stack_size       = 0;
    m->wake_tick        = 0;
    m->exit_code        = 0;
    m->state            = PROC_RUNNING;
    m->is_user          = 0;
    copy_name(m->name, "main");
    g_current = m;

    syscall_set_kstack((uint64_t)stack_top);
    syscall_set_user_rsp(0);

    logi(S_SCH, "scheduler initialized, main pid=0 cr3=%x", m->cr3);
}

process_t *process_current(void) { return g_current; }

int process_count(void) {
    int n = 0;
    for (int i = 0; i < PROC_MAX; i++) if (g_procs[i].state != PROC_FREE) n++;
    return n;
}

process_t *process_get(int i) {
    return (i >= 0 && i < PROC_MAX) ? &g_procs[i] : 0;
}

static process_t *alloc_slot(void) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (g_procs[i].state == PROC_FREE) return &g_procs[i];
    }
    return 0;
}

process_t *process_create_kernel(void (*entry)(void), const char *name) {
    uint64_t f = irq_save();
    process_t *p = alloc_slot();
    if (!p) { irq_restore(f); return 0; }

    uint64_t stack_phys = pmm_alloc();
    if (!stack_phys) { irq_restore(f); return 0; }

    p->pid              = g_next_pid++;
    p->parent_pid       = 0;
    p->cr3              = vmm_get_current_pml4();
    p->kernel_stack_top = stack_phys + 0x1000;
    p->user_rsp         = 0;
    p->user_entry       = 0;
    p->stack_phys       = stack_phys;
    p->stack_size       = 0x1000;
    p->wake_tick        = 0;
    p->exit_code        = 0;
    p->state            = PROC_READY;
    p->is_user          = 0;
    copy_name(p->name, name);

    uint64_t *sp = (uint64_t*)(stack_phys + 0x1000);
    *--sp = (uint64_t)process_exit;
    *--sp = (uint64_t)entry;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0x202;

    p->rsp = (uint64_t)sp;
    irq_restore(f);

    logi(S_SCH, "kernel thread: pid=%d name=%s stack=%x", p->pid, p->name, stack_phys);
    return p;
}

static uint64_t build_user_stack(uint64_t stack_phys, int argc, const char **argv) {
    uint8_t *base = (uint8_t*)stack_phys;
    uint64_t offset = 0x1000;
    uint64_t argv_ptrs[16];

    if (argc > 16) argc = 16;
    if (argc < 1) argc = 1;

    for (int i = argc - 1; i >= 0; i--) {
        const char *s = argv[i];
        uint64_t len = 0;
        while (s[len]) len++;
        offset -= (len + 1);
        uint64_t vaddr = USER_STACK_VADDR + offset;
        for (uint64_t j = 0; j <= len; j++) base[offset + j] = s[j];
        argv_ptrs[i] = vaddr;
    }

    offset &= ~7ULL;
    offset -= 8;  *(uint64_t*)(base + offset) = 0;
    for (int i = argc - 1; i >= 0; i--) {
        offset -= 8;
        *(uint64_t*)(base + offset) = argv_ptrs[i];
    }
    offset -= 8;
    *(uint64_t*)(base + offset) = (uint64_t)argc;
    return USER_STACK_VADDR + offset;
}

static void user_entry_thunk(void) {
    process_t *p = process_current();
    __asm__ volatile("sti");
    usermode_enter(p->user_entry, p->user_rsp);
    process_exit();
}

process_t *process_create_user(const uint8_t *elf, uint64_t elf_size,
                               int argc, const char **argv) {
    if (!elf || elf_size < 64) return 0;

    uint64_t new_cr3 = vmm_alloc_pml4();
    if (!new_cr3) return 0;
    uint64_t old_cr3 = vmm_get_current_pml4();
    vmm_switch(new_cr3);

    uint64_t entry = elf_load(elf, elf_size);
    if (!entry) { vmm_switch(old_cr3); return 0; }

    // ИЗМЕНЕНО: Выделяем 16 страниц под стек (64 КБ)
    uint64_t user_stack_phys = 0;
    for (int i = 0; i < 16; i++) {
        uint64_t phys = pmm_alloc();
        if (!phys) { vmm_switch(old_cr3); return 0; }
        vmm_map(USER_STACK_VADDR - (i * 0x1000), phys, PTE_PRESENT | PTE_WRITE | PTE_USER);
        if (i == 0) user_stack_phys = phys;
    }

    uint64_t user_rsp = build_user_stack(user_stack_phys, argc, argv);

    vmm_switch(old_cr3);

    uint64_t kstack_phys = pmm_alloc();
    if (!kstack_phys) return 0;

    uint64_t *sp = (uint64_t*)(kstack_phys + 0x1000);
    *--sp = (uint64_t)process_exit;
    *--sp = (uint64_t)user_entry_thunk;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0x202;
    uint64_t initial_rsp = (uint64_t)sp;

    uint64_t f = irq_save();
    process_t *p = alloc_slot();
    if (!p) { irq_restore(f); return 0; }

    p->pid              = g_next_pid++;
    p->parent_pid       = 0;
    p->rsp              = initial_rsp;
    p->user_rsp         = user_rsp;
    p->cr3              = new_cr3;
    p->kernel_stack_top = kstack_phys + 0x1000;
    p->user_entry       = entry;
    p->stack_phys       = user_stack_phys;
    p->stack_size       = 0x1000 * 16;
    p->wake_tick        = 0;
    p->exit_code        = 0;
    p->state            = PROC_READY;
    p->is_user          = 1;
    copy_name(p->name, argv[0] ? argv[0] : "user");
    irq_restore(f);

    logi(S_SCH, "user process: pid=%d name=%s entry=%x cr3=%x", p->pid, p->name, entry, new_cr3);
    return p;
}

process_t *process_fork(process_t *parent, syscall_regs_t *r) {
    uint64_t flags = irq_save();
    process_t *child = alloc_slot();
    if (!child) { irq_restore(flags); return 0; }

    child->pid = g_next_pid++;
    child->parent_pid = parent->pid;
    child->cr3 = vmm_clone_address_space(parent->cr3);
    if (!child->cr3) { irq_restore(flags); return 0; }

    uint64_t kstack_phys = pmm_alloc();
    if (!kstack_phys) { irq_restore(flags); return 0; }

    uint64_t *sp = (uint64_t*)(kstack_phys + 0x1000);

    /* 1. Сначала 15 регистров syscall_regs_t */
    sp -= 15;
    syscall_regs_t *child_regs = (syscall_regs_t*)sp;
    *child_regs = *r;
    child_regs->rax = 0;

    /* 2. Затем адрес возврата и rflags для context_switch */
    *--sp = (uint64_t)syscall_fork_ret;
    *--sp = 0x202;

    /* 3. Остальные 6 регистров, которые pop'ает context_switch */
    *--sp = 0; // rbp
    *--sp = 0; // rbx
    *--sp = 0; // r12
    *--sp = 0; // r13
    *--sp = 0; // r14
    *--sp = 0; // r15

    child->kernel_stack_top = kstack_phys + 0x1000;
    child->rsp = (uint64_t)sp;
    child->user_rsp = syscall_get_user_rsp();
    child->user_entry = parent->user_entry;
    child->stack_phys = 0;
    child->stack_size = parent->stack_size;
    child->wake_tick = 0;
    child->exit_code = 0;
    child->state = PROC_READY;
    child->is_user = 1;
    copy_name(child->name, parent->name);

    irq_restore(flags);
    logi(S_SCH, "fork: parent=%d child=%d", parent->pid, child->pid);
    return child;
}

void process_exit(void) {
    __asm__ volatile("cli");
    g_current->state = PROC_ZOMBIE;
    logi(S_SCH, "process pid=%d (%s) exited, code=%d", g_current->pid, g_current->name, g_current->exit_code);
    schedule();
    for (;;) __asm__ volatile("hlt");
}

void process_kill(uint64_t pid) {
    uint64_t f = irq_save();
    for (int i = 0; i < PROC_MAX; i++) {
        if (g_procs[i].pid == pid && g_procs[i].state != PROC_FREE && g_procs[i].state != PROC_ZOMBIE) {
            g_procs[i].exit_code = -9;
            g_procs[i].state = PROC_ZOMBIE;
            if (g_current == &g_procs[i]) {
                irq_restore(f);
                schedule();
                return;
            }
            break;
        }
    }
    irq_restore(f);
}

void process_reap(process_t *p) {
    if (!p || p->state != PROC_ZOMBIE) return;
    if (p->is_user && p->cr3) pmm_free(p->cr3);
    if (p->is_user && p->stack_phys) pmm_free(p->stack_phys);
    if (p->kernel_stack_top && p->kernel_stack_top != (uint64_t)stack_top)
        pmm_free(p->kernel_stack_top - 0x1000);
    p->state = PROC_FREE;
    p->cr3 = 0;
    p->stack_phys = 0;
    p->kernel_stack_top = 0;
    p->pid = 0;
    p->name[0] = 0;
}

void schedule(void) {
    uint64_t f = irq_save();
    int start = (int)(g_current - g_procs);
    int next = -1;
    for (int i = 1; i <= PROC_MAX; i++) {
        int idx = (start + i) % PROC_MAX;
        if (g_procs[idx].state == PROC_READY || (g_procs[idx].state == PROC_RUNNING && &g_procs[idx] != g_current)) {
            next = idx;
            break;
        }
    }

    if (next < 0 || next == start) {
        irq_restore(f);
        return;
    }

    process_t *prev  = g_current;
    process_t *nextp = &g_procs[next];

    if (prev->state == PROC_RUNNING) prev->state = PROC_READY;
    nextp->state = PROC_RUNNING;
    g_current    = nextp;

    if (nextp->kernel_stack_top) {
        tss_set_rsp0(nextp->kernel_stack_top);
        syscall_set_kstack(nextp->kernel_stack_top);
    }

    prev->user_rsp = syscall_get_user_rsp();
    syscall_set_user_rsp(nextp->user_rsp);

    irq_restore(f);
    __asm__ volatile("cli");

    if (prev->cr3 != nextp->cr3) {
        vmm_switch(nextp->cr3);
    }
    
    context_switch(&prev->rsp, nextp->rsp);
}

void sched_tick(void) {
    uint64_t now = pit_ticks();
    for (int i = 0; i < PROC_MAX; i++) {
        if (g_procs[i].state == PROC_BLOCKED) {
            if (g_procs[i].wake_tick > 0 && now >= g_procs[i].wake_tick) {
                g_procs[i].wake_tick = 0;
                g_procs[i].state = PROC_READY;
            }
        }
    }
    if (g_current->state == PROC_RUNNING) schedule();
}