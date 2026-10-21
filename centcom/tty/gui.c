#include "gui.h"
#include "fb.h"
#include "../driver/keyboard.h"
#include "../driver/mouse.h"
#include "../time/pit.h"
#include "../sched/sched.h"
#include "../mem/pmm.h"
#include "../mem/heap.h"
#include "../fs/ntmem_fs.h"
#include "../fs/ntrfs.h"
#include "../driver/ahci.h"
#include "../driver/sb16.h"
#include "../sound/cue.h"
#include "../sound/synth.h"
#include "../log/serial.h"
#include "../core/panic.h"

// Цветовая палитра в стиле Windows 95 / Nanotrasen
#define GUI_BG_DESKTOP 0x00008080
#define GUI_BAR_BG     0x00C0C0C0
#define GUI_BAR_ACCENT 0x00000080
#define GUI_WIN_BG     0x00C0C0C0
#define GUI_WIN_BORDER 0x00000000
#define GUI_WIN_TITLE  0x00000080
#define GUI_TITLE_INACTIVE 0x00808080
#define GUI_TEXT       0x00000000
#define GUI_TEXT_MUTED 0x00808080
#define GUI_WHITE      0x00FFFFFF
#define GUI_RED        0x00FF0000
#define GUI_GREEN      0x0000FF00
#define GUI_CYAN       0x0000FFFF
#define GUI_ORANGE     0x00FF8000
#define GUI_BTN_BG     0x00A0A0A0
#define GUI_TERM_BG    0x00000000
#define GUI_TERM_FG    0x00C0C0C0

#define MAX_WINDOWS 8
#define TITLE_BAR_H 24

typedef struct window window_t;

struct window {
    int id;
    int x, y, w, h;
    char title[64];
    int is_open;
    
    void (*draw_content)(window_t *win);
    void (*on_click)(window_t *win, int mx, int my);
    void (*on_key)(window_t *win, int key);
};

static window_t g_windows[MAX_WINDOWS];
static int g_next_win_id = 1;

static int g_drag_idx = -1;
static int g_drag_off_x = 0, g_drag_off_y = 0;
static int g_active_win_id = -1;

static int g_exit_to_shell = 0;
static int g_shutdown = 0;
static int g_start_menu_open = 0;

static const uint8_t cursor_bitmap[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,1,1,1,1,1,0,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0}
};

static uint32_t saved_bg[16][16];
static int last_mx = -100, last_my = -100;

// --- Вспомогательные ---
static void put_str(const char *s, uint32_t fg, uint32_t bg) { 
    while (*s) fb_putc(*s++, fg, bg); 
}
static void put_dec(uint64_t v, uint32_t fg, uint32_t bg) {
    char b[24]; int i = 0;
    if (!v) { fb_putc('0', fg, bg); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) fb_putc(b[i], fg, bg);
}
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

// ==========================================
//            ОКОННЫЙ МЕНЕДЖЕР
// ==========================================

static void wm_bring_to_front(int idx) {
    if (idx == MAX_WINDOWS - 1) return;
    window_t tmp = g_windows[idx];
    for (int k = idx; k < MAX_WINDOWS - 1; k++) g_windows[k] = g_windows[k + 1];
    g_windows[MAX_WINDOWS - 1] = tmp;
}

static int wm_create_window(const char *title, int x, int y, int w, int h,
                            void (*draw)(window_t*), void (*click)(window_t*, int, int), void (*key)(window_t*, int)) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_open) {
            g_windows[i].id = g_next_win_id++;
            g_windows[i].x = x; g_windows[i].y = y; g_windows[i].w = w; g_windows[i].h = h;
            copy_str(g_windows[i].title, title, 64);
            g_windows[i].is_open = 1;
            g_windows[i].draw_content = draw;
            g_windows[i].on_click = click;
            g_windows[i].on_key = key;
            wm_bring_to_front(i);
            g_active_win_id = g_windows[MAX_WINDOWS - 1].id;
            return g_windows[MAX_WINDOWS - 1].id;
        }
    }
    return -1;
}

