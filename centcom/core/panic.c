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

// Цветовая палитра
#define COL_BG      0x000A0A0C
#define COL_PANEL   0x00141A20
#define COL_BORDER  0x00AA2222
#define COL_RED     0x00FF3333
#define COL_ORANGE  0x00FFAA00
#define COL_TEXT    0x00D0D0D0
#define COL_MUTED   0x00808080
#define COL_CYAN    0x0044DDFF

#define USER_CODE_VADDR  0x0000010000000000ULL
#define USER_STACK_VADDR 0x0000020000000000ULL

extern int g_last_syscall;
extern uint64_t g_last_syscall_arg;

static void put_str(const char *s, uint32_t fg, uint32_t bg) {
    while (*s) fb_putc(*s++, fg, bg);
}

static void put_hex(uint64_t v, uint32_t fg, uint32_t bg) {
    static const char *h = "0123456789abcdef";
    fb_putc('0', fg, bg); fb_putc('x', fg, bg);
    for (int i = 60; i >= 0; i -= 4) fb_putc(h[(v >> i) & 0xF], fg, bg);
}

static void put_dec(uint64_t v, uint32_t fg, uint32_t bg) {
    char b[24]; int i = 0;
    if (!v) { fb_putc('0', fg, bg); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) fb_putc(b[i], fg, bg);
}

