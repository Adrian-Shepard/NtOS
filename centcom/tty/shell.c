#include "shell.h"
#include "fb.h"
#include "gui.h"           // <-- ДОБАВЛЕНО
#include "../driver/keyboard.h"
#include "../driver/sb16.h"
#include "../driver/ahci.h"
#include "../mem/pmm.h"
#include "../mem/heap.h"
#include "../time/pit.h"
#include "../fs/ntmem_fs.h"
#include "../fs/ntrfs.h"
#include "../sched/sched.h"
#include "../sound/cue.h"
#include "../sound/synth.h"
#include "../log/log.h"
#include "../log/serial.h"
#include "../core/panic.h"

#define SH_BG  0x00101018
#define SH_FG  0x00E0E0E0
#define SH_ACC 0x00C8102E
#define SH_DIM 0x00808080

#define LINE_MAX 128
static char g_line[LINE_MAX];
static int  g_len = 0;

static void sh_puts(const char *s, uint32_t fg) { while (*s) fb_putc(*s++, fg, SH_BG); }
static void prompt(void) { sh_puts("centcom", SH_ACC); sh_puts("> ", SH_FG); }
static int streq(const char *a, const char *b) { while (*a && *b && *a == *b) { a++; b++; } return *a == *b; }
static int starts_with(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static void print_uint(uint64_t v) {
    char b[24]; int i = 0;
    if (!v) { fb_putc('0', SH_FG, SH_BG); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) fb_putc(b[i], SH_FG, SH_BG);
}
static void print_hex(uint64_t v) {
    const char *h = "0123456789abcdef";
    sh_puts("0x", SH_DIM);
    for (int i = 60; i >= 0; i -= 4) fb_putc(h[(v >> i) & 0xF], SH_DIM, SH_BG);
}

/* ============ тестовые kernel-потоки ============ */

static void test_thread_a(void) {
    for (int i = 0; i < 20; i++) {
        serial_puts("[A]"); serial_putdec((uint64_t)i); serial_puts("\n");
        for (volatile int j = 0; j < 500000; j++) __asm__ volatile("pause");
    }
    logi(S_SCH, "thrA done");
}

static void test_thread_b(void) {
    for (int i = 0; i < 20; i++) {
        serial_puts("[B]"); serial_putdec((uint64_t)i); serial_puts("\n");
        for (volatile int j = 0; j < 500000; j++) __asm__ volatile("pause");
    }
    logi(S_SCH, "thrB done");
}

/* ============ команды ============ */

static void cmd_help(void) {
    sh_puts("commands:\n", SH_FG);
    sh_puts("  help      - this message\n", SH_DIM);
    sh_puts("  mem       - memory statistics\n", SH_DIM);
    sh_puts("  heap      - heap statistics\n", SH_DIM);
    sh_puts("  ticks     - PIT tick counter\n", SH_DIM);
    sh_puts("  fs        - list files in ntmem_fs\n", SH_DIM);
    sh_puts("  cat <n>   - hex dump of file by index\n", SH_DIM);
    sh_puts("  disk      - read sector 0 from SATA drive\n", SH_DIM);
    sh_puts("  format    - format disk (NTRFS)\n", SH_DIM);
    sh_puts("  mount     - mount disk (NTRFS)\n", SH_DIM);
    sh_puts("  dls       - list files on disk\n", SH_DIM);
    sh_puts("  dread <n> - read file from disk (hex)\n", SH_DIM);
    sh_puts("  dsave <i> <name> - copy ntmem file #i to disk\n", SH_DIM);
    sh_puts("  spawn <n> [args] - start ELF in background\n", SH_DIM);
    sh_puts("  run <n>   [args] - start ELF and wait for exit\n", SH_DIM);
    sh_puts("  kill <pid>- terminate process (0 = panic)\n", SH_DIM);
    sh_puts("  hello     - run hello.elf\n", SH_DIM);
    sh_puts("  procs     - list processes\n", SH_DIM);
    sh_puts("  threads   - spawn 2 kernel threads (test)\n", SH_DIM);
    sh_puts("  chord     - play a C major chord (synth test)\n", SH_DIM);
    sh_puts("  melody    - play a short melody\n", SH_DIM);
    sh_puts("  cue <n>   - play a cue (0..7)\n", SH_DIM);
    sh_puts("  beep f ms - SB16 tone (background)\n", SH_DIM);
    sh_puts("  sound [stop] - show status / stop sound\n", SH_DIM);
    sh_puts("  gui       - launch graphical interface demo\n", SH_DIM); // <-- ДОБАВЛЕНО
    sh_puts("  clear     - clear screen\n", SH_DIM);
    sh_puts("  panic     - trigger test panic\n", SH_DIM);
    sh_puts("  halt      - halt the CPU\n", SH_DIM);
}

static void cmd_mem(void) {
    sh_puts("total pages: ", SH_FG); print_uint(pmm_total_pages()); sh_puts("\n", SH_FG);
    sh_puts("free pages:  ", SH_FG); print_uint(pmm_free_pages());  sh_puts("\n", SH_FG);
    sh_puts("total RAM:   ", SH_FG); print_uint((pmm_total_pages() * 4) / 1024); sh_puts(" MiB\n", SH_FG);
    sh_puts("free RAM:    ", SH_FG); print_uint((pmm_free_pages()  * 4) / 1024); sh_puts(" MiB\n", SH_FG);
}

static void cmd_heap(void) {
    sh_puts("heap used:  ", SH_FG); print_uint(heap_used());  sh_puts(" bytes\n", SH_FG);
    sh_puts("heap total: ", SH_FG); print_uint(heap_total()); sh_puts(" bytes\n", SH_FG);
}

static void cmd_ticks(void) {
    sh_puts("ticks: ", SH_FG); print_uint(pit_ticks());
    sh_puts("  (~", SH_FG); print_uint(pit_ticks() / 100);
    sh_puts(" seconds at 100 Hz)\n", SH_FG);
}

static void cmd_fs(void) {
    int n = ntmem_fs_count();
    sh_puts("ntmem_fs: ", SH_FG); print_uint(n); sh_puts(" file(s)\n", SH_FG);
    for (int i = 0; i < n; i++) {
        ntmem_file_t *f = ntmem_fs_get(i);
        if (!f) continue;
        sh_puts("  [", SH_DIM); print_uint(i); sh_puts("] ", SH_DIM);
        sh_puts(f->name, SH_FG);
        sh_puts("  (", SH_DIM); print_uint(f->size); sh_puts(" bytes)\n", SH_DIM);
    }
}

static void cmd_cat(const char *arg) {
    int idx = 0;
    while (*arg >= '0' && *arg <= '9') { idx = idx*10 + (*arg - '0'); arg++; }
    ntmem_file_t *f = ntmem_fs_get(idx);
    if (!f) { sh_puts("no such file\n", SH_DIM); return; }
    sh_puts("--- ", SH_ACC); sh_puts(f->name, SH_ACC); sh_puts(" (", SH_ACC); print_uint(f->size); sh_puts(" bytes) ---\n", SH_ACC);
    for (uint64_t i = 0; i < f->size; i += 16) {
        char hex[] = "0000  ";
        for (int k = 0; k < 4; k++) {
            int nib = (int)((i >> ((3 - k) * 4)) & 0xF);
            hex[k] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        }
        sh_puts(hex, SH_DIM);
        for (int j = 0; j < 16; j++) {
            if (i + j >= f->size) { sh_puts("   ", SH_DIM); continue; }
            uint8_t b = f->data[i + j];
            char hb[3] = { (char)((b >> 4) < 10 ? '0' + (b >> 4) : 'a' + (b >> 4) - 10), (char)((b & 0xF) < 10 ? '0' + (b & 0xF) : 'a' + (b & 0xF) - 10), ' ' };
            sh_puts(hb, SH_FG);
        }
        sh_puts(" ", SH_DIM);
        for (int j = 0; j < 16; j++) {
            if (i + j >= f->size) break;
            uint8_t b = f->data[i + j];
            char c = (b >= 32 && b < 127) ? (char)b : '.';
            fb_putc(c, SH_DIM, SH_BG);
        }
        sh_puts("\n", SH_FG);
    }
}

static void cmd_disk(void) {
    static uint8_t buf[512];
    sh_puts("disk: reading sector 0...\n", SH_DIM);
    
    if (!ahci_read_sector(0, buf)) {
        sh_puts("error: failed to read sector (AHCI not ready? disk connected?)\n", SH_DIM);
        return;
    }
    
    sh_puts("disk: sector 0 (first 32 bytes)\n", SH_FG);
    for (int i = 0; i < 32; i++) {
        const char *h = "0123456789abcdef";
        char b[3] = { h[(buf[i] >> 4) & 0xF], h[buf[i] & 0xF], 0 };
        sh_puts(b, SH_FG);
        sh_puts(" ", SH_DIM);
        if ((i + 1) % 16 == 0) sh_puts("\n", SH_DIM);
    }
    sh_puts("\n", SH_FG);
}

static void cmd_format(void) {
    sh_puts("formatting disk...\n", SH_DIM);
    if (ntrfs_format()) sh_puts("ok: NTRFS formatted\n", SH_ACC);
    else sh_puts("error: format failed\n", SH_DIM);
}

static void cmd_mount(void) {
    sh_puts("mounting disk...\n", SH_DIM);
    if (ntrfs_mount()) sh_puts("ok: NTRFS mounted\n", SH_ACC);
    else sh_puts("error: mount failed (try 'format')\n", SH_DIM);
}

static void cmd_dls(void) {
    if (!ntrfs_is_mounted()) { sh_puts("not mounted\n", SH_DIM); return; }
    sh_puts("disk files:\n", SH_FG);
    int count = 0;
    for (int i = 0; ; i++) {
        ntrfs_inode_t in;
        int r = ntrfs_list(i, &in);
        if (r <= 0) break;
        sh_puts("  ", SH_DIM); sh_puts(in.name, SH_FG);
        sh_puts("  ", SH_DIM); print_uint(in.size);
        sh_puts(" bytes @ block ", SH_DIM); print_uint(in.first_block); sh_puts("\n", SH_DIM);
        count++;
    }
    if (count == 0) sh_puts("  (empty)\n", SH_DIM);
}

static void cmd_dread(const char *arg) {
    if (!ntrfs_is_mounted()) { sh_puts("not mounted\n", SH_DIM); return; }
    char name[64]; int i = 0;
    while (*arg && *arg != ' ' && i < 63) name[i++] = *arg++;
    name[i] = 0;
    if (!name[0]) { sh_puts("usage: dread <name>\n", SH_DIM); return; }
    static uint8_t buf[8192];
    int n = ntrfs_read(name, buf, sizeof(buf));
    if (n < 0) { sh_puts("not found\n", SH_DIM); return; }
    sh_puts("--- ", SH_ACC); sh_puts(name, SH_ACC); sh_puts(" (", SH_ACC); print_uint((uint64_t)n); sh_puts(" bytes) ---\n", SH_ACC);
    for (int i = 0; i < n; i += 16) {
        char hex[] = "0000  ";
        for (int k = 0; k < 4; k++) {
            int nib = (i >> ((3 - k) * 4)) & 0xF;
            hex[k] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        }
        sh_puts(hex, SH_DIM);
        for (int j = 0; j < 16; j++) {
            if (i + j >= n) { sh_puts("   ", SH_DIM); continue; }
            uint8_t b = buf[i + j];
            char hb[3] = { (char)((b >> 4) < 10 ? '0' + (b >> 4) : 'a' + (b >> 4) - 10), (char)((b & 0xF) < 10 ? '0' + (b & 0xF) : 'a' + (b & 0xF) - 10), ' ' };
            sh_puts(hb, SH_FG);
        }
        sh_puts(" ", SH_DIM);
        for (int j = 0; j < 16; j++) {
            if (i + j >= n) break;
            char c = (buf[i + j] >= 32 && buf[i + j] < 127) ? (char)buf[i + j] : '.';
            fb_putc(c, SH_DIM, SH_BG);
        }
        sh_puts("\n", SH_FG);
    }
}

static void cmd_dsave(const char *arg) {
    if (!ntrfs_is_mounted()) { sh_puts("not mounted\n", SH_DIM); return; }
    int idx = 0;
    while (*arg >= '0' && *arg <= '9') { idx = idx*10 + (*arg - '0'); arg++; }
    while (*arg == ' ') arg++;
    if (!*arg) { sh_puts("usage: dsave <i> <name>\n", SH_DIM); return; }
    ntmem_file_t *f = ntmem_fs_get(idx);
    if (!f) { sh_puts("no such ntmem file\n", SH_DIM); return; }
    if (ntrfs_create(arg, f->data, f->size)) {
        sh_puts("saved '", SH_DIM); sh_puts(arg, SH_DIM); sh_puts("' to disk\n", SH_DIM);
    } else sh_puts("save failed\n", SH_DIM);
}

static void cmd_spawn(const char *rest) {
    if (!*rest) { sh_puts("usage: spawn <name> [args...]\n", SH_DIM); return; }
    char buf[LINE_MAX];
    int i = 0;
    while (rest[i] && i < LINE_MAX - 1) { buf[i] = rest[i]; i++; }
    buf[i] = 0;
    char *argv[16];
    int   argc = 0;
    char *p = buf;
    while (*p && argc < 16) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) { sh_puts("usage: spawn <name>\n", SH_DIM); return; }
    ntmem_file_t *f = ntmem_fs_find(argv[0]);
    if (!f) { sh_puts("not found: ", SH_DIM); sh_puts(argv[0], SH_DIM); sh_puts("\n", SH_DIM); return; }
    process_t *p2 = process_create_user(f->data, f->size, argc, (const char**)argv);
    if (!p2) { sh_puts("spawn failed\n", SH_DIM); return; }
    sh_puts("spawned pid=", SH_DIM); print_uint(p2->pid); sh_puts(" name=", SH_DIM); sh_puts(p2->name, SH_DIM); sh_puts("\n", SH_DIM);
}

