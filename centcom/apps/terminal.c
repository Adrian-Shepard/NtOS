#include "../gui/apps.h"
#include "../tty/fb.h"
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

void app_term_init(void) {
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
        term_add_line_ex("--- FILESYSTEM ---", TERM_TYPE_HEADER);
        term_add_line("  fs              - list files in initramfs");
        term_add_line("  cat <n>         - hex dump of initramfs file by index");
        term_add_line("  disk            - read sector 0 from SATA drive");
        term_add_line("  format          - format disk (NTRFS)");
        term_add_line("  mount           - mount disk (NTRFS)");
        term_add_line("  dls             - list files on disk");
        term_add_line("  dread <name>    - hex dump of disk file");
        term_add_line("  dsave <i> <name>- save initramfs file to disk");
        term_add_line_ex("--- PROCESSES ---", TERM_TYPE_HEADER);
        term_add_line("  run <name>      - run ELF and wait");
        term_add_line("  spawn <name>    - start ELF in background");
        term_add_line("  kill <pid>      - terminate process (0 = panic)");
        term_add_line("  hello           - run hello.elf");
        term_add_line_ex("--- AUDIO ---", TERM_TYPE_HEADER);
        term_add_line("  chord           - play a C major chord");
        term_add_line("  melody          - play a short melody");
        term_add_line("  cue <n>         - play a cue (1..7)");
        term_add_line("  beep <f> <ms>   - SB16 tone");
        term_add_line("  sound [stop]    - show status or stop sound");
        term_add_line_ex("--- HYBRID IPC ---", TERM_TYPE_HEADER);
        term_add_line("  audio_start     - start userspace audio_server");
        term_add_line("  audio_test      - send notes via IPC to server");
        term_add_line("  vfs_start       - start userspace vfs_server");
        term_add_line("  vfs_test        - test VFS via IPC");
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
        while(r--) b[p++]=rev[r]; b[p] = 0; term_add_line(b);
    }
    else if (gui_strcmp(arg, "ticks") == 0) {
        char b[64]; copy_str(b, " ticks: ", 64); int p = 8;
        uint64_t t = pit_ticks();
        char rev[24]; int r = 0; if(!t){rev[r++]='0';} while(t){rev[r++]='0'+(t%10);t/=10;}
        while(r--) b[p++]=rev[r]; b[p] = 0; term_add_line(b);
    }
    else if (gui_strcmp(arg, "fs") == 0) {
        int n = ntmem_fs_count();
        char b[64]; copy_str(b, " ntmem_fs: ", 64); int p = 11;
        b[p++] = '0' + (n/10)%10; b[p++] = '0' + (n/1)%10; b[p]=0; term_add_line(b);
        for(int i=0; i<n; i++) {
            ntmem_file_t *f = ntmem_fs_get(i);
            if (f) { char fb[64]; copy_str(fb, "  ", 64); copy_str(fb+2, f->name, 62); term_add_line(fb); }
        }
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
            if (p2) { while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); } process_reap(p2); }
        }
    }
    else if (gui_strcmp(arg, "audio_start") == 0) {
        char *cmd = "audio_server.elf";
        ntmem_file_t *f = ntmem_fs_find(cmd);
        if (f) {
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&cmd);
            if (p2) term_add_line(" audio_server spawned in background");
        } else term_add_line_ex(" audio_server.elf not found", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "audio_test") == 0) {
        char *cmd = "audio_test.elf";
        ntmem_file_t *f = ntmem_fs_find(cmd);
        if (f) {
            term_add_line(" running audio_test...");
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&cmd);
            if (p2) { while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); } term_add_line(" audio_test finished"); process_reap(p2); }
        } else term_add_line_ex(" audio_test.elf not found", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "vfs_start") == 0) {
        char *cmd = "vfs_server.elf";
        ntmem_file_t *f = ntmem_fs_find(cmd);
        if (f) {
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&cmd);
            if (p2) term_add_line(" vfs_server spawned in background");
        } else term_add_line_ex(" vfs_server.elf not found", TERM_TYPE_ERR);
    }
    else if (gui_strcmp(arg, "vfs_test") == 0) {
        char *cmd = "vfs_test.elf";
        ntmem_file_t *f = ntmem_fs_find(cmd);
        if (f) {
            term_add_line(" running vfs_test...");
            process_t *p2 = process_create_user(f->data, f->size, 1, (const char**)&cmd);
            if (p2) { while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); } term_add_line(" vfs_test finished"); process_reap(p2); }
        } else term_add_line_ex(" vfs_test.elf not found", TERM_TYPE_ERR);
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
        term_add_line_ex("PID  STATE     NAME", TERM_TYPE_HEADER);
        for(int i=0; i<PROC_MAX; i++) {
            process_t *pr = process_get(i);
            if (pr && pr->state != PROC_FREE) {
                char b[64]; copy_str(b, "  ", 64); int idx = 2;
                uint64_t id = pr->pid; char rev[16]; int r = 0;
                if (id == 0) rev[r++] = '0'; else while(id) { rev[r++]='0'+(id%10); id/=10; }
                while(r--) b[idx++] = rev[r];
                while (idx < 7) b[idx++] = ' ';
                const char *st = "READY";
                if (pr->state == PROC_RUNNING) st = "RUNNING";
                else if (pr->state == PROC_BLOCKED) st = "BLOCKED";
                else if (pr->state == PROC_ZOMBIE) st = "ZOMBIE";
                int sl = 0; while (st[sl] && idx < 17) b[idx++] = st[sl++];
                while (idx < 17) b[idx++] = ' ';
                copy_str(b+idx, pr->name, 64-idx);
                term_add_line(b);
            }
        }
    }
    else if (gui_strcmp(arg, "chord") == 0) {
        static uint8_t pcm[65536];
        sb16_stop(); synth_init();
        synth_note_on(0, 262, WAVE_SINE, 100); synth_note_on(1, 330, WAVE_SINE, 100); synth_note_on(2, 392, WAVE_SINE, 100);
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
    else if (gui_strcmp(arg, "panic") == 0) { __asm__ volatile("int $3"); centcom_panic(); }
    else if (gui_strcmp(arg, "halt") == 0) { term_add_line_ex(" system halted.", TERM_TYPE_ERR); for(;;) __asm__ volatile("hlt"); }
    else if (g_term_input_len > 0) term_add_line_ex(" unknown command", TERM_TYPE_ERR);

    for (int i = 0; i < 256; i++) g_term_input[i] = 0;
    g_term_input_len = 0;
    (void)win;
}

void app_term_draw(window_t *win) {
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

void app_term_key(window_t *win, int c) {
    if (c == '\b') { if (g_term_input_len > 0) { g_term_input_len--; g_term_input[g_term_input_len] = 0; } }
    else if (c == '\n') { term_execute(win); }
    else if (c >= 32 && c < 127) {
        if (g_term_input_len < 255) { g_term_input[g_term_input_len++] = c; g_term_input[g_term_input_len] = 0; }
    }
}