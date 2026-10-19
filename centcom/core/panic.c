#include "panic.h"
#include "panic_phrases.h"
#include "../tty/fb.h"
#include "../log/serial.h"
#include "../log/log.h"
#include "../sched/sched.h"
#include "../mem/pmm.h"
#include "../mem/heap.h"
#include "../time/pit.h"
#include "../sound/cue.h"
#include "../ntsad/ntsad.h"

#define BG_ORANGE  0x00D06010
#define BG_DARK    0x00ADD8E6
#define FG_WHITE   0x00F0F0F0
#define FG_DIM     0x00603008
#define BORDER_COL 0x00F0F0F0

#define USER_CODE_VADDR  0x0000010000000000ULL
#define USER_STACK_VADDR 0x0000020000000000ULL

#define BOX_MARGIN    16
#define BOX_PAD_X     12
#define BOX_PAD_Y     12
#define BORDER_THICK  2

extern int g_last_syscall;
extern uint64_t g_last_syscall_arg;

static uint32_t g_box_x, g_box_y, g_box_w, g_box_h;
static uint32_t g_cur_y;

static void app(char **p, const char *s) { while (*s) *(*p)++ = *s++; }

static void app_hex(char **p, uint64_t v) {
    static const char *h = "0123456789abcdef";
    *(*p)++ = '0'; *(*p)++ = 'x';
    for (int i = 60; i >= 0; i -= 4) *(*p)++ = h[(v >> i) & 0xF];
}

static void app_dec(char **p, uint64_t v) {
    char b[24]; int i = 0;
    if (!v) { *(*p)++ = '0'; return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) *(*p)++ = b[i];
}

static void pline(const char *s, uint32_t fg, uint32_t bg) {
    uint32_t max_chars = (g_box_w - BOX_PAD_X * 2) / 8;
    fb_set_cursor(g_box_x + BOX_PAD_X, g_cur_y);
    for (uint32_t i = 0; s[i] && i < max_chars; i++) {
        fb_putc(s[i], fg, bg);
    }
    g_cur_y += 8;
}

static void pline_kv(const char *label, const char *val_str, uint32_t val_color, uint32_t bg) {
    uint32_t max_chars = (g_box_w - BOX_PAD_X * 2) / 8;
    fb_set_cursor(g_box_x + BOX_PAD_X, g_cur_y);
    
    int len = 0;
    while (label[len]) { 
        if ((uint32_t)len < max_chars) fb_putc(label[len], FG_WHITE, bg); 
        len++; 
    }
    
    int vlen = 0;
    while (val_str[vlen]) {
        if ((uint32_t)(len + vlen) < max_chars) fb_putc(val_str[vlen], val_color, bg);
        vlen++;
    }
    
    g_cur_y += 8;
}

static void hsep(uint32_t bg) {
    uint32_t max_chars = (g_box_w - BOX_PAD_X * 2) / 8;
    fb_set_cursor(g_box_x + BOX_PAD_X, g_cur_y);
    for (uint32_t i = 0; i < max_chars; i++) fb_putc('-', FG_DIM, bg);
    g_cur_y += 8;
}

static void print_phrase_centered(const char *default_phrase, uint32_t bg) {
    uint32_t idx = pit_ticks() % PANIC_PHRASE_COUNT;
    const char *p = panic_phrases[idx];
    if (!p) p = default_phrase;
    
    uint32_t max_chars = (g_box_w - BOX_PAD_X * 2) / 8;
    uint32_t len = 0;
    while (p[len]) len++;
    
    uint32_t total_len = len + 2;
    if (total_len > max_chars) total_len = max_chars;
    
    uint32_t pad = (max_chars - total_len) / 2;
    fb_set_cursor(g_box_x + BOX_PAD_X + (pad * 8), g_cur_y);
    
    fb_putc('"', FG_DIM, bg);
    for (uint32_t i = 0; i < len && (i + 2) < max_chars; i++) {
        fb_putc(p[i], FG_DIM, bg);
    }
    fb_putc('"', FG_DIM, bg);
    g_cur_y += 8;
}

static void get_pf_err_str(uint64_t err, char *out) {
    char *p = out;
    app(&p, (err & 1) ? "P=1 " : "P=0 ");
    app(&p, (err & 2) ? "W=1 " : "W=0 ");
    app(&p, (err & 4) ? "U=1 " : "U=0 ");
    app(&p, (err & 8) ? "RSVD=1 " : "RSVD=0 ");
    app(&p, (err & 16) ? "I=1" : "I=0");
    *p = 0;
}