static void cmd_run(const char *rest) {
    if (!*rest) { sh_puts("usage: run <name> [args...]\n", SH_DIM); return; }
    char buf[LINE_MAX];
    int i = 0;
    while (rest[i] && i < LINE_MAX - 1) { buf[i] = rest[i]; i++; }
    buf[i] = 0;
    char *argv[16];
    int   argc = 0;
    char *p = buf;
    while (*p && argc < 16) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) { sh_puts("usage: run <name>\n", SH_DIM); return; }
    ntmem_file_t *f = ntmem_fs_find(argv[0]);
    if (!f) { sh_puts("not found: ", SH_DIM); sh_puts(argv[0], SH_DIM); sh_puts("\n", SH_DIM); return; }
    process_t *p2 = process_create_user(f->data, f->size, argc, (const char**)argv);
    if (!p2) { sh_puts("spawn failed\n", SH_DIM); return; }
    sh_puts("running pid=", SH_ACC); print_uint(p2->pid); sh_puts("...\n", SH_DIM);
    while (p2->state != PROC_ZOMBIE) { __asm__ volatile("hlt"); }
    sh_puts("(exited, code=", SH_DIM); print_uint((uint64_t)(p2->exit_code & 0xFF)); sh_puts(")\n", SH_DIM);
    process_reap(p2);
}