// ==========================================
//            ПРИЛОЖЕНИЯ (APPS)
// ==========================================

// --- 1. Terminal ---
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
        term_add_line(line);
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
        term_add_line_ex("--- FILESYSTEM ---", TERM_TYPE_HEADER);
        term_add_line("  fs              - list files in initramfs (ntmem_fs)");
        term_add_line("  cat <n>         - hex dump of initramfs file by index");
        term_add_line("  disk            - read sector 0 from SATA drive");
        term_add_line("  format          - format disk (NTRFS)");
        term_add_line("  mount           - mount disk (NTRFS)");
        term_add_line("  dls             - list files on disk");
        term_add_line("  dread <name>    - hex dump of disk file");
        term_add_line("  dsave <i> <name>- save initramfs file to disk");
        term_add_line_ex("--- PROCESSES ---", TERM_TYPE_HEADER);
        term_add_line("  run <name>      - run ELF and wait for exit");
        term_add_line("  spawn <name>    - start ELF in background");
        term_add_line("  kill <pid>      - terminate process (0 = panic)");
        term_add_line("  hello           - run hello.elf");
        term_add_line("  procs           - list running processes");
        term_add_line_ex("--- AUDIO ---", TERM_TYPE_HEADER);
        term_add_line("  chord           - play a C major chord");
        term_add_line("  melody          - play a short melody");
        term_add_line("  cue <n>         - play a cue (1..7)");
        term_add_line("  beep <f> <ms>   - SB16 tone");
        term_add_line("  sound [stop]    - show status or stop sound");
        term_add_line_ex("--- MISC ---", TERM_TYPE_HEADER);
        term_add_line("  clear           - clear terminal");
        term_add_line("  panic           - trigger kernel panic");
        term_add_line("  halt            - halt the CPU");
    } 
    else if (gui_strcmp(arg, "mem") == 0) {
        char b[64]; copy_str(b, " total RAM: ", 64); int p = 12;
        uint64_t mb = (pmm_total_pages() * 4) / 1024;
        b[p++] = '0' + (mb/100)%10; b[p++] = '0' + (mb/10)%10; b[p++] = '0' + (mb/1)%10;
        b[p++]='M'; b[p++]='B'; b[p]=0; term_add_line(b);
        copy_str(b, " free RAM:  ", 64); p = 12;
        mb = (pmm_free_pages() * 4) / 1024;
        b[p++] = '0' + (mb/100)%10; b[p++] = '0' + (mb/10)%10; b[p++] = '0' + (mb/1)%10;
        b[p++]='M'; b[p++]='B'; b[p]=0; term_add_line(b);
    }
    else if (gui_strcmp(arg, "heap") == 0) {
        char b[64]; copy_str(b, " heap used: ", 64); int p = 11;
        uint64_t u = heap_used();
        char rev[24]; int r = 0; if(!u){rev[r++]='0';} while(u){rev[r++]='0'+(u%10);u/=10;}
        while(r--) b[p++]=rev[r]; b[p]=0; term_add_line(b);
    }
    else if (gui_strcmp(arg, "ticks") == 0) {
        char b[64]; copy_str(b, " ticks: ", 64); int p = 8;
        uint64_t t = pit_ticks();
        char rev[24]; int r = 0; if(!t){rev[r++]='0';} while(t){rev[r++]='0'+(t%10);t/=10;}
        while(r--) b[p++]=rev[r]; b[p]=0; term_add_line(b);
    }
    else if (gui_strcmp(arg, "fs") == 0) {
        int n = ntmem_fs_count();
        char b[64]; copy_str(b, " ntmem_fs: ", 64); int p = 11;
        b[p++] = '0' + (n/10)%10; b[p++] = '0' + (n/1)%10; b[p]=0; term_add_line(b);
        for(int i=0; i<n && i<5; i++) {
            ntmem_file_t *f = ntmem_fs_get(i);
            if (f) { char fb[64]; copy_str(fb, "  ", 64); copy_str(fb+2, f->name, 62); term_add_line(fb); }
        }
        if (n > 5) term_add_line("  ...");
    }
    else if (gui_strncmp(arg, "cat ", 4) == 0) {
        int idx = 0; char *p = arg + 4; while(*p >= '0' && *p <= '9') { idx = idx*10 + (*p - '0'); p++; }
        ntmem_file_t *f = ntmem_fs_get(idx);
        if (!f) { term_add_line_ex(" no such file", TERM_TYPE_ERR); return; }
        term_add_line_ex(" --- hex dump ---", TERM_TYPE_HEADER);
        term_hexdump(0, f->data, f->size > 128 ? 128 : f->size);
    }
    else if (gui_strcmp(arg, "disk") == 0) {
        static uint8_t buf[512];
        term_add_line_ex(" disk: reading sector 0...", TERM_TYPE_HEADER);
        if (ahci_read_sector(0, buf)) term_hexdump(0, buf, 64);
        else term_add_line_ex(" disk read: FAILED", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "format") == 0) {
        if (ntrfs_format()) term_add_line(" NTRFS formatted");
        else term_add_line_ex(" format failed", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "mount") == 0) {
        if (ntrfs_mount()) term_add_line(" NTRFS mounted");
        else term_add_line_ex(" mount failed", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "dls") == 0) {
        if (!ntrfs_is_mounted()) { term_add_line_ex(" not mounted", TERM_TYPE_ERR); }
        else {
            int count = 0;
            for (int i = 0; ; i++) {
                ntrfs_inode_t in;
                if (ntrfs_list(i, &in) <= 0) break;
                char b[64]; copy_str(b, "  ", 64); copy_str(b+2, in.name, 62); term_add_line(b);
                count++;
            }
            if (count == 0) term_add_line(" (empty disk)");
        }
    }
    else if (gui_strncmp(arg, "dread ", 6) == 0) {
        if (!ntrfs_is_mounted()) { term_add_line_ex(" not mounted", TERM_TYPE_ERR); return; }
        static uint8_t buf[8192];
        int n = ntrfs_read(arg + 6, buf, sizeof(buf));
        if (n < 0) { term_add_line_ex(" not found", TERM_TYPE_ERR); return; }
        term_add_line_ex(" --- disk hex dump ---", TERM_TYPE_HEADER);
        term_hexdump(0, buf, n > 128 ? 128 : n);
    }
    else if (gui_strncmp(arg, "dsave ", 6) == 0) {
        if (!ntrfs_is_mounted()) { term_add_line_ex(" not mounted", TERM_TYPE_ERR); return; }
        int idx = 0; char *p = arg + 6; while(*p >= '0' && *p <= '9') { idx = idx*10 + (*p - '0'); p++; }
        while(*p == ' ') p++;
        if (!*p) { term_add_line_ex(" usage: dsave <i> <name>", TERM_TYPE_ERR); return; }
        ntmem_file_t *f = ntmem_fs_get(idx);
        if (!f) { term_add_line_ex(" no such ntmem file", TERM_TYPE_ERR); return; }
        if (ntrfs_create(p, f->data, f->size)) term_add_line(" saved to disk");
        else term_add_line_ex(" save failed", TERM_TYPE_ERR);
    }
    else if (gui_strncmp(arg, "spawn ", 6) == 0) {
        char *p = arg + 6; while(*p==' ')p++;
        ntmem_file_t *f = ntmem_fs_find(p);
        if (f) { term_add_line(" starting app..."); process_create_user(f->data, f->size, 1, (const char**)&p); }
        else term_add_line_ex(" file not found", TERM_TYPE_ERR);
    }
    else if (gui_strncmp(arg, "run ", 4) == 0) {
        char *p = arg + 4; while(*p==' ')p++;
        ntmem_file_t *f = ntmem_fs_find(p);
        if (f) {
            term_add_line(" running app...");
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&p);
            if (p2) {
                while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); }
                term_add_line(" app exited");
                process_reap(p2);
            }
        } else term_add_line_ex(" file not found", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "hello") == 0) {
        char *cmd = "hello.elf";
        ntmem_file_t *f = ntmem_fs_find(cmd);
        if (f) {
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&cmd);
            if (p2) {
                while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); }
                process_reap(p2);
            }
        }
    }
    else if (gui_strncmp(arg, "kill ", 5) == 0) {
        int pid = 0; char *p = arg + 5; while(*p >= '0' && *p <= '9') { pid = pid*10 + (*p - '0'); p++; }
        if (pid == 0) {
            term_add_line_ex(" Why did you killed 0? you are monster.", TERM_TYPE_ERR);
            __asm__ volatile("int $3");
            centcom_panic();
        }
        else { process_kill((uint64_t)pid); term_add_line(" killed"); }
    }
    else if (gui_strcmp(arg, "procs") == 0) {
        for(int i=0; i<PROC_MAX; i++) {
            process_t *pr = process_get(i);
            if (pr && pr->state != PROC_FREE) {
                char b[64]; copy_str(b, "  ", 64);
                int idx = 2;
                uint64_t id = pr->pid;
                char rev[16]; int r = 0; 
                if (id == 0) rev[r++] = '0'; // <-- ФИКС ДЛЯ PID=0
                else while(id) { rev[r++]='0'+(id%10); id/=10; }
                while(r--) b[idx++] = rev[r];
                b[idx++] = ' '; copy_str(b+idx, pr->name, 64-idx);
                term_add_line(b);
            }
        }
    }
    else if (gui_strcmp(arg, "chord") == 0) {
        static uint8_t pcm[65536];
        sb16_stop(); synth_init();
        synth_note_on(0, 262, WAVE_SINE, 100);
        synth_note_on(1, 330, WAVE_SINE, 100);
        synth_note_on(2, 392, WAVE_SINE, 100);
        uint32_t samples = (22050 * 1000) / 1000;
        synth_render(pcm, samples);
        sb16_play_pcm_async(pcm, samples);
        term_add_line(" playing C major chord...");
    }
    else if (gui_strcmp(arg, "melody") == 0) {
        static uint8_t pcm[65536];
        int offset = 0, note_len = 4000, release_len = 1500;
        sb16_stop(); synth_init();
        uint32_t freqs[] = { 262, 330, 392, 523 };
        int lens[] = { note_len, note_len, note_len, note_len * 2 };
        for (int i = 0; i < 4; i++) {
            if (offset + lens[i] > 60000) break;
            synth_note_on(0, freqs[i], WAVE_SINE, 100);
            synth_render(pcm + offset, lens[i]);
            offset += lens[i];
            synth_note_off(0);
            if (offset + release_len > 60000) break;
            synth_render(pcm + offset, release_len);
            offset += release_len;
        }
        sb16_play_pcm_async(pcm, offset);
        term_add_line(" playing melody...");
    }
    else if (gui_strncmp(arg, "cue ", 4) == 0) {
        int idx = 0; char *p = arg + 4; while(*p >= '0' && *p <= '9') { idx = idx*10 + (*p - '0'); p++; }
        if (idx == 0) { cue_stop(); term_add_line(" cue stopped"); }
        else if (idx >= 1 && idx <= 7) { cue_play((uint8_t)idx); term_add_line(" playing cue"); }
        else term_add_line_ex(" cue code 1..7", TERM_TYPE_ERR);
    }
    else if (gui_strncmp(arg, "beep ", 5) == 0) {
        int freq = 0, ms = 0; char *p = arg + 5;
        while(*p >= '0' && *p <= '9') { freq = freq*10 + (*p - '0'); p++; }
        while(*p == ' ') p++;
        while(*p >= '0' && *p <= '9') { ms = ms*10 + (*p - '0'); p++; }
        if (freq > 0 && ms > 0) { sb16_play_tone_async(freq, ms); term_add_line(" beep started"); }
        else term_add_line_ex(" usage: beep <freq> <ms>", TERM_TYPE_ERR);
    }
    else if (gui_strncmp(arg, "sound", 5) == 0) {
        if (gui_strcmp(arg + 5, " stop") == 0) { sb16_stop(); term_add_line(" sound stopped"); }
        else {
            if (sb16_is_playing()) term_add_line(" SB16: PLAYING");
            else term_add_line(" SB16: IDLE");
        }
    }
    else if (gui_strcmp(arg, "clear") == 0) g_term_line_count = 0;
    else if (gui_strcmp(arg, "panic") == 0) {
        __asm__ volatile("int $3");
        centcom_panic();
    }
    else if (gui_strcmp(arg, "halt") == 0) { term_add_line_ex(" system halted.", TERM_TYPE_ERR); for(;;) __asm__ volatile("hlt"); }
    else if (g_term_input_len > 0) term_add_line_ex(" unknown command", TERM_TYPE_ERR);

    for (int i = 0; i < 256; i++) g_term_input[i] = 0;
    g_term_input_len = 0;
    (void)win;
}

