#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "synth_user.h"

#define MAX_EVENTS 128
#define MAX_PCM    32768

typedef struct {
    int freqs[8]; // до 8 нот в аккорде
    int count;
    int ms;
} event_t;

static event_t g_events[MAX_EVENTS];
static int     g_event_count = 0;
static uint8_t g_pcm[MAX_PCM];

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}
static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int note_to_freq(const char *s) {
    int all_digits = (*s != 0);
    for (int i = 0; s[i]; i++) if (s[i] < '0' || s[i] > '9') { all_digits = 0; break; }
    if (all_digits) {
        int v = 0;
        for (int i = 0; s[i]; i++) v = v * 10 + (s[i] - '0');
        return v;
    }
    int semi = -1;
    int i = 0;
    switch (s[i]) {
        case 'C': case 'c': semi = 0;  break;
        case 'D': case 'd': semi = 2;  break;
        case 'E': case 'e': semi = 4;  break;
        case 'F': case 'f': semi = 5;  break;
        case 'G': case 'g': semi = 7;  break;
        case 'A': case 'a': semi = 9;  break;
        case 'B': case 'b': semi = 11; break;
        default: return 0;
    }
    i++;
    if (s[i] == '#') { semi++; i++; }
    else if (s[i] == 'b') { semi--; i++; }
    int oct = 4;
    if (s[i] >= '0' && s[i] <= '9') { oct = s[i] - '0'; i++; }
    if (s[i] != 0) return 0;
    return midi_to_freq((oct + 1) * 12 + semi);
}

static int parse_event(const char *line, event_t *ev) {
    ev->count = 0;
    ev->ms = 0;
    const char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == 0) break;
        int val = 0;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 0) {
            if (*p == 0 || *p == '\n' || *p == '\r') {
                ev->ms = val;
            } else {
                if (ev->count < 8) ev->freqs[ev->count++] = val;
            }
        }
    }
    return ev->count > 0 && ev->ms > 0;
}

static void cmd_help(void) {
    printf("synth commands:\n");
    printf("  add <note> <ms>   - append note (C4, 440)\n");
    printf("  list              - show current melody\n");
    printf("  play              - play melody\n");
    printf("  clear             - clear melody\n");
    printf("  save/load         - RAM KV\n");
    printf("  dsave/dload       - NTRFS Disk\n");
    printf("  quit              - exit\n");
}

static void cmd_add(const char *arg) {
    if (g_event_count >= MAX_EVENTS) { printf("full\n"); return; }
    char note[16]; int i = 0;
    while (*arg && *arg != ' ' && i < 15) note[i++] = *arg++;
    note[i] = 0;
    while (*arg == ' ') arg++;
    int ms = 0;
    while (*arg >= '0' && *arg <= '9') { ms = ms * 10 + (*arg - '0'); arg++; }
    if (!note[0] || ms <= 0) { printf("usage: add <note> <ms>\n"); return; }
    
    int f = note_to_freq(note);
    if (f <= 0) { printf("bad note '%s'\n", note); return; }

    event_t *ev = &g_events[g_event_count];
    ev->count = 1;
    ev->freqs[0] = f;
    ev->ms = ms;
    g_event_count++;
    printf("added %s (%d Hz), %d ms\n", note, f, ms);
}

static void cmd_list(void) {
    if (g_event_count == 0) { printf("(empty)\n"); return; }
    for (int i = 0; i < g_event_count; i++) {
        printf("  [%d] ", i);
        for (int k = 0; k < g_events[i].count; k++) printf("%d ", g_events[i].freqs[k]);
        printf("for %d ms\n", g_events[i].ms);
    }
}

static void cmd_play(void) {
    if (g_event_count == 0) { printf("nothing\n"); return; }
    
    int total_ms = 0;
    for (int i = 0; i < g_event_count; i++) total_ms += g_events[i].ms;
    printf("playing %d events (%d ms)...\n", g_event_count, total_ms);

    for (int i = 0; i < g_event_count; i++) {
        int ms = g_events[i].ms;
        int remaining = ms;
        
        while (remaining > 0) {
            int chunk_ms = remaining > 1000 ? 1000 : remaining;
            size_t n = synth_chord(g_pcm, MAX_PCM, g_events[i].freqs, g_events[i].count, chunk_ms, 0, 80);
            if (n > 0) {
                play_pcm(g_pcm, n);
                sleep(chunk_ms);
            }
            remaining -= chunk_ms;
        }
    }
    printf("done playing\n");
}

static void cmd_clear(void) { g_event_count = 0; printf("cleared\n"); }

