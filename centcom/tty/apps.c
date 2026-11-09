#include "apps.h"
#include "fb.h"
#include "../sched/sched.h"
#include "../mem/pmm.h"
#include "../mem/heap.h"
#include "../fs/ntmem_fs.h"
#include "../fs/ntrfs.h"
#include "../driver/ahci.h"
#include "../driver/sb16.h"
#include "../sound/cue.h"
#include "../sound/synth.h"
#include "../time/pit.h"
#include "../core/panic.h"

// ==========================================
// 1. ТЕРМИНАЛ
// ==========================================
#define TERM_LINES 200
#define TERM_COLS 256
static char g_term_lines[TERM_LINES][TERM_COLS];
static int g_term_line_count = 0;
static char g_term_input[256];
static int g_term_input_len = 0;

#define TERM_TYPE_CMD 0
#define TERM_TYPE_OUT 1
#define TERM_TYPE_ERR 2
#define TERM_TYPE_HEADER 3
static int g_term_line_types[TERM_LINES];

static void put_str(const char *s, uint32_t fg, uint32_t bg) { while (*s) fb_putc(*s++, fg, bg); }
static void copy_str(char *dst, const char *src, int max) {
    int i = 0; while (src[i] && i < max - 1) { dst[i] = src[i]; i++; } dst[i] = 0;
}
static int gui_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int gui_strncmp(const char *a, const char *b, int n) {
    while (n && *a && *b && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static void term_add_line_ex(const char *s, int type) {
    if (g_term_line_count < TERM_LINES) {
        copy_str(g_term_lines[g_term_line_count], s, TERM_COLS);
        g_term_line_types[g_term_line_count] = type;
        g_term_line_count++;
    } else {
        for (int i = 0; i < TERM_LINES - 1; i++) {
            copy_str(g_term_lines[i], g_term_lines[i+1], TERM_COLS);
            g_term_line_types[i] = g_term_line_types[i+1];
        }
        copy_str(g_term_lines[TERM_LINES - 1], s, TERM_COLS);
        g_term_line_types[TERM_LINES - 1] = type;
    }
}
static void term_add_line(const char *s) { term_add_line_ex(s, TERM_TYPE_OUT); }

static void app_term_init(void) {
    g_term_line_count = 0;
    term_add_line_ex("NtOS v0.15.2 - GUI Shell", TERM_TYPE_HEADER);
    term_add_line_ex("Type 'help' for commands.", TERM_TYPE_OUT);
}

void app_term_write(const char *s, uint64_t len) {
    char line[256];
    int l = 0;
    for (uint64_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            line[l] = 0;
            if (l > 0) term_add_line_ex(line, TERM_TYPE_OUT);
            l = 0;
        } else {
            line[l++] = s[i];
            if (l >= 255) { line[l] = 0; term_add_line_ex(line, TERM_TYPE_OUT); l = 0; }
        }
    }
    if (l > 0) { line[l] = 0; term_add_line_ex(line, TERM_TYPE_OUT); }
}

static void term_hexdump(uint64_t addr, const uint8_t *data, int len) {
    char line[TERM_COLS];
    for (int i = 0; i < len; i += 8) {
        int p = 0;
        const char *hex = "0123456789abcdef";
        line[p++] = hex[(addr >> 28) & 0xF]; line[p++] = hex[(addr >> 24) & 0xF];
        line[p++] = hex[(addr >> 20) & 0xF]; line[p++] = hex[(addr >> 16) & 0xF];
        line[p++] = hex[(addr >> 12) & 0xF]; line[p++] = hex[(addr >> 8) & 0xF];
        line[p++] = hex[(addr >> 4) & 0xF]; line[p++] = hex[addr & 0xF];
        line[p++] = ':'; line[p++] = ' ';
        for (int j = 0; j < 8; j++) {
            if (i + j < len) {
                uint8_t b = data[i+j];
                line[p++] = hex[(b >> 4) & 0xF]; line[p++] = hex[b & 0xF];
            } else { line[p++] = ' '; line[p++] = ' '; }
            line[p++] = ' ';
        }
        line[p++] = '|';
        for (int j = 0; j < 8; j++) {
            if (i + j < len) {
                uint8_t b = data[i+j];
                line[p++] = (b >= 32 && b < 127) ? b : '.';
            } else line[p++] = ' ';
        }
        line[p++] = '|'; line[p] = 0;
        term_add_line_ex(line, TERM_TYPE_OUT);
        addr += 8;
    }
}

static void term_execute(window_t *win) {
    g_term_input[g_term_input_len] = 0;
    char cmd_buf[TERM_COLS];
    copy_str(cmd_buf, "centcom> ", TERM_COLS);
    int len = 9;
    for(int i=0; i < g_term_input_len && len < TERM_COLS-1; i++) cmd_buf[len++] = g_term_input[i];
    cmd_buf[len] = 0;
    term_add_line_ex(cmd_buf, TERM_TYPE_CMD);

    char *arg = g_term_input;
    while (*arg == ' ') arg++;

    if (gui_strcmp(arg, "help") == 0) {
        term_add_line_ex("--- SYSTEM ---", TERM_TYPE_HEADER);
        term_add_line("  mem             - memory statistics");
        term_add_line("  heap            - heap usage");
        term_add_line("  ticks           - PIT tick counter");
        term_add_line("  procs           - list running processes");
        term_add_line("  clear           - clear terminal");
        term_add_line("  panic           - trigger kernel panic");
        term_add_line_ex("--- HYBRID IPC ---", TERM_TYPE_HEADER);
        term_add_line("  vfs_start       - start userspace vfs_server");
        term_add_line("  vfs_test        - test VFS via IPC");
        term_add_line_ex("--- MISC ---", TERM_TYPE_HEADER);
        term_add_line("  run <name>      - run ELF");
    } 
    else if (gui_strcmp(arg, "mem") == 0) {
        char b[64]; copy_str(b, " total RAM: ", 64); int p = 12;
        uint64_t mb = (pmm_total_pages() * 4) / 1024;
        b[p++] = '0' + (mb/100)%10; b[p++] = '0' + (mb/10)%10; b[p++] = '0' + (mb/1)%10;
        b[p++]='M'; b[p++]='B'; b[p]=0; term_add_line(b);
    }
    else if (gui_strcmp(arg, "clear") == 0) g_term_line_count = 0;
    else if (gui_strcmp(arg, "panic") == 0) { __asm__ volatile("int $3"); centcom_panic(); }
    else if (gui_strncmp(arg, "run ", 4) == 0) {
        char *p = arg + 4; while(*p==' ')p++;
        ntmem_file_t *f = ntmem_fs_find(p);
        if (f) {
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&p);
            if (p2) {
                while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); }
                process_reap(p2);
            }
        }
    }
    else if (gui_strcmp(arg, "vfs_start") == 0) {
        char *cmd = "vfs_server.elf";
        ntmem_file_t *f = ntmem_fs_find(cmd);
        if (f) process_create_user(f->data, f->size, 1, (const char**)&cmd);
    }
    else if (g_term_input_len > 0) term_add_line_ex(" unknown command", TERM_TYPE_ERR);

    for (int i = 0; i < 256; i++) g_term_input[i] = 0;
    g_term_input_len = 0;
    (void)win;
}