static void app_term_draw(window_t *win) {
    int max_chars = (win->w - 16) / 8;
    int start_x = win->x + 8;
    int start_y = win->y + TITLE_BAR_H + 4;
    
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, GUI_TERM_BG);

    int max_visible_lines = (win->h - TITLE_BAR_H - 4 - 20) / 10;
    if (max_visible_lines < 1) max_visible_lines = 1;

    int start_idx = 0;
    if (g_term_line_count > max_visible_lines) {
        start_idx = g_term_line_count - max_visible_lines;
    }

    int cy = start_y;
    for (int i = start_idx; i < g_term_line_count; i++) {
        const char *s = g_term_lines[i];
        int cx = start_x;
        int chars = 0;
        
        uint32_t line_fg = GUI_TERM_FG;
        if (g_term_line_types[i] == TERM_TYPE_ERR) line_fg = GUI_RED;
        else if (g_term_line_types[i] == TERM_TYPE_HEADER) line_fg = GUI_ORANGE;
        else if (g_term_line_types[i] == TERM_TYPE_OUT) line_fg = GUI_WHITE;

        while (*s) {
            if (chars >= max_chars) {
                cx = start_x;
                cy += 10;
                chars = 0;
            }
            
            if (cy > win->y + win->h - 20) break; 

            fb_set_cursor(cx, cy);
            
            uint32_t fg = line_fg;
            if (g_term_line_types[i] == TERM_TYPE_CMD) {
                fg = (chars < 9) ? GUI_GREEN : GUI_WHITE;
            }
            
            fb_putc(*s++, fg, GUI_TERM_BG);
            cx += 8;
            chars++;
        }
        cy += 10;
        
        if (cy > win->y + win->h - 20) break;
    }

    if (cy <= win->y + win->h - 12) {
        int cx = start_x;
        int chars = 0;
        
        fb_set_cursor(cx, cy);
        put_str("centcom> ", GUI_GREEN, GUI_TERM_BG);
        cx += 9 * 8;
        chars = 9;

        for (int i = 0; i < g_term_input_len; i++) {
            if (chars >= max_chars) {
                cx = start_x;
                cy += 10;
                chars = 0;
                if (cy > win->y + win->h - 12) break;
            }
            fb_set_cursor(cx, cy);
            fb_putc(g_term_input[i], GUI_WHITE, GUI_TERM_BG);
            cx += 8;
            chars++;
        }

        if (cy <= win->y + win->h - 12) {
            if (chars >= max_chars) {
                cx = start_x;
                cy += 10;
            }
            fb_fill_rect(cx, cy, 8, 8, GUI_WHITE);
        }
    }
}