static void draw_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *title) {
    fb_fill_rect(x, y, w, h, COL_PANEL);
    fb_draw_box(x, y, w, h, COL_BORDER);
    
    fb_set_cursor(x + 4, y);
    put_str("=[ ", COL_RED, COL_PANEL); // Исправлено на ASCII
    put_str(title, COL_ORANGE, COL_PANEL);
    put_str(" ]", COL_RED, COL_PANEL);
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
        case 0: return "EXIT";      case 1: return "WRITE";
        case 2: return "OPEN";      case 3: return "READ";
        case 4: return "CLOSE";     case 5: return "LIST";
        case 6: return "SLEEP";     case 7: return "PLAY_PCM";
        case 8: return "KV_WRITE";  case 9: return "KV_READ";
        case 10: return "NTRFS_LIST"; case 11: return "NTRFS_READ";
        case 12: return "NTRFS_WRITE"; case 13: return "EXEC";
        case 14: return "FORK";     case 15: return "WAIT";
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
    fb_clear(COL_BG);

    uint32_t w = fb_width();
    uint32_t h = fb_height();
    uint32_t cx = 16;
    uint32_t cy = 16;
    uint32_t panel_w = w - 32;
    uint32_t panel_h = 100;

    // --- ШАПКА ---
    fb_fill_rect(cx, cy, panel_w, 32, COL_RED);
    fb_set_cursor(cx + 8, cy + 12);
    put_str("NANOTRASEN SECURITY SYSTEMS // CENTCOM CORE BOOT FAULT", 0x00FFFFFF, COL_RED);
    
    cy += 48;
    fb_set_cursor(cx, cy);
    put_str("INCIDENT REPORT - CONFIDENTIAL", COL_ORANGE, COL_BG);
    fb_set_cursor(w - 250, cy);
    put_str("SEVERITY: ", COL_MUTED, COL_BG); put_str("CRITICAL", COL_RED, COL_BG);
    
    cy += 16;
    fb_set_cursor(cx, cy);
    put_str("UPTIME:   ", COL_MUTED, COL_BG); 
    put_dec(uptime / 3600, COL_TEXT, COL_BG); put_str(":", COL_TEXT, COL_BG);
    if ((uptime / 60) % 60 < 10) put_str("0", COL_TEXT, COL_BG);
    put_dec((uptime / 60) % 60, COL_TEXT, COL_BG); put_str(":", COL_TEXT, COL_BG);
    if (uptime % 60 < 10) put_str("0", COL_TEXT, COL_BG);
    put_dec(uptime % 60, COL_TEXT, COL_BG);
    
    fb_set_cursor(w - 250, cy);
    put_str("TERMINAL: ", COL_MUTED, COL_BG); put_str("NT-0xDEADBEEF", COL_CYAN, COL_BG);
    
    cy += 24;

    // --- СЕКЦИЯ 1: EXCEPTION ---
    draw_panel(cx, cy, panel_w, panel_h, "EXCEPTION");
    cy += 16;
    
    fb_set_cursor(cx + 8, cy);
    put_str("CODE:   ", COL_MUTED, COL_PANEL); put_str(get_cause(r->int_no), COL_RED, COL_PANEL); 
    put_str(" (", COL_MUTED, COL_PANEL); put_dec(r->int_no, COL_MUTED, COL_PANEL); put_str(")", COL_MUTED, COL_PANEL);
    
    fb_set_cursor(cx + panel_w/2, cy);
    put_str("RIP:    ", COL_MUTED, COL_PANEL); put_hex(r->rip, COL_CYAN, COL_PANEL);
    cy += 12;
    
    fb_set_cursor(cx + 8, cy);
    put_str("ERR:    ", COL_MUTED, COL_PANEL); put_hex(r->err_code, COL_TEXT, COL_PANEL);
    fb_set_cursor(cx + panel_w/2, cy);
    put_str("CS:     ", COL_MUTED, COL_PANEL); put_hex(r->cs, COL_TEXT, COL_PANEL);
    cy += 12;
    
    fb_set_cursor(cx + 8, cy);
    put_str("CR2:    ", COL_MUTED, COL_PANEL); put_hex(cr2, COL_TEXT, COL_PANEL);
    fb_set_cursor(cx + panel_w/2, cy);
    put_str("RFLAGS: ", COL_MUTED, COL_PANEL); put_hex(r->rflags, COL_TEXT, COL_PANEL);
    cy += 16;

    // --- СЕКЦИЯ 2: PROCESS CONTEXT ---
    draw_panel(cx, cy, panel_w, panel_h, "PROCESS CONTEXT");
    cy += 16;
    
    process_t *proc = process_current();
    if (proc) {
        fb_set_cursor(cx + 8, cy);
        put_str("NAME:   ", COL_MUTED, COL_PANEL); put_str(proc->name, COL_TEXT, COL_PANEL);
        fb_set_cursor(cx + panel_w/2, cy);
        put_str("PID:    ", COL_MUTED, COL_PANEL); put_dec(proc->pid, COL_TEXT, COL_PANEL);
        cy += 12;
        
        fb_set_cursor(cx + 8, cy);
        put_str("STATE:  ", COL_MUTED, COL_PANEL);
        const char *state_str = "UNKNOWN";
        if (proc->state == PROC_RUNNING) state_str = "RUNNING";
        else if (proc->state == PROC_READY) state_str = "READY";
        else if (proc->state == PROC_BLOCKED) state_str = "BLOCKED";
        else if (proc->state == PROC_ZOMBIE) state_str = "ZOMBIE";
        put_str(state_str, COL_TEXT, COL_PANEL);
        
        fb_set_cursor(cx + panel_w/2, cy);
        put_str("TYPE:   ", COL_MUTED, COL_PANEL); put_str(proc->is_user ? "USER (ring 3)" : "KERNEL (ring 0)", COL_TEXT, COL_PANEL);
        cy += 12;
        
        fb_set_cursor(cx + 8, cy);
        put_str("CR3:    ", COL_MUTED, COL_PANEL); put_hex(proc->cr3, COL_CYAN, COL_PANEL);
        fb_set_cursor(cx + panel_w/2, cy);
        put_str("USER RSP: ", COL_MUTED, COL_PANEL); put_hex(proc->user_rsp, COL_CYAN, COL_PANEL);
        cy += 12;
    } else {
        fb_set_cursor(cx + 8, cy);
        put_str("KERNEL THREAD (EARLY BOOT)", COL_TEXT, COL_PANEL);
        cy += 36;
    }
    cy += 16;

    // --- СЕКЦИЯ 3: REGISTERS ---
    draw_panel(cx, cy, panel_w, panel_h, "REGISTERS");
    cy += 16;
    
    uint32_t rx = cx + 8;
    uint32_t ry = cy;
    uint32_t col_w = panel_w / 4;

    fb_set_cursor(rx + 0 * col_w, ry); put_str("RAX: ", COL_MUTED, COL_PANEL); put_hex(r->rax, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 1 * col_w, ry); put_str("RBX: ", COL_MUTED, COL_PANEL); put_hex(r->rbx, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 2 * col_w, ry); put_str("RCX: ", COL_MUTED, COL_PANEL); put_hex(r->rcx, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 3 * col_w, ry); put_str("RDX: ", COL_MUTED, COL_PANEL); put_hex(r->rdx, COL_CYAN, COL_PANEL);
    ry += 12;

    fb_set_cursor(rx + 0 * col_w, ry); put_str("RSI: ", COL_MUTED, COL_PANEL); put_hex(r->rsi, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 1 * col_w, ry); put_str("RDI: ", COL_MUTED, COL_PANEL); put_hex(r->rdi, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 2 * col_w, ry); put_str("RBP: ", COL_MUTED, COL_PANEL); put_hex(r->rbp, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 3 * col_w, ry); put_str("RSP: ", COL_MUTED, COL_PANEL); put_hex(r->rsp, COL_CYAN, COL_PANEL);
    ry += 12;

    fb_set_cursor(rx + 0 * col_w, ry); put_str("R8:  ", COL_MUTED, COL_PANEL); put_hex(r->r8,  COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 1 * col_w, ry); put_str("R9:  ", COL_MUTED, COL_PANEL); put_hex(r->r9,  COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 2 * col_w, ry); put_str("R10: ", COL_MUTED, COL_PANEL); put_hex(r->r10, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 3 * col_w, ry); put_str("R11: ", COL_MUTED, COL_PANEL); put_hex(r->r11, COL_CYAN, COL_PANEL);
    ry += 12;

    fb_set_cursor(rx + 0 * col_w, ry); put_str("R12: ", COL_MUTED, COL_PANEL); put_hex(r->r12, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 1 * col_w, ry); put_str("R13: ", COL_MUTED, COL_PANEL); put_hex(r->r13, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 2 * col_w, ry); put_str("R14: ", COL_MUTED, COL_PANEL); put_hex(r->r14, COL_CYAN, COL_PANEL);
    fb_set_cursor(rx + 3 * col_w, ry); put_str("R15: ", COL_MUTED, COL_PANEL); put_hex(r->r15, COL_CYAN, COL_PANEL);
    
    cy += 76 + 16;

    // --- СЕКЦИЯ 4: SYSTEM STATE ---
    draw_panel(cx, cy, panel_w, panel_h, "SYSTEM STATE");
    cy += 16;
    
    fb_set_cursor(cx + 8, cy); 
    put_str("PMM:    ", COL_MUTED, COL_PANEL); put_dec(pmm_free_pages(), COL_TEXT, COL_PANEL); put_str(" / ", COL_MUTED, COL_PANEL); put_dec(pmm_total_pages(), COL_TEXT, COL_PANEL); put_str(" pages free", COL_MUTED, COL_PANEL);
    fb_set_cursor(cx + panel_w/2, cy); 
    put_str("HEAP:   ", COL_MUTED, COL_PANEL); put_dec(heap_used(), COL_TEXT, COL_PANEL); put_str(" / ", COL_MUTED, COL_PANEL); put_dec(heap_total(), COL_TEXT, COL_PANEL); put_str(" bytes", COL_MUTED, COL_PANEL);
    cy += 12;

    int running=0, blocked=0, zombie=0;
    for(int i=0; i<PROC_MAX; i++) {
        process_t *pr = process_get(i);
        if(!pr || pr->state == PROC_FREE) continue;
        if(pr->state == PROC_RUNNING) running++;
        else if(pr->state == PROC_BLOCKED) blocked++;
        else if(pr->state == PROC_ZOMBIE) zombie++;
    }
    fb_set_cursor(cx + 8, cy); 
    put_str("PROCS:  ", COL_MUTED, COL_PANEL); put_dec(process_count(), COL_TEXT, COL_PANEL); put_str(" active (", COL_MUTED, COL_PANEL);
    put_dec(running, COL_TEXT, COL_PANEL); put_str(" run, ", COL_MUTED, COL_PANEL);
    put_dec(blocked, COL_TEXT, COL_PANEL); put_str(" block, ", COL_MUTED, COL_PANEL);
    put_dec(zombie, COL_TEXT, COL_PANEL); put_str(" zomb)", COL_MUTED, COL_PANEL);
    
    fb_set_cursor(cx + panel_w/2, cy);
    put_str("SYSC:   ", COL_MUTED, COL_PANEL);
    if (g_last_syscall >= 0) {
        put_str(get_syscall_name(g_last_syscall), COL_TEXT, COL_PANEL);
        put_str(" (", COL_MUTED, COL_PANEL); put_dec(g_last_syscall, COL_MUTED, COL_PANEL); put_str(")", COL_MUTED, COL_PANEL);
    } else {
        put_str("NONE", COL_TEXT, COL_PANEL);
    }
    cy += 12;
    
    fb_set_cursor(cx + 8, cy); 
    put_str("INTERRUPTS: ", COL_MUTED, COL_PANEL); put_str((r->rflags & 0x200) ? "ENABLED" : "DISABLED", COL_TEXT, COL_PANEL);
    
    cy += 16;

    // --- СЕКЦИЯ 5: RECENT LOGS ---
    draw_panel(cx, cy, panel_w, panel_h, "RECENT LOGS");
    cy += 16;
    
    static char logs[2048];
    log_get_recent(logs, 5);
    char *q = logs;
    int line_count = 0;
    while (*q && line_count < 5) {
        char line[128]; int n = 0;
        while (*q && *q != '\n' && n < 127) line[n++] = *q++;
        line[n] = 0;
        if (*q == '\n') q++;
        
        fb_set_cursor(cx + 8, cy);
        put_str(line, COL_MUTED, COL_PANEL);
        cy += 12;
        line_count++;
    }
    
    // --- ПОДВАЛ ---
    fb_set_cursor(cx, h - 32);
    put_str("RECOVERY: system halted. Manual reset required.", COL_RED, COL_BG);
    
    uint32_t idx = pit_ticks() % PANIC_PHRASE_COUNT;
    const char *p = panic_phrases[idx];
    if (!p) p = "That wasn't our mistake. Meow :3";
    
    uint32_t plen = 0; while (p[plen]) plen++;
    uint32_t px = (w - (plen * 8)) / 2;
    fb_set_cursor(px, h - 16);
    fb_putc('"', COL_MUTED, COL_BG);
    put_str(p, COL_MUTED, COL_BG);
    fb_putc('"', COL_MUTED, COL_BG);

    serial_puts("\n*** CENTCOM CORE BOOT FAULT ***\n");
    for (;;) __asm__ volatile("hlt");
}

void panic_unknown(regs_t *r) {
    panic_exception(r);
}

void centcom_panic(void) {
    cue_play(NTSAD_CUE_PANIC);
    fb_set_scale(1);
    fb_clear(COL_BG);

    uint32_t w = fb_width();
    uint32_t h = fb_height();

    fb_fill_rect(16, 16, w - 32, 32, COL_RED);
    fb_set_cursor(24, 24);
    put_str(" NANOTRASEN SECURITY SYSTEMS // CENTCOM CORE MANUAL HALT ", 0x00FFFFFF, COL_RED);

    fb_set_cursor(16, 64);
    put_str("SEVERITY: MANUAL HALT", COL_RED, COL_BG);

    uint32_t idx = pit_ticks() % PANIC_PHRASE_COUNT;
    const char *p = panic_phrases[idx];
    if (!p) p = "You asked for this. Meow :3";
    
    uint32_t plen = 0; while (p[plen]) plen++;
    uint32_t px = (w - (plen * 8)) / 2;
    fb_set_cursor(px, h / 2);
    fb_putc('"', COL_MUTED, COL_BG);
    put_str(p, COL_MUTED, COL_BG);
    fb_putc('"', COL_MUTED, COL_BG);

    for (;;) __asm__ volatile("hlt");
}