static void app_term_draw(window_t *win) {
    int max_chars = (win->w - 16) / 8;
    int start_x = win->x + 8;
    int start_y = win->y + TITLE_BAR_H + 4;
    
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, 0x00000000);
    int max_visible_lines = (win->h - TITLE_BAR_H - 4 - 20) / 10;
    if (max_visible_lines < 1) max_visible_lines = 1;
    int start_idx = 0;
    if (g_term_line_count > max_visible_lines) start_idx = g_term_line_count - max_visible_lines;

    int cy = start_y;
    for (int i = start_idx; i < g_term_line_count; i++) {
        const char *s = g_term_lines[i];
        int cx = start_x;
        int chars = 0;
        uint32_t line_fg = 0x00C0C0C0;
        if (g_term_line_types[i] == TERM_TYPE_ERR) line_fg = 0x00FF3333;
        else if (g_term_line_types[i] == TERM_TYPE_HEADER) line_fg = 0x00FFAA00;

        while (*s) {
            if (chars >= max_chars || cx + 8 > win->x + win->w - 2) { cx = start_x; cy += 10; chars = 0; }
            if (cy > win->y + win->h - 20) break; 
            fb_set_cursor(cx, cy);
            uint32_t fg = line_fg;
            if (g_term_line_types[i] == TERM_TYPE_CMD) fg = (chars < 9) ? 0x0044FF88 : 0x00FFFFFF;
            fb_putc(*s++, fg, 0x00000000);
            cx += 8; chars++;
        }
        cy += 10;
        if (cy > win->y + win->h - 20) break;
    }

    if (cy <= win->y + win->h - 12) {
        int cx = start_x, chars = 0;
        fb_set_cursor(cx, cy);
        put_str("centcom> ", 0x0044FF88, 0x00000000);
        cx += 9 * 8; chars = 9;
        for (int i = 0; i < g_term_input_len; i++) {
            if (chars >= max_chars || cx + 8 > win->x + win->w - 2) { cx = start_x; cy += 10; chars = 0; if (cy > win->y + win->h - 12) break; }
            fb_set_cursor(cx, cy);
            fb_putc(g_term_input[i], 0x00FFFFFF, 0x00000000);
            cx += 8; chars++;
        }
        if (cy <= win->y + win->h - 12) {
            if (chars >= max_chars || cx + 8 > win->x + win->w - 2) { cx = start_x; cy += 10; }
            fb_fill_rect(cx, cy, 8, 8, 0x00FFFFFF);
        }
    }
}

