#include "usermode.h"
#include "gdt.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../elf/elf.h"
#include "../fs/ntmem_fs.h"
#include "../log/log.h"

#define USER_CODE_VADDR  0x0000010000000000ULL
#define USER_STACK_VADDR 0x0000020000000000ULL
#define USER_STACK_SIZE  0x1000

extern void usermode_enter(uint64_t entry, uint64_t user_rsp);

jmp_buf_t g_user_ctx;
int       g_user_active = 0;
int       g_user_exit   = 0;

static uint8_t irq_stack[16384] __attribute__((aligned(16)));

/* физический адрес user-stack, мапится один раз */
static uint64_t g_stack_phys = 0;

static int setup_stack(void) {
    if (!g_stack_phys) {
        g_stack_phys = pmm_alloc();
        if (!g_stack_phys) return 0;
        vmm_map(USER_STACK_VADDR, g_stack_phys,
                PTE_PRESENT | PTE_WRITE | PTE_USER);
    }
    /* обнуляем содержимое — чтобы не оставлять мусор прошлых запусков */
    uint8_t *p = (uint8_t*)g_stack_phys;
    for (uint64_t i = 0; i < USER_STACK_SIZE; i++) p[i] = 0;
    return 1;
}

/* запись в user-память по vaddr через физический адрес */
static void user_write(uint64_t vaddr, const void *src, uint64_t len) {
    uint64_t phys = vmm_get_phys(vaddr);
    if (!phys) return;
    uint8_t *dst = (uint8_t*)phys;
    const uint8_t *s = (const uint8_t*)src;
    for (uint64_t i = 0; i < len; i++) dst[i] = s[i];
}

static void user_write_u64(uint64_t vaddr, uint64_t v) {
    user_write(vaddr, &v, 8);
}

/* построить стартовый стек: argc, argv[], строки argv.
   Возвращает user_rsp или 0. */
static uint64_t build_initial_stack(int argc, const char **argv) {
    uint64_t sp = USER_STACK_VADDR + USER_STACK_SIZE;   /* верх страницы */
    uint64_t argv_ptrs[32];   /* до 32 аргументов */

    if (argc > 32) argc = 32;
    if (argc < 1) argc = 1;

    /* 1. строки в обратном порядке (от последней к первой) */
    for (int i = argc - 1; i >= 0; i--) {
        const char *s = argv[i];
        uint64_t len = 0;
        while (s[len]) len++;
        sp -= (len + 1);
        if (sp < USER_STACK_VADDR + 64) return 0;
        user_write(sp, s, len + 1);
        argv_ptrs[i] = sp;
    }

    /* 2. выравнивание sp на 16 */
    sp &= ~15ULL;

    /* 3. NULL-терминатор argv */
    sp -= 8;
    user_write_u64(sp, 0);

    /* 4. argv[argc-1] .. argv[0] — в обратном порядке */
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        user_write_u64(sp, argv_ptrs[i]);
    }

    /* 5. argc */
    sp -= 8;
    user_write_u64(sp, (uint64_t)argc);

    /* 6. выравнивание итогового user_rsp на 16 */
    sp &= ~15ULL;

    return sp;
}

void usermode_run_elf(const uint8_t *data, uint64_t size,
                      int argc, const char **argv) {
    if (!data || size < 64) {
        loge(S_USR, "elf: no data");
        return;
    }

    logi(S_USR, "loading ELF (%d bytes, argc=%d)", size, argc);

    uint64_t entry = elf_load(data, size);
    if (!entry) {
        loge(S_USR, "elf_load failed");
        return;
    }

    if (!setup_stack()) {
        loge(S_USR, "no phys for user stack");
        return;
    }

    uint64_t user_rsp = build_initial_stack(argc, argv);
    if (!user_rsp) {
        loge(S_USR, "build_initial_stack failed");
        return;
    }

    tss_set_rsp0((uint64_t)irq_stack + sizeof(irq_stack) - 16);

    logi(S_USR, "entering ring3: rip=%x rsp=%x", entry, user_rsp);

    if (!g_user_active) {
        user_setjmp(&g_user_ctx);
        if (g_user_active == 0) {
            g_user_active = 1;
            usermode_enter(entry, user_rsp);
        }
        __asm__ volatile("sti");
    }

    g_user_active = 0;
    logi(S_USR, "user program returned, exit code=%d", g_user_exit);
}

void usermode_run_init(void) {
    ntmem_file_t *f = ntmem_fs_find("hello.elf");
    if (!f) {
        logw(S_USR, "hello.elf not found in ntmem_fs");
        return;
    }
    const char *argv[] = { "hello.elf" };
    usermode_run_elf(f->data, f->size, 1, argv);
}