static void cmd_hello(void) { cmd_run("hello.elf"); }

static void cmd_kill(const char *arg) {
    int pid = -1;
    while (*arg >= '0' && *arg <= '9') { if (pid == -1) pid = 0; pid = pid * 10 + (*arg - '0'); arg++; }
    if (pid == -1) { sh_puts("usage: kill <pid>\n", SH_DIM); return; }
    if (pid == 0) {
        sh_puts("Why did you killed 0? you are monster.\n", SH_ACC);
        __asm__ volatile("int $3");
        centcom_panic();
    }
    process_kill((uint64_t)pid);
    sh_puts("killed pid=", SH_DIM); print_uint((uint64_t)pid); sh_puts("\n", SH_DIM);
}

static void cmd_procs(void) {
    sh_puts("PID  NAME         STATE     CR3\n", SH_FG);
    static const char *st[] = { "READY", "RUNNING", "BLOCKED", "ZOMBIE", "FREE" };
    for (int i = 0; i < PROC_MAX; i++) {
        process_t *p = process_get(i);
        if (!p || p->state == PROC_FREE) continue;
        print_uint(p->pid); sh_puts("    ", SH_DIM);
        sh_puts(p->name, SH_FG);
        int len = 0; while (p->name[len]) len++;
        for (int k = len; k < 13; k++) fb_putc(' ', SH_FG, SH_BG);
        sh_puts(st[p->state], SH_DIM);
        int stlen = 0; while (st[p->state][stlen]) stlen++;
        for (int k = stlen; k < 10; k++) fb_putc(' ', SH_FG, SH_BG);
        print_hex(p->cr3); sh_puts("\n", SH_DIM);
    }
}