static void app_term_key(window_t *win, int c) {
    if (c == '\b') {
        if (g_term_input_len > 0) {
            g_term_input_len--;
            g_term_input[g_term_input_len] = 0;
        }
    } else if (c == '\n') {
        term_execute(win);
    } else if (c >= 32 && c < 127) {
        if (g_term_input_len < 255) {
            g_term_input[g_term_input_len++] = c;
            g_term_input[g_term_input_len] = 0;
        }
    }
}

// --- 2. Task Manager ---
static void app_taskman_draw(window_t *win) {
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, GUI_WIN_BG);

    fb_set_cursor(win->x + 16, win->y + 40);
    put_str("TASK MANAGER", GUI_WIN_TITLE, GUI_WIN_BG);
    fb_fill_rect(win->x + 16, win->y + 56, win->w - 32, 1, GUI_TEXT_MUTED);
    fb_set_cursor(win->x + 16, win->y + 72);
    put_str("PID  NAME", GUI_TEXT, GUI_WIN_BG);
    
    int y = win->y + 88;
    for (int i = 0; i < PROC_MAX && y < win->y + win->h - 20; i++) {
        process_t *p = process_get(i);
        if (!p || p->state == PROC_FREE) continue;
        fb_set_cursor(win->x + 16, y);
        put_dec(p->pid, GUI_TEXT_MUTED, GUI_WIN_BG);
        fb_set_cursor(win->x + 56, y);
        if (p->state == PROC_RUNNING) put_str(p->name, GUI_GREEN, GUI_WIN_BG);
        else if (p->state == PROC_ZOMBIE) put_str(p->name, GUI_RED, GUI_WIN_BG);
        else put_str(p->name, GUI_TEXT, GUI_WIN_BG);
        y += 12;
    }
}

