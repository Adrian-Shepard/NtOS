#include "syscall.h"
#include "usermode.h"
#include "../log/log.h"
#include "../log/serial.h"
#include "../tty/fb.h"
#include "../fs/ntmem_fs.h"
#include "../fs/ntrfs.h"
#include "../sched/sched.h"
#include "../time/pit.h"
#include "../driver/sb16.h"
#include "../driver/keyboard.h"
#include "../store/kv.h"
#include "../mem/vmm.h"
#include "../mem/pmm.h"
#include "../elf/elf.h"

#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

#define FD_MAX 32
#define USER_CODE_VADDR  0x0000010000000000ULL
#define USER_STACK_VADDR 0x0000020000000000ULL

extern void syscall_entry(void);
extern uint64_t user_rsp_save;

// Для паники
int g_last_syscall = -1;
uint64_t g_last_syscall_arg = 0;

uint64_t g_current_kstack_top = 0;

uint64_t syscall_get_kstack(void) { return g_current_kstack_top; }
void     syscall_set_kstack(uint64_t top) { g_current_kstack_top = top; }
uint64_t syscall_get_user_rsp(void) { return user_rsp_save; }
void     syscall_set_user_rsp(uint64_t rsp) { user_rsp_save = rsp; }

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

typedef struct {
    ntmem_file_t *file;
    uint64_t      offset;
    uint8_t       used;
} fd_t;

static fd_t g_fds[FD_MAX];

static int is_user_ptr(uint64_t addr, uint64_t len) {
    if (len == 0) return 1;
    uint64_t end = addr + len;
    if (end < addr) return 0;
    if (addr >= USER_CODE_VADDR && end <= USER_STACK_VADDR + 0x1000) return 1;
    return 0;
}

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    (void)fd;
    if (!is_user_ptr(buf, len)) return (uint64_t)-1;
    const char *s = (const char*)buf;
    for (uint64_t i = 0; i < len; i++) fb_putc(s[i], 0x00E0E0E0, 0x00101018);
    return len;
}

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd == 0) {
        if (!is_user_ptr(buf, len)) return (uint64_t)-1;
        char *dst = (char*)buf;
        int n = 0;
        while (n < (int)len) {
            int c = keyboard_getchar();
            if (c == 0) {
                __asm__ volatile("sti; hlt; cli");
                continue;
            }
            dst[n++] = (char)c;
            if (c == '\n') break;
        }
        return n;
    }
    if (fd >= FD_MAX || !g_fds[fd].used) return (uint64_t)-1;
    if (!is_user_ptr(buf, len)) return (uint64_t)-1;
    ntmem_file_t *f = g_fds[fd].file;
    uint64_t remain = f->size - g_fds[fd].offset;
    if (remain == 0) return 0;
    if (len > remain) len = remain;
    const uint8_t *src = f->data + g_fds[fd].offset;
    uint8_t *dst = (uint8_t*)buf;
    for (uint64_t i = 0; i < len; i++) dst[i] = src[i];
    g_fds[fd].offset += len;
    return len;
}

static uint64_t sys_open(uint64_t name_ptr, uint64_t flags) {
    (void)flags;
    if (!is_user_ptr(name_ptr, 1)) return (uint64_t)-1;
    const char *name = (const char*)name_ptr;
    uint64_t n = 0;
    while (name[n] && n < 256) n++;
    if (!is_user_ptr(name_ptr, n + 1)) return (uint64_t)-1;
    ntmem_file_t *f = ntmem_fs_find(name);
    if (!f) return (uint64_t)-1;
    for (int i = 3; i < FD_MAX; i++) {
        if (!g_fds[i].used) {
            g_fds[i].file = f;
            g_fds[i].offset = 0;
            g_fds[i].used = 1;
            return (uint64_t)i;
        }
    }
    return (uint64_t)-1;
}

static uint64_t sys_close(uint64_t fd) {
    if (fd >= FD_MAX || !g_fds[fd].used) return (uint64_t)-1;
    g_fds[fd].used = 0;
    g_fds[fd].file = 0;
    g_fds[fd].offset = 0;
    return 0;
}

static uint64_t sys_list(uint64_t idx, uint64_t buf, uint64_t max) {
    int n = ntmem_fs_count();
    if ((int)idx >= n) return 0;
    ntmem_file_t *f = ntmem_fs_get((int)idx);
    if (!f) return 0;
    uint64_t name_len = 0;
    while (f->name[name_len]) name_len++;
    if (name_len + 1 > max) return (uint64_t)-1;
    if (!is_user_ptr(buf, name_len + 1)) return (uint64_t)-1;
    char *dst = (char*)buf;
    for (uint64_t i = 0; i <= name_len; i++) dst[i] = f->name[i];
    return name_len;
}