static void cmd_threads(void) {
    process_t *a = process_create_kernel(test_thread_a, "thrA");
    process_t *b = process_create_kernel(test_thread_b, "thrB");
    sh_puts("created: ", SH_DIM);
    if (a) { sh_puts("thrA(", SH_DIM); print_uint(a->pid); sh_puts(") ", SH_DIM); }
    if (b) { sh_puts("thrB(", SH_DIM); print_uint(b->pid); sh_puts(") ", SH_DIM); }
    sh_puts("\n", SH_DIM);
}

static void cmd_chord(void) {
    static uint8_t pcm[65536];
    sb16_stop(); synth_init();
    synth_note_on(0, 262, WAVE_SINE, 100);
    synth_note_on(1, 330, WAVE_SINE, 100);
    synth_note_on(2, 392, WAVE_SINE, 100);
    uint32_t samples = (22050 * 1000) / 1000;
    synth_render(pcm, samples);
    sh_puts("playing C major chord...\n", SH_ACC);
    sb16_play_pcm_async(pcm, samples);
}

static void cmd_melody(void) {
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
    sh_puts("playing melody (", SH_ACC); print_uint((uint64_t)(offset / 22)); sh_puts(" ms)...\n", SH_ACC);
    sb16_play_pcm_async(pcm, offset);
}