// ==========================================
//            ОТРИСОВКА
// ==========================================

static void draw_top_bar(void) {
    uint32_t w = fb_width();
    fb_fill_rect(0, 0, w, 24, GUI_BAR_BG);
    fb_fill_rect(0, 24, w, 2, GUI_TEXT_MUTED);
    fb_fill_rect(0, 0, 120, 24, GUI_BAR_ACCENT); 
    fb_set_cursor(16, 8);
    put_str("NANOTRASEN", GUI_WHITE, GUI_BAR_ACCENT);
    
    fb_set_cursor(w - 100, 8);
    put_str("UPTIME: ", GUI_TEXT, GUI_BAR_BG);
    uint64_t sec = pit_ticks() / 100;
    char time_str[16];
    time_str[0] = '0' + (sec / 100) % 10;
    time_str[1] = '0' + (sec / 10) % 10;
    time_str[2] = '0' + (sec % 10);
    time_str[3] = 's'; time_str[4] = 0;
    put_str(time_str, GUI_TEXT, GUI_BAR_BG);
}

static void draw_start_menu(void) {
    fb_fill_rect(0, 26, 200, 100, GUI_WIN_BG);
    fb_draw_box(0, 26, 200, 100, GUI_WIN_BORDER);
    fb_set_cursor(10, 36); put_str("TERMINAL", GUI_TEXT, GUI_WIN_BG);
    fb_set_cursor(10, 56); put_str("TASK MANAGER", GUI_TEXT, GUI_WIN_BG);
    fb_set_cursor(10, 76); put_str("CLASSIC SHELL", GUI_TEXT, GUI_WIN_BG);
    fb_set_cursor(10, 96); put_str("SHUTDOWN", GUI_TEXT, GUI_WIN_BG);
}