static uint64_t sys_sleep(uint64_t ms) {
    process_t *p = process_current();
    if (!p || !p->is_user) return 0;
    p->wake_tick = pit_ticks() + (ms / 10) + 1;
    p->state = PROC_BLOCKED;
    schedule();
    return 0;
}

static uint64_t sys_play_pcm(uint64_t buf, uint64_t samples) {
    if (!is_user_ptr(buf, samples)) return (uint64_t)-1;
    if (samples > 32768) samples = 32768;
    sb16_stop();
    int ok = sb16_play_pcm_async((const uint8_t*)buf, (uint32_t)samples);
    return ok ? samples : 0;
}

static uint64_t sys_kv_write(uint64_t key_ptr, uint64_t data_ptr, uint64_t len) {
    if (!is_user_ptr(key_ptr, 1)) return (uint64_t)-1;
    if (!is_user_ptr(data_ptr, len)) return (uint64_t)-1;
    const char *key = (const char*)key_ptr;
    uint64_t kn = 0;
    while (key[kn] && kn < 256) kn++;
    if (!is_user_ptr(key_ptr, kn + 1)) return (uint64_t)-1;
    return (uint64_t)kv_write(key, (const uint8_t*)data_ptr, len);
}

static uint64_t sys_kv_read(uint64_t key_ptr, uint64_t out_ptr, uint64_t max) {
    if (!is_user_ptr(key_ptr, 1)) return (uint64_t)-1;
    if (!is_user_ptr(out_ptr, max)) return (uint64_t)-1;
    const char *key = (const char*)key_ptr;
    uint64_t kn = 0;
    while (key[kn] && kn < 256) kn++;
    if (!is_user_ptr(key_ptr, kn + 1)) return (uint64_t)-1;
    return (uint64_t)kv_read(key, (uint8_t*)out_ptr, max);
}

static uint64_t sys_ntrfs_list(uint64_t idx, uint64_t out_ptr) {
    if (!is_user_ptr(out_ptr, sizeof(ntrfs_inode_t))) return (uint64_t)-1;
    ntrfs_inode_t *out = (ntrfs_inode_t*)out_ptr;
    return (uint64_t)ntrfs_list((int)idx, out);
}

static uint64_t sys_ntrfs_read(uint64_t name_ptr, uint64_t buf_ptr, uint64_t max) {
    if (!is_user_ptr(name_ptr, 1)) return (uint64_t)-1;
    const char *name = (const char*)name_ptr;
    uint64_t n = 0;
    while (name[n] && n < 256) n++;
    if (!is_user_ptr(name_ptr, n + 1)) return (uint64_t)-1;
    if (!is_user_ptr(buf_ptr, max)) return (uint64_t)-1;
    return (uint64_t)ntrfs_read(name, (uint8_t*)buf_ptr, (uint32_t)max);
}

static uint64_t sys_ntrfs_write(uint64_t name_ptr, uint64_t buf_ptr, uint64_t len) {
    if (!is_user_ptr(name_ptr, 1)) return (uint64_t)-1;
    const char *name = (const char*)name_ptr;
    uint64_t n = 0;
    while (name[n] && n < 256) n++;
    if (!is_user_ptr(name_ptr, n + 1)) return (uint64_t)-1;
    if (!is_user_ptr(buf_ptr, len)) return (uint64_t)-1;
    return (uint64_t)ntrfs_create(name, (const uint8_t*)buf_ptr, (uint32_t)len);
}

static uint64_t exec_build_user_stack(uint64_t stack_phys) {
    uint8_t *base = (uint8_t*)stack_phys;
    uint64_t offset = 0x1000;
    const char *name = "user";
    uint64_t len = 4;
    offset -= (len + 1);
    uint64_t name_vaddr = USER_STACK_VADDR + offset;
    for (uint64_t i = 0; i <= len; i++) base[offset + i] = name[i];
    offset &= ~7ULL;
    offset -= 8;  *(uint64_t*)(base + offset) = 0;
    offset -= 8;  *(uint64_t*)(base + offset) = name_vaddr;
    offset -= 8;  *(uint64_t*)(base + offset) = 1;
    return USER_STACK_VADDR + offset;
}

static uint64_t sys_exec_do(const uint8_t *elf, uint64_t elf_size) {
    process_t *p = process_current();
    if (!p || !p->is_user) return 0;

    uint64_t new_cr3 = vmm_alloc_pml4();
    if (!new_cr3) return 0;
    
    uint64_t old_cr3 = vmm_get_current_pml4();
    vmm_switch(new_cr3);

    uint64_t entry = elf_load(elf, elf_size);
    if (!entry) {
        vmm_switch(old_cr3);
        return 0;
    }

    uint64_t new_stack_phys = pmm_alloc();
    if (!new_stack_phys) {
        vmm_switch(old_cr3);
        return 0;
    }
    vmm_map(USER_STACK_VADDR, new_stack_phys, PTE_PRESENT | PTE_WRITE | PTE_USER);
    
    uint64_t new_rsp = exec_build_user_stack(new_stack_phys);

    if (p->cr3) pmm_free(p->cr3);
    if (p->stack_phys) pmm_free(p->stack_phys);

    p->cr3 = new_cr3;
    p->user_entry = entry;
    p->user_rsp = new_rsp;
    p->stack_phys = new_stack_phys;

    syscall_set_user_rsp(new_rsp);
    return entry;
}