static void cmd_cue(const char *arg) {
    int idx = 0;
    while (*arg >= '0' && *arg <= '9') { idx = idx*10 + (*arg - '0'); arg++; }
    if (idx == 0) { cue_stop(); sh_puts("cue: stopped\n", SH_DIM); return; }
    if (idx < 1 || idx > 7) { sh_puts("cue: code 1..7\n", SH_DIM); return; }
    sh_puts("cue: ", SH_DIM); print_uint((uint64_t)idx); sh_puts("\n", SH_DIM);
    cue_play((uint8_t)idx);
}

static void cmd_beep(const char *arg) {
    int freq = 0, ms = 0;
    while (*arg >= '0' && *arg <= '9') { freq = freq*10 + (*arg - '0'); arg++; }
    while (*arg == ' ') arg++;
    while (*arg >= '0' && *arg <= '9') { ms = ms * 10 + (*arg - '0'); arg++; }
    if (freq <= 0 || ms <= 0) { sh_puts("usage: beep <freq> <ms>\n", SH_DIM); return; }
    if (ms > 3000) { sh_puts("[capped to 3000 ms]\n", SH_DIM); ms = 3000; }
    if (freq > 8000) freq = 8000;
    sh_puts("beep ", SH_DIM); print_uint((uint64_t)freq); sh_puts(" Hz, ", SH_DIM); print_uint((uint64_t)ms); sh_puts(" ms", SH_DIM);
    if (sb16_play_tone_async(freq, ms)) sh_puts(" [background]\n", SH_DIM);
    else sh_puts(" [busy]\n", SH_DIM);
}