static void draw_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_open) continue;
        window_t *win = &g_windows[i];
        
        fb_fill_rect(win->x + 4, win->y + 4, win->w, win->h, 0x00000000);
        fb_fill_rect(win->x, win->y, win->w, win->h, GUI_WIN_BG);
        
        uint32_t border = (win->id == g_active_win_id) ? GUI_WIN_BORDER : GUI_TEXT_MUTED;
        fb_draw_box(win->x, win->y, win->w, win->h, border);
        
        uint32_t title_bg = (win->id == g_active_win_id) ? GUI_WIN_TITLE : GUI_TITLE_INACTIVE;
        fb_fill_rect(win->x + 1, win->y + 1, win->w - 2, TITLE_BAR_H - 4, title_bg);
        fb_set_cursor(win->x + 8, win->y + 8);
        put_str(win->title, GUI_WHITE, title_bg);
        
        fb_fill_rect(win->x + win->w - 20, win->y + 4, 16, 16, GUI_BTN_BG);
        fb_draw_box(win->x + win->w - 20, win->y + 4, 16, 16, GUI_WIN_BORDER);
        fb_set_cursor(win->x + win->w - 16, win->y + 8);
        put_str("X", GUI_TEXT, GUI_BTN_BG);

        if (win->draw_content) win->draw_content(win);
    }
}