static uint64_t sys_exec(uint64_t path_ptr) {
    if (!is_user_ptr(path_ptr, 1)) return 0;
    const char *name = (const char*)path_ptr;
    uint64_t n = 0; while (name[n] && n < 256) n++;
    if (!is_user_ptr(path_ptr, n + 1)) return 0;

    ntmem_file_t *f = ntmem_fs_find(name);
    if (f) return sys_exec_do(f->data, f->size);

    static uint8_t elf_buf[65536];
    int r = ntrfs_read(name, elf_buf, sizeof(elf_buf));
    if (r > 0) return sys_exec_do(elf_buf, r);

    return 0;
}

static uint64_t sys_fork(syscall_regs_t *r) {
    process_t *parent = process_current();
    if (!parent || !parent->is_user) return (uint64_t)-1;
    process_t *child = process_fork(parent, r);
    if (!child) return (uint64_t)-1;
    return child->pid;
}

static uint64_t sys_wait(uint64_t pid, uint64_t status_ptr) {
    process_t *parent = process_current();
    if (!parent || !parent->is_user) return (uint64_t)-1;
    while (1) {
        int found = 0;
        for (int i = 0; i < PROC_MAX; i++) {
            process_t *p = process_get(i);
            if (p && p->state != PROC_FREE && p->parent_pid == parent->pid) {
                if (pid == (uint64_t)-1 || p->pid == pid) {
                    found = 1;
                    if (p->state == PROC_ZOMBIE) {
                        int code = p->exit_code;
                        uint64_t child_pid = p->pid;
                        process_reap(p);
                        if (status_ptr && is_user_ptr(status_ptr, 4)) {
                            *(int*)status_ptr = code;
                        }
                        return child_pid;
                    }
                }
            }
        }
        if (!found && pid != (uint64_t)-1) return (uint64_t)-1;
        parent->state = PROC_BLOCKED;
        schedule();
    }
}

uint64_t syscall_dispatch(syscall_regs_t *r) {
    // Запоминаем сисколл для паники
    g_last_syscall = (int)r->rax;
    g_last_syscall_arg = r->rdi;

    switch (r->rax) {
        case SYS_EXIT: {
            process_t *p = process_current();
            if (p && p->is_user) {
                p->exit_code = (int)r->rdi;
                p->state = PROC_ZOMBIE;
                __asm__ volatile("cli");
                schedule();
                for (;;) __asm__ volatile("hlt");
            }
            g_user_exit = (int)r->rdi;
            if (g_user_active) user_longjmp(&g_user_ctx, 1);
            for (;;) __asm__ volatile("hlt");
        }

        case SYS_EXEC: {
            uint64_t entry = sys_exec(r->rdi);
            if (entry != 0) {
                r->rcx = entry;
                r->r11 = 0x202;
                return 0;
            }
            return (uint64_t)-1;
        }

        case SYS_FORK:      return sys_fork(r);
        case SYS_WAIT:      return sys_wait(r->rdi, r->rsi);
        case SYS_WRITE:     return sys_write(r->rdi, r->rsi, r->rdx);
        case SYS_OPEN:      return sys_open(r->rdi, r->rsi);
        case SYS_READ:      return sys_read(r->rdi, r->rsi, r->rdx);
        case SYS_CLOSE:     return sys_close(r->rdi);
        case SYS_LIST:      return sys_list(r->rdi, r->rsi, r->rdx);
        case SYS_SLEEP:     return sys_sleep(r->rdi);
        case SYS_PLAY_PCM:  return sys_play_pcm(r->rdi, r->rsi);
        case SYS_KV_WRITE:  return sys_kv_write(r->rdi, r->rsi, r->rdx);
        case SYS_KV_READ:   return sys_kv_read(r->rdi, r->rsi, r->rdx);
        case SYS_NTRFS_LIST: return sys_ntrfs_list(r->rdi, r->rsi);
        case SYS_NTRFS_READ: return sys_ntrfs_read(r->rdi, r->rsi, r->rdx);
        case SYS_NTRFS_WRITE:return sys_ntrfs_write(r->rdi, r->rsi, r->rdx);

        default:
            logw(S_SYC, "unknown syscall %d", r->rax);
            return (uint64_t)-1;
    }
}

void syscall_init(void) {
    for (int i = 0; i < FD_MAX; i++) g_fds[i].used = 0;

    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);

    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x200);

    logi(S_SYC, "syscall enabled: STAR=%x LSTAR=%x", star, (uint64_t)syscall_entry);
}