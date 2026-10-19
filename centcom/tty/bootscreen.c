#include "bootscreen.h"
#include "fb.h"
#include "../log/log.h"
#include "../core/gdt.h"
#include "../core/idt.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/heap.h"
#include "../time/pit.h"
#include "../fs/ntmem_fs.h"
#include <stdint.h>

// Цветовая палитра в стиле CCBF
#define COL_BG      0x000A0A0C
#define COL_PANEL   0x00141A20
#define COL_BORDER  0x00AA2222
#define COL_RED     0x00FF3333
#define COL_ORANGE  0x00FFAA00
#define COL_TEXT    0x00D0D0D0
#define COL_MUTED   0x00808080
#define COL_CYAN    0x0044DDFF
#define COL_GREEN   0x0044FF88

static uint32_t g_cur_y = 0;
static const uint32_t PAD_X = 16;
static const uint32_t PAD_Y = 16;
static int g_current_percent = 0;

static void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 10000; i++) __asm__ volatile("pause");
}

static void put_str(const char *s, uint32_t fg, uint32_t bg) {
    while (*s) fb_putc(*s++, fg, bg);
}

static void put_dec(uint64_t v, uint32_t fg, uint32_t bg) {
    char b[24]; int i = 0;
    if (!v) { fb_putc('0', fg, bg); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) fb_putc(b[i], fg, bg);
}

static void draw_centered_text(const char *s, uint32_t scale, uint32_t y, uint32_t fg, uint32_t bg) {
    fb_set_scale(scale);
    int len = 0; while (s[len]) len++;
    uint32_t width = len * 8 * scale;
    uint32_t x = (fb_width() - width) / 2;
    fb_set_cursor(x, y);
    put_str(s, fg, bg);
}

static void draw_header(const char *subtitle) {
    uint32_t w = fb_width();
    
    fb_fill_rect(PAD_X, PAD_Y + 48, w - PAD_X * 2, 16, COL_BG);

    fb_fill_rect(PAD_X, PAD_Y, w - PAD_X * 2, 32, COL_RED);
    fb_set_cursor(PAD_X + 8, PAD_Y + 12);
    put_str("NANOTRASEN SECURITY SYSTEMS // CENTCOM BOOT", 0x00FFFFFF, COL_RED);

    fb_set_cursor(PAD_X, PAD_Y + 48);
    put_str("PHASE: ", COL_MUTED, COL_BG);
    put_str(subtitle, COL_ORANGE, COL_BG);
    
    const char *term_str = "TERMINAL: NT-0xDEADBEEF";
    int term_len = 0; while (term_str[term_len]) term_len++;
    fb_set_cursor(w - PAD_X - (term_len * 8), PAD_Y + 48);
    put_str(term_str, COL_MUTED, COL_BG);
}

static void log_line(const char *tag, const char *msg) {
    if (g_cur_y > fb_height() - 100) return;

    fb_set_cursor(PAD_X + 16, g_cur_y);
    
    put_str("[ ", COL_MUTED, COL_PANEL);
    put_str("..", COL_CYAN, COL_PANEL);
    put_str(" ] ", COL_MUTED, COL_PANEL);
    delay_ms(10);
    
    fb_set_cursor(PAD_X + 16, g_cur_y);
    put_str("[ ", COL_MUTED, COL_PANEL);
    put_str("OK", COL_GREEN, COL_PANEL);
    put_str(" ] ", COL_MUTED, COL_PANEL);
    
    put_str(tag, COL_CYAN, COL_PANEL);
    
    int tag_len = 0; while(tag[tag_len]) tag_len++;
    for (int i = tag_len; i < 16; i++) fb_putc(' ', COL_PANEL, COL_PANEL);
    
    put_str(msg, COL_TEXT, COL_PANEL);
    g_cur_y += 12;
}