static void restore_cursor_bg_direct(void) {
    if (last_mx == -100) return;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t *ptr = (uint32_t*)fb_get_real_ptr(last_mx + x, last_my + y);
        if (ptr) *ptr = saved_bg[y][x];
    }
}

static void draw_cursor_direct(int mx, int my) {
    if (mx < 0 || my < 0 || mx + 16 >= (int)fb_width() || my + 16 >= (int)fb_height()) return;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t *ptr = (uint32_t*)fb_get_real_ptr(mx + x, my + y);
        if (!ptr) continue;
        saved_bg[y][x] = *ptr; 
        if (cursor_bitmap[y][x] == 1) *ptr = 0x00000000;
        else if (cursor_bitmap[y][x] == 2) *ptr = 0x00FFFFFF;
    }
    last_mx = mx; last_my = my;
}

static void redraw_gui(int mx, int my) {
    fb_clear(GUI_BG_DESKTOP);    
    draw_top_bar();
    draw_windows();
    if (g_start_menu_open) draw_start_menu();
    fb_swap(); 
    last_mx = -100; 
    draw_cursor_direct(mx, my); 
}

// ==========================================
//            ГЛАВНЫЙ ЦИКЛ
// ==========================================

int gui_start(void) {
    uint32_t buf_size = fb_width() * fb_height() * 4;
    uint32_t *gui_buf = (uint32_t*)kmalloc(buf_size);
    if (gui_buf) fb_set_buffer(gui_buf);

    for (int i = 0; i < MAX_WINDOWS; i++) g_windows[i].is_open = 0;
    g_term_line_count = 0;
    term_add_line_ex("NtOS v0.15.2 - GUI Shell", TERM_TYPE_HEADER);
    term_add_line("Type 'help' for commands.");

    wm_create_window("CENTCOM TERMINAL", 50, 100, 500, 350, app_term_draw, 0, app_term_key);
    wm_create_window("TASK MANAGER", 580, 100, 300, 250, app_taskman_draw, 0, 0);

    redraw_gui(400, 300);

    int last_left = 0;
    uint64_t last_sec = pit_ticks() / 100;
    g_exit_to_shell = 0;
    g_shutdown = 0;
    g_start_menu_open = 0;

    while (!g_exit_to_shell && !g_shutdown) {
        __asm__ volatile("sti"); 

        int c = keyboard_getchar();
        if (c == 27) break;

        if (c && g_active_win_id >= 0) {
            window_t *win = &g_windows[MAX_WINDOWS - 1];
            if (win->is_open && win->on_key) {
                win->on_key(win, c);
                win->draw_content(win);
                
                restore_cursor_bg_direct();
                fb_swap_rect(win->x, win->y, win->w, win->h);
                draw_cursor_direct(mouse_get_state().x, mouse_get_state().y);
            }
        }

        uint64_t current_sec = pit_ticks() / 100;
        if (current_sec != last_sec) {
            last_sec = current_sec;
            draw_top_bar();
            fb_swap_rect(0, 0, fb_width(), 26);
        }

        mouse_state_t m = mouse_get_state();
        
        if (m.left && !last_left) {
            if (m.y <= 24 && m.x <= 120) {
                g_start_menu_open = 1;
                redraw_gui(m.x, m.y);
            } else if (g_start_menu_open) {
                if (m.x < 200 && m.y > 26 && m.y < 126) {
                    if (m.y >= 36 && m.y <= 50) { g_start_menu_open = 0; wm_create_window("CENTCOM TERMINAL", 100, 100, 500, 350, app_term_draw, 0, app_term_key); }
                    else if (m.y >= 56 && m.y <= 70) { g_start_menu_open = 0; wm_create_window("TASK MANAGER", 150, 150, 300, 250, app_taskman_draw, 0, 0); }
                    else if (m.y >= 76 && m.y <= 90) { g_start_menu_open = 0; g_exit_to_shell = 1; }
                    else if (m.y >= 96 && m.y <= 110) { g_start_menu_open = 0; g_shutdown = 1; }
                } else {
                    g_start_menu_open = 0;
                }
                redraw_gui(m.x, m.y);
                if (g_exit_to_shell || g_shutdown) break;
            } else {
                for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                    if (!g_windows[i].is_open) continue;
                    window_t *win = &g_windows[i];

                    if (m.x >= win->x && m.x <= win->x + win->w &&
                        m.y >= win->y && m.y <= win->y + win->h) {
                        
                        g_active_win_id = win->id;
                        wm_bring_to_front(i);
                        win = &g_windows[MAX_WINDOWS - 1];
                        
                        if (m.y <= win->y + TITLE_BAR_H) {
                            if (m.x >= win->x + win->w - 20 && m.x <= win->x + win->w - 4) {
                                win->is_open = 0;
                                g_active_win_id = -1;
                            } else {
                                g_drag_idx = MAX_WINDOWS - 1;
                                g_drag_off_x = m.x - win->x;
                                g_drag_off_y = m.y - win->y;
                            }
                        } else if (win->on_click) {
                            win->on_click(win, m.x, m.y);
                        }
                        
                        redraw_gui(m.x, m.y);
                        break;
                    }
                }
            }
        }

        if (!m.left && last_left) g_drag_idx = -1;

        if (m.left && g_drag_idx >= 0) {
            window_t *win = &g_windows[g_drag_idx];
            int new_x = m.x - g_drag_off_x;
            int new_y = m.y - g_drag_off_y;

            if (new_x < 0) new_x = 0;
            if (new_y < 24) new_y = 24;
            if (new_x + win->w > (int)fb_width()) new_x = fb_width() - win->w;
            if (new_y + win->h > (int)fb_height()) new_y = fb_height() - win->h;

            int dx = new_x - win->x; if (dx < 0) dx = -dx;
            int dy = new_y - win->y; if (dy < 0) dy = -dy;

            if (dx >= 4 || dy >= 4) {
                win->x = new_x;
                win->y = new_y;
                redraw_gui(m.x, m.y);
            }
        }

        if (m.x != last_mx || m.y != last_my) {
            restore_cursor_bg_direct();
            draw_cursor_direct(m.x, m.y);
        }

        last_left = m.left;
        __asm__ volatile("hlt"); 
    }

    __asm__ volatile("cli");
    restore_cursor_bg_direct();
    fb_set_buffer(0); 
    fb_clear(0x00101018);
    fb_set_cursor(0, 0);
    __asm__ volatile("sti");
    
    return g_exit_to_shell;
}