static void app_term_key(window_t *win, int c) {
    if (c == '\b') { if (g_term_input_len > 0) { g_term_input_len--; g_term_input[g_term_input_len] = 0; } }
    else if (c == '\n') { term_execute(win); }
    else if (c >= 32 && c < 127) {
        if (g_term_input_len < 255) { g_term_input[g_term_input_len++] = c; g_term_input[g_term_input_len] = 0; }
    }
}

// ==========================================
// 2. TASK MANAGER
// ==========================================
static void app_taskman_draw(window_t *win) {
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, 0x00141A20);
    fb_set_cursor(win->x + 16, win->y + 40);
    put_str("TASK MANAGER", 0x0044DDFF, 0x00141A20);
    fb_fill_rect(win->x + 16, win->y + 56, win->w - 32, 1, 0x00808080);
    fb_set_cursor(win->x + 16, win->y + 72);
    put_str("PID  NAME", 0x00D0D0D0, 0x00141A20);
    
    int y = win->y + 88;
    for (int i = 0; i < PROC_MAX && y < win->y + win->h - 20; i++) {
        process_t *p = process_get(i);
        if (!p || p->state == PROC_FREE) continue;
        fb_set_cursor(win->x + 16, y);
        fb_putc('0' + (p->pid / 10) % 10, 0x00808080, 0x00141A20);
        fb_putc('0' + (p->pid % 10), 0x00808080, 0x00141A20);
        fb_set_cursor(win->x + 56, y);
        if (p->state == PROC_RUNNING) put_str(p->name, 0x0044FF88, 0x00141A20);
        else put_str(p->name, 0x00D0D0D0, 0x00141A20);
        y += 12;
    }
}

// ==========================================
// 3. TEXT EDITOR
// ==========================================
static void app_editor_draw(window_t *win) {
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, 0x00141A20);
    fb_set_cursor(win->x + 16, win->y + 40);
    put_str("TEXT EDITOR (Not Implemented)", 0x00FFAA00, 0x00141A20);
}
static void app_editor_key(window_t *win, int c) { (void)win; (void)c; }

// ==========================================
// РЕГИСТРАЦИЯ ПРИЛОЖЕНИЙ
// ==========================================
app_meta_t app_terminal = {
    .name = "CENTCOM TERMINAL",
    .icon_text = ">_",
    .icon_color = 0x0044FF88,
    .init = app_term_init,
    .draw = app_term_draw,
    .key = app_term_key
};

app_meta_t app_taskman = {
    .name = "TASK MANAGER",
    .icon_text = "|||",
    .icon_color = 0x00FF3333,
    .draw = app_taskman_draw
};

app_meta_t app_editor = {
    .name = "TEXT EDITOR",
    .icon_text = "TXT",
    .icon_color = 0x00FFAA00,
    .draw = app_editor_draw,
    .key = app_editor_key
};