static void cmd_save(const char *name) {
    if (!name || !*name) { printf("usage: save <name>\n"); return; }
    char buf[2048];
    int p = 0;
    p += sprintf(buf + p, "# NTSAD v2\n");
    for (int i = 0; i < g_event_count && p < 1900; i++) {
        for (int k = 0; k < g_events[i].count; k++) {
            p += sprintf(buf + p, "%d ", g_events[i].freqs[k]);
        }
        p += sprintf(buf + p, "%d\n", g_events[i].ms);
    }
    if (kv_write(name, buf, p) == 0) printf("saved %d bytes -> %s (RAM)\n", p, name);
    else printf("save failed\n");
}

static void cmd_load(const char *name) {
    if (!name || !*name) { printf("usage: load <name>\n"); return; }
    char buf[2048];
    long n = kv_read(name, buf, sizeof(buf) - 1);
    if (n < 0) { printf("not found: %s\n", name); return; }
    buf[n] = 0;
    g_event_count = 0;
    
    char line[256];
    int l_idx = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            line[l_idx] = 0;
            l_idx = 0;
            if (line[0] != '#' && line[0] != 0) {
                if (g_event_count < MAX_EVENTS) {
                    event_t ev;
                    if (parse_event(line, &ev)) {
                        g_events[g_event_count++] = ev;
                    }
                }
            }
        } else if (buf[i] != '\r') {
            if (l_idx < 255) line[l_idx++] = buf[i];
        }
    }
    printf("loaded %d events from %s (RAM)\n", g_event_count, name);
}

static void cmd_dsave(const char *name) {
    if (!name || !*name) { printf("usage: dsave <name>\n"); return; }
    char buf[2048];
    int p = 0;
    p += sprintf(buf + p, "# NTSAD v2\n");
    for (int i = 0; i < g_event_count && p < 1900; i++) {
        for (int k = 0; k < g_events[i].count; k++) {
            p += sprintf(buf + p, "%d ", g_events[i].freqs[k]);
        }
        p += sprintf(buf + p, "%d\n", g_events[i].ms);
    }
    if (ntrfs_write(name, buf, p) > 0) printf("saved %d bytes to disk '%s'\n", p, name);
    else printf("dsave failed\n");
}

static void cmd_dload(const char *name) {
    if (!name || !*name) { printf("usage: dload <name>\n"); return; }
    char buf[2048];
    long n = ntrfs_read(name, buf, sizeof(buf) - 1);
    if (n < 0) { printf("not found on disk: %s\n", name); return; }
    buf[n] = 0;
    g_event_count = 0;
    
    char line[256];
    int l_idx = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            line[l_idx] = 0;
            l_idx = 0;
            if (line[0] != '#' && line[0] != 0) {
                if (g_event_count < MAX_EVENTS) {
                    event_t ev;
                    if (parse_event(line, &ev)) {
                        g_events[g_event_count++] = ev;
                    }
                }
            }
        } else if (buf[i] != '\r') {
            if (l_idx < 255) line[l_idx++] = buf[i];
        }
    }
    printf("loaded %d events from disk '%s'\n", g_event_count, name);
}

static char g_line[128];
static int  g_len = 0;
static void prompt(void) { printf("synth> "); }

static void execute(void) {
    g_line[g_len] = 0;
    if (g_len == 0) return;
    if (streq(g_line, "help"))       cmd_help();
    else if (streq(g_line, "list"))  cmd_list();
    else if (streq(g_line, "play"))  cmd_play();
    else if (streq(g_line, "clear")) cmd_clear();
    else if (starts_with(g_line, "add "))   cmd_add(g_line + 4);
    else if (starts_with(g_line, "dsave ")) cmd_dsave(g_line + 6);
    else if (starts_with(g_line, "dload ")) cmd_dload(g_line + 6);
    else if (starts_with(g_line, "save "))  cmd_save(g_line + 5);
    else if (starts_with(g_line, "load "))  cmd_load(g_line + 5);
    else if (streq(g_line, "quit") || streq(g_line, "exit")) {
        printf("bye\n");
        _exit(0);
    }
    else printf("unknown: %s\n", g_line);
    g_len = 0;
}

int main(void) {
    printf("\nNTSAD synthesizer v2\n");
    printf("type 'help'\n\n");
    prompt();

    for (;;) {
        char c;
        long r = read(0, &c, 1);
        if (r <= 0) continue;
        if (c == '\n') {
            printf("\n");
            execute();
            prompt();
        } else if (c == '\b' || c == 127) {
            if (g_len > 0) { g_len--; printf("\b \b"); }
        } else if (c >= 32 && c < 127) {
            if (g_len < 127) {
                g_line[g_len++] = c;
                char t[2] = { c, 0 };
                printf("%s", t);
            }
        }
    }
    return 0;
}