static const char *get_cause(uint64_t n) {
    switch (n) {
        case 0:  return "Divide Error";
        case 3:  return "Breakpoint";
        case 6:  return "Invalid Opcode";
        case 8:  return "Double Fault";
        case 13: return "General Protection";
        case 14: return "Page Fault";
        default: return "Unknown Exception";
    }
}

static const char *get_syscall_name(int n) {
    switch(n) {
        case 0: return "EXIT";
        case 1: return "WRITE";
        case 2: return "OPEN";
        case 3: return "READ";
        case 4: return "CLOSE";
        case 5: return "LIST";
        case 6: return "SLEEP";
        case 7: return "PLAY_PCM";
        case 8: return "KV_WRITE";
        case 9: return "KV_READ";
        case 10: return "NTRFS_LIST";
        case 11: return "NTRFS_READ";
        case 12: return "NTRFS_WRITE";
        case 13: return "EXEC";
        case 14: return "FORK";
        case 15: return "WAIT";
        default: return "UNKNOWN";
    }
}

void panic_exception(regs_t *r) {
    ntsad_error(NTSAD_CH_CORE, r->int_no);
    cue_play(NTSAD_CUE_PANIC);

    uint64_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    uint64_t uptime = pit_ticks() / 100;

    fb_set_scale(1);
    fb_clear(BG_ORANGE);

    g_box_x = BOX_MARGIN;
    g_box_y = BOX_MARGIN;
    g_box_w = fb_width()  - BOX_MARGIN * 2;
    g_box_h = fb_height() - BOX_MARGIN * 2;

    for (uint32_t t = 0; t < BORDER_THICK; t++) {
        fb_draw_box(g_box_x + t, g_box_y + t, g_box_w - 2 * t, g_box_h - 2 * t, BORDER_COL);
    }

    g_cur_y = g_box_y + BOX_PAD_Y;
    char buf[256]; char *p;

    // --- ШАПКА ---
    pline("*** CENTCOM CORE BOOT FAULT ***", FG_WHITE, BG_ORANGE);
    pline("NANOTRASEN SECURITY SYSTEMS — INCIDENT REPORT", FG_WHITE, BG_ORANGE);
    pline("", FG_WHITE, BG_ORANGE);
    
    pline_kv("SEVERITY:   ", "CRITICAL", FG_WHITE, BG_ORANGE);
    pline_kv("TERMINAL:   ", "NT-0xDEADBEEF", FG_DIM, BG_ORANGE);
    
    p = buf; 
    app_dec(&p, uptime / 3600); app(&p, ":");
    if ((uptime / 60) % 60 < 10) { app(&p, "0"); }
    app_dec(&p, (uptime / 60) % 60); app(&p, ":");
    if (uptime % 60 < 10) { app(&p, "0"); }
    app_dec(&p, uptime % 60);
    app(&p, " ("); app_dec(&p, pit_ticks()); app(&p, " ticks)");
    *p = 0;
    pline_kv("UPTIME:     ", buf, FG_DIM, BG_ORANGE);
    hsep(BG_ORANGE);

    // --- СЕКЦИЯ 1 ---
    pline("[1] EXCEPTION", FG_WHITE, BG_ORANGE);
    hsep(BG_ORANGE);
    
    p = buf; app(&p, "#"); app(&p, get_cause(r->int_no)); app(&p, " ("); app_dec(&p, r->int_no); app(&p, ")");
    *p = 0;
    pline_kv("    Code:      ", buf, FG_DIM, BG_ORANGE);
    pline_kv("    Category:  ", (r->int_no == 14) ? "Memory / Paging" : "CPU / Execution", FG_DIM, BG_ORANGE);
    pline_kv("    Cause:     ", "CPU executed illegal or unknown instruction", FG_DIM, BG_ORANGE);
    pline_kv("    Suggestion:", "check recent code changes in userspace or ELF loader", FG_DIM, BG_ORANGE);
    pline("", FG_WHITE, BG_ORANGE);

    p = buf; app_hex(&p, r->rip); *p = 0; pline_kv("    RIP:       ", buf, FG_DIM, BG_ORANGE);
    p = buf; app_hex(&p, r->cs); *p = 0; pline_kv("    CS:        ", buf, FG_DIM, BG_ORANGE);
    p = buf; app_hex(&p, r->rflags); *p = 0; pline_kv("    RFLAGS:    ", buf, FG_DIM, BG_ORANGE);
    p = buf; app_hex(&p, r->err_code); *p = 0; pline_kv("    ERRCODE:   ", buf, FG_DIM, BG_ORANGE);

    if (r->int_no == 14) {
        p = buf; app_hex(&p, cr2); *p = 0; pline_kv("    CR2:       ", buf, FG_DIM, BG_ORANGE);
        char err_desc[64]; get_pf_err_str(r->err_code, err_desc);
        p = buf; app(&p, "("); app(&p, err_desc); app(&p, ")");
        *p = 0;
        pline_kv("               ", buf, FG_DIM, BG_ORANGE);
    } else {
        pline_kv("    CR2:       ", "— (not a page fault)", FG_DIM, BG_ORANGE);
    }
    hsep(BG_ORANGE);

    // --- СЕКЦИЯ 2 ---
    pline("[2] PROCESS CONTEXT", FG_WHITE, BG_ORANGE);
    hsep(BG_ORANGE);
    process_t *proc = process_current();
    if (proc) {
        pline_kv("    Name:      ", proc->name, FG_DIM, BG_ORANGE);
        p = buf; app_dec(&p, proc->pid); *p = 0; pline_kv("    PID:       ", buf, FG_DIM, BG_ORANGE);
        p = buf; app_dec(&p, proc->parent_pid); *p = 0; pline_kv("    Parent:    ", buf, FG_DIM, BG_ORANGE);
        
        const char *state_str = "UNKNOWN";
        if (proc->state == PROC_RUNNING) state_str = "RUNNING";
        else if (proc->state == PROC_READY) state_str = "READY";
        else if (proc->state == PROC_BLOCKED) state_str = "BLOCKED";
        else if (proc->state == PROC_ZOMBIE) state_str = "ZOMBIE";
        pline_kv("    State:     ", state_str, FG_DIM, BG_ORANGE);
        
        pline_kv("    Type:      ", proc->is_user ? "USER (ring 3)" : "KERNEL (ring 0)", FG_DIM, BG_ORANGE);
        p = buf; app_hex(&p, proc->cr3); *p = 0; pline_kv("    CR3:       ", buf, FG_DIM, BG_ORANGE);
        p = buf; app_hex(&p, proc->user_rsp); *p = 0; pline_kv("    User RSP:  ", buf, FG_DIM, BG_ORANGE);
        p = buf; app_hex(&p, proc->kernel_stack_top); *p = 0; pline_kv("    KStackTop: ", buf, FG_DIM, BG_ORANGE);
    } else {
        pline_kv("    Name:      ", "KERNEL THREAD (EARLY BOOT)", FG_DIM, BG_ORANGE);
    }
    hsep(BG_ORANGE);

    // --- СЕКЦИЯ 3 ---
    pline("[3] REGISTERS", FG_WHITE, BG_ORANGE);
    hsep(BG_ORANGE);
    p = buf; app(&p, "RAX = "); app_hex(&p, r->rax); app(&p, "    R8  = "); app_hex(&p, r->r8); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RBX = "); app_hex(&p, r->rbx); app(&p, "    R9  = "); app_hex(&p, r->r9); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RCX = "); app_hex(&p, r->rcx); app(&p, "    R10 = "); app_hex(&p, r->r10); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RDX = "); app_hex(&p, r->rdx); app(&p, "    R11 = "); app_hex(&p, r->r11); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RSI = "); app_hex(&p, r->rsi); app(&p, "    R12 = "); app_hex(&p, r->r12); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RDI = "); app_hex(&p, r->rdi); app(&p, "    R13 = "); app_hex(&p, r->r13); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RBP = "); app_hex(&p, r->rbp); app(&p, "    R14 = "); app_hex(&p, r->r14); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    p = buf; app(&p, "RSP = "); app_hex(&p, r->rsp); app(&p, "    R15 = "); app_hex(&p, r->r15); *p = 0;
    pline(buf, FG_DIM, BG_ORANGE);
    hsep(BG_ORANGE);

    // --- СЕКЦИЯ 4 ---
    pline("[4] STACK (RSP + 0..63)", FG_WHITE, BG_ORANGE);
    hsep(BG_ORANGE);
    uint64_t *stack = (uint64_t*)r->rsp;
    for (int i = 0; i < 4; i++) {
        p = buf; 
        app(&p, "+0x"); app_hex(&p, (uint64_t)i*8); app(&p, "  "); app_hex(&p, stack[i]);
        app(&p, "    +0x"); app_hex(&p, (uint64_t)(i+4)*8); app(&p, "  "); app_hex(&p, stack[i+4]);
        *p = 0;
        pline(buf, FG_DIM, BG_ORANGE);
    }
    hsep(BG_ORANGE);

    // --- СЕКЦИЯ 5 ---
    pline("[5] KERNEL STATE", FG_WHITE, BG_ORANGE);
    hsep(BG_ORANGE);
    p = buf; 
    app_dec(&p, pmm_free_pages()); app(&p, " free / "); app_dec(&p, pmm_total_pages()); app(&p, " total pages");
    *p = 0; pline_kv("    PMM:       ", buf, FG_DIM, BG_ORANGE);

    p = buf; 
    app_dec(&p, heap_used()); app(&p, " / "); app_dec(&p, heap_total()); app(&p, " bytes");
    *p = 0; pline_kv("    Heap:      ", buf, FG_DIM, BG_ORANGE);

    int running=0, blocked=0, zombie=0;
    for(int i=0; i<PROC_MAX; i++) {
        process_t *pr = process_get(i);
        if(!pr || pr->state == PROC_FREE) continue;
        if(pr->state == PROC_RUNNING) running++;
        else if(pr->state == PROC_BLOCKED) blocked++;
        else if(pr->state == PROC_ZOMBIE) zombie++;
    }
    p = buf; 
    app_dec(&p, process_count()); app(&p, " active ("); app_dec(&p, running); app(&p, " run, ");
    app_dec(&p, blocked); app(&p, " block, "); app_dec(&p, zombie); app(&p, " zomb)");
    *p = 0; pline_kv("    Processes: ", buf, FG_DIM, BG_ORANGE);

    p = buf;
    if (g_last_syscall >= 0) {
        app(&p, get_syscall_name(g_last_syscall)); app(&p, " ("); app_dec(&p, g_last_syscall);
        app(&p, "), arg="); app_hex(&p, g_last_syscall_arg);
        if (g_last_syscall == SYS_EXEC || g_last_syscall == SYS_OPEN) {
            if (g_last_syscall_arg >= USER_CODE_VADDR && g_last_syscall_arg < USER_STACK_VADDR + 0x1000) {
                app(&p, " \""); app(&p, (const char*)g_last_syscall_arg); app(&p, "\"");
            }
        }
    } else {
        app(&p, "NONE");
    }
    *p = 0; pline_kv("    Last syscall: ", buf, FG_DIM, BG_ORANGE);

    p = buf;
    app(&p, "IF="); app(&p, (r->rflags & 0x200) ? "1" : "0");
    app(&p, ", in_syscall="); app(&p, g_last_syscall >= 0 ? "1" : "0");
    *p = 0; pline_kv("    Interrupts:", buf, FG_DIM, BG_ORANGE);
    hsep(BG_ORANGE);

    // --- СЕКЦИЯ 6 (Логи) ---
    pline("[6] RECENT LOGS", FG_WHITE, BG_ORANGE);
    hsep(BG_ORANGE);
    {
        static char logs[2048];
        log_get_recent(logs, 8);
        char *q = logs;
        while (*q) {
            char line[128]; int n = 0;
            while (*q && *q != '\n' && n < 127) line[n++] = *q++;
            line[n] = 0;
            if (*q == '\n') q++;
            pline(line, FG_DIM, BG_ORANGE);
        }
        pline("<FAULT>", FG_DIM, BG_ORANGE);
    }

    // --- ПОДВАЛ ---
    g_cur_y = g_box_y + g_box_h - BOX_PAD_Y - 24;
    pline("RECOVERY: system halted. Manual reset required.", FG_WHITE, BG_ORANGE);
    pline("", FG_WHITE, BG_ORANGE);
    print_phrase_centered("That wasn't our mistake. Meow :3", BG_ORANGE);

    serial_puts("\n*** CENTCOM CORE BOOT FAULT ***\n");
    for (;;) __asm__ volatile("hlt");
}

void panic_unknown(regs_t *r) {
    panic_exception(r);
}

void centcom_panic(void) {
    cue_play(NTSAD_CUE_PANIC);
    fb_set_scale(1);
    fb_clear(BG_ORANGE);

    g_box_x = BOX_MARGIN;
    g_box_y = BOX_MARGIN;
    g_box_w = fb_width()  - BOX_MARGIN * 2;
    g_box_h = fb_height() - BOX_MARGIN * 2;

    for (uint32_t t = 0; t < BORDER_THICK; t++) {
        fb_draw_box(g_box_x + t, g_box_y + t, g_box_w - 2 * t, g_box_h - 2 * t, BORDER_COL);
    }

    g_cur_y = g_box_y + BOX_PAD_Y;
    pline("*** CENTCOM CORE BOOT FAILURE ***", FG_WHITE, BG_ORANGE);
    pline("SEVERITY: MANUAL HALT", FG_WHITE, BG_ORANGE);
    pline("", FG_WHITE, BG_ORANGE);
    
    print_phrase_centered("You asked for this. Meow :3", BG_ORANGE);

    for (;;) __asm__ volatile("hlt");
}