static void animate_progress(int target_percent) {
    uint32_t w = fb_width();
    uint32_t h = fb_height();
    uint32_t bar_w = w - PAD_X * 4;
    uint32_t bar_h = 20;
    uint32_t bar_y = h - PAD_Y - 40;
    
    if (g_current_percent == 0) {
        fb_draw_box(PAD_X * 2, bar_y, bar_w, bar_h, COL_BORDER);
    }

    while (g_current_percent < target_percent) {
        g_current_percent++;
        
        fb_fill_rect(PAD_X * 2 + 2, bar_y + 2, bar_w - 4, bar_h - 4, COL_PANEL);
        
        uint32_t fill_w = (bar_w - 4) * g_current_percent / 100;
        fb_fill_rect(PAD_X * 2 + 2, bar_y + 2, fill_w, bar_h - 4, COL_GREEN);
        
        fb_set_cursor(PAD_X * 2 + 8, bar_y + 6);
        put_dec(g_current_percent, COL_BG, COL_GREEN);
        put_str("%", COL_BG, COL_GREEN);
        
        delay_ms(5);
    }
}

// =============================================================
//          ФАЗА 1: HANDOFF
// =============================================================
void bootscreen_phase1(const bootinfo_t *bi) {
    log_set_fb(0); 
    fb_set_scale(1);
    fb_clear(COL_BG);
    
    draw_header("1 - SYSTEM HANDSHAKE");
    
    uint32_t w = fb_width();
    uint32_t h = fb_height();
    fb_fill_rect(PAD_X, PAD_Y + 72, w - PAD_X * 2, h - PAD_Y - 140, COL_PANEL);
    fb_draw_box(PAD_X, PAD_Y + 72, w - PAD_X * 2, h - PAD_Y - 140, COL_BORDER);
    g_cur_y = PAD_Y + 88;

    char buf[64];
    
    log_line("BOOTINFO", "Handoff verified from UEFI");
    
    int p = 0;
    const char *prefix = "FB Mode: ";
    while (prefix[p]) { buf[p] = prefix[p]; p++; }
    buf[p++] = '0' + (bi->fb_width / 1000) % 10;
    buf[p++] = '0' + (bi->fb_width / 100) % 10;
    buf[p++] = '0' + (bi->fb_width / 10) % 10;
    buf[p++] = '0' + (bi->fb_width / 1) % 10;
    buf[p++] = 'x';
    buf[p++] = '0' + (bi->fb_height / 1000) % 10;
    buf[p++] = '0' + (bi->fb_height / 100) % 10;
    buf[p++] = '0' + (bi->fb_height / 10) % 10;
    buf[p++] = '0' + (bi->fb_height / 1) % 10;
    buf[p] = 0;
    log_line("FRAMEBUFFER", buf);

    log_line("MEMORY_MAP", "EFI descriptors received");
    log_line("CPU_MODE", "Long mode 64-bit protected");

    animate_progress(15);
}

// =============================================================
//          ФАЗА 2: CPU
// =============================================================
void bootscreen_phase2(void) {
    draw_header("2 - CPU SUBSYSTEM");
    
    char buf[64];
    int p;

    p = 0;
    const char *gdt_s = "Entries: ";
    while (gdt_s[p]) { buf[p] = gdt_s[p]; p++; }
    buf[p++] = '0' + gdt_count();
    buf[p] = 0;
    log_line("GDT", buf);

    p = 0;
    const char *idt_s = "Vectors: ";
    while (idt_s[p]) { buf[p] = idt_s[p]; p++; }
    buf[p++] = '0' + (idt_count() / 100) % 10;
    buf[p++] = '0' + (idt_count() / 10) % 10;
    buf[p++] = '0' + (idt_count() / 1) % 10;
    buf[p] = 0;
    log_line("IDT", buf);

    log_line("TSS", "Task State Segment initialized");
    log_line("EXCEPTIONS", "Page Fault / GP / DF handlers ready");

    animate_progress(35);
}