static void cmd_sound(const char *arg) {
    while (*arg == ' ') arg++;
    if (streq(arg, "stop")) { sb16_stop(); sh_puts("sound: stopped\n", SH_DIM); return; }
    sh_puts("SB16 status: ", SH_FG);
    if (sb16_is_playing()) sh_puts("PLAYING\n", SH_ACC);
    else                   sh_puts("IDLE\n", SH_DIM);
}

static void execute(void) {
    g_line[g_len] = 0;
    if (g_len == 0) return;

    if (streq(g_line, "help"))       cmd_help();
    else if (streq(g_line, "mem"))   cmd_mem();
    else if (streq(g_line, "heap"))  cmd_heap();
    else if (streq(g_line, "ticks")) cmd_ticks();
    else if (streq(g_line, "fs"))    cmd_fs();
    else if (streq(g_line, "disk"))  cmd_disk();
    else if (streq(g_line, "format")) cmd_format();
    else if (streq(g_line, "mount"))  cmd_mount();
    else if (streq(g_line, "dls"))    cmd_dls();
    else if (starts_with(g_line, "cat "))   cmd_cat(g_line + 4);
    else if (starts_with(g_line, "dread ")) cmd_dread(g_line + 6);
    else if (starts_with(g_line, "dsave ")) cmd_dsave(g_line + 6);
    else if (starts_with(g_line, "spawn ")) cmd_spawn(g_line + 6);
    else if (starts_with(g_line, "run "))   cmd_run(g_line + 4);
    else if (starts_with(g_line, "kill "))  cmd_kill(g_line + 5);
    else if (streq(g_line, "hello")) cmd_hello();
    else if (streq(g_line, "procs")) cmd_procs();
    else if (streq(g_line, "threads")) cmd_threads();
    else if (streq(g_line, "chord")) cmd_chord();
    else if (streq(g_line, "melody")) cmd_melody();
    else if (starts_with(g_line, "cue "))   cmd_cue(g_line + 4);
    else if (starts_with(g_line, "beep "))  cmd_beep(g_line + 5);
    else if (starts_with(g_line, "sound"))  cmd_sound(g_line + 5);
    else if (streq(g_line, "gui")) gui_demo(); // <-- ДОБАВЛЕНО
    else if (streq(g_line, "clear")) fb_clear(SH_BG);
    else if (streq(g_line, "panic")) __asm__ volatile("int $3");
    else if (streq(g_line, "halt")) {
        sh_puts("halted.\n", SH_FG);
        for (;;) __asm__ volatile("hlt");
    }
    else {
        sh_puts("unknown command: ", SH_DIM);
        sh_puts(g_line, SH_DIM);
        sh_puts("\n", SH_DIM);
    }
    g_len = 0;
}

void shell_run(void) {
    fb_clear(SH_BG);
    fb_set_scale(1);

    sh_puts("\n", SH_FG);
    sh_puts("  NtOS v0.15.2 - CENTCOM shell\n", SH_ACC);
    sh_puts("  type 'help' for commands\n\n", SH_DIM);

    prompt();

    for (;;) {
        for (int i = 1; i < PROC_MAX; i++) {
            process_t *p = process_get(i);
            if (p && p->state == PROC_ZOMBIE) process_reap(p);
        }

        int c = keyboard_getchar();
        if (!c) { __asm__ volatile("hlt"); continue; }

        if (c == '\n') {
            fb_putc('\n', SH_FG, SH_BG);
            execute();
            prompt();
        } else if (c == '\b') {
            if (g_len > 0) { g_len--; fb_putc('\b', SH_FG, SH_BG); }
        } else if (c >= 32 && c < 127) {
            if (g_len < LINE_MAX - 1) {
                g_line[g_len++] = (char)c;
                fb_putc((char)c, SH_FG, SH_BG);
            }
        }
    }
}