// =============================================================
//          ФАЗА 3: MEMORY
// =============================================================
void bootscreen_phase3(void) {
    draw_header("3 - MEMORY MANAGER");
    
    char buf[64];
    int p = 0;
    
    uint64_t total_mb = (pmm_total_pages() * 4) / 1024;
    const char *ram_s = "RAM Total: ";
    while (ram_s[p]) { buf[p] = ram_s[p]; p++; }
    
    if (total_mb >= 100) buf[p++] = '0' + (total_mb / 100) % 10;
    if (total_mb >= 10)  buf[p++] = '0' + (total_mb / 10) % 10;
    buf[p++] = '0' + (total_mb / 1) % 10;
    buf[p++] = ' '; buf[p++] = 'M'; buf[p++] = 'B';
    buf[p] = 0;
    log_line("PMM", buf);
    
    log_line("PAGING", "4-level page tables active");
    log_line("VMM", "Identity map 0..4 GiB");

    p = 0;
    const char *heap_s = "Size: ";
    while (heap_s[p]) { buf[p] = heap_s[p]; p++; }
    uint64_t heap_mb = heap_total() / (1024 * 1024);
    buf[p++] = '0' + (heap_mb / 10) % 10;
    buf[p++] = '0' + (heap_mb / 1) % 10;
    buf[p++] = ' '; buf[p++] = 'M'; buf[p++] = 'B';
    buf[p] = 0;
    log_line("HEAP", buf);

    animate_progress(60);
}

// =============================================================
//          ФАЗА 4: PERIPHERALS
// =============================================================
void bootscreen_phase4(void) {
    draw_header("4 - PERIPHERALS & DRIVERS");
    
    log_line("PCI_BUS", "Enumeration complete");
    log_line("AHCI", "SATA controller initialized");
    log_line("PIC", "8259 Interrupt Controller remapped");
    log_line("PIT", "Timer configured at 100 Hz");
    log_line("PS2_KBD", "Keyboard buffer online");
    log_line("SB16", "Audio DSP ready");
    
    animate_progress(85);
}

// =============================================================
//          ФАЗА 5: BINARY (С исправленной анимацией)
// =============================================================
void bootscreen_phase5(const bootinfo_t *bi) {
    (void)bi;
    draw_header("5 - SYSTEM BINARY");
    
    char buf[64];
    int p = 0;
    const char *fs_s = "Files mounted: ";
    while (fs_s[p]) { buf[p] = fs_s[p]; p++; }
    buf[p++] = '0' + (ntmem_fs_count() / 10) % 10;
    buf[p++] = '0' + (ntmem_fs_count() / 1) % 10;
    buf[p] = 0;
    
    log_line("FILESYSTEM", buf);
    log_line("SYSCALL", "STAR/LSTAR MSRs configured");
    log_line("SCHEDULER", "Round-Robin thread manager ready");
    log_line("SECURITY", "Access level: 1/10 (RESTRICTED)");
    
    animate_progress(100);
    delay_ms(400);

    // Финальный экран перед передачей управления
    fb_clear(COL_BG);
    uint32_t h = fb_height();
    uint32_t w = fb_width();

    draw_centered_text("N T O S", 6, h / 2 - 80, COL_ORANGE, COL_BG);

    // Анимация спиннера
    const char *spinner = "|/-\\";
    uint32_t spin_y = h / 2 + 10;
    uint32_t spin_x = (w - 16) / 2; // Центрируем 16x16 (шрифт 8x8 при scale=2)

    for (int i = 0; i < 12; i++) {
        // Очищаем строго ту область, где рисуем (16x16)
        fb_fill_rect(spin_x, spin_y, 16, 16, COL_BG);
        
        fb_set_scale(2); 
        fb_set_cursor(spin_x, spin_y); // Рисуем без сдвигов
        fb_putc(spinner[i % 4], COL_CYAN, COL_BG);
        delay_ms(80);
    }

    // Стираем спиннер окончательно
    fb_fill_rect(spin_x, spin_y, 16, 16, COL_BG);

    draw_centered_text("SYSTEM OPERATIONAL", 2, h / 2 + 40, COL_CYAN, COL_BG);
    draw_centered_text("TRUST THE CORPORATION", 2, h / 2 + 70, COL_MUTED, COL_BG);

    delay_ms(500);
    
    log_set_fb(1);
    fb_set_scale(1);
    fb_clear(0x00101018); 
    fb_set_cursor(0, 0);
}