#include "log.h"
#include "serial.h"
#include "../tty/fb.h"
#include <stdarg.h>

#define RING_SIZE 16
#define LINE_LEN  80

static char g_ring[RING_SIZE][LINE_LEN];
static int  g_ring_head = 0;
static int  g_fb_enabled = 1;

void log_set_fb(int enabled) {
    g_fb_enabled = enabled;
}

static const char *tag(subsys_t s) {
    static const char *t[] = {
        "BOO","CPU","LOG","PAN","DBG","PMM","VMM","HEP","CAC","MAP",
        "IRQ","TMR","SMP","SCH","PRC","THR","SYC","DRV","PCI","ACP",
        "ATA","KBD","TTY","NET","USB","VFS","FAT","EXT","NTM","ELF",
        "LIB","USR","SHP","SEC","AUT"
    };
    if ((int)s < 0 || s >= S_COUNT) return "???";
    return t[s];
}

static uint32_t color(subsys_t s) {
    switch (s) {
        case S_BOO: case S_CPU: case S_LOG: case S_PAN: case S_DBG: return 0x00C8102E;
        case S_PMM: case S_VMM: case S_HEP: case S_CAC: case S_MAP: return 0x009060C0;
        case S_IRQ: case S_TMR: case S_SMP: return 0x00D0A020;
        case S_SCH: case S_PRC: case S_THR: case S_SYC: return 0x0030C030;
        case S_DRV: case S_PCI: case S_ACP: case S_ATA: case S_KBD: case S_TTY: case S_NET: case S_USB: return 0x00E07020;
        case S_VFS: case S_FAT: case S_EXT: case S_NTM: return 0x00A07040;
        case S_ELF: case S_LIB: case S_USR: case S_SHP: return 0x004080E0;
        case S_SEC: case S_AUT: return 0x00808080;
        default: return 0x00F0F0F0;
    }
}

static void ch(char c, uint32_t col) {
    serial_putc(c);
    if (g_fb_enabled) {
        fb_putc(c, col, 0x00101018);
    }
}

static void str(const char *s, uint32_t col) { while (*s) ch(*s++, col); }

static void vlog(subsys_t s, const char *lvl, const char *fmt, va_list ap) {
    uint32_t col = color(s);
    char line_buf[LINE_LEN];
    int p = 0;

    line_buf[p++] = '[';
    const char *t = tag(s);
    for (int i = 0; t[i] && p < LINE_LEN - 1; i++) line_buf[p++] = t[i];
    line_buf[p++] = ']';
    line_buf[p++] = ' ';
    
    ch('[', col); str(tag(s), col); ch(']', col);
    if (lvl) { ch(':', col); str(lvl, col); line_buf[p++] = ':'; }
    ch(' ', col);
    line_buf[p++] = ' ';

    while (*fmt && p < LINE_LEN - 1) {
        if (*fmt != '%') {
            line_buf[p++] = *fmt;
            ch(*fmt++, col);
            continue;
        }
        fmt++;
        switch (*fmt++) {
            case 's': {
                const char *v = va_arg(ap, const char*);
                if (!v) v = "(null)";
                while (*v && p < LINE_LEN - 1) { line_buf[p++] = *v; ch(*v++, col); }
                break;
            }
            case 'd': {
                int64_t v = va_arg(ap, int64_t);
                char num_buf[24]; int n = 0;
                if (v < 0) { ch('-', col); line_buf[p++] = '-'; v = -v; }
                if (!v) { ch('0', col); line_buf[p++] = '0'; }
                while (v) { num_buf[n++] = '0' + (v % 10); v /= 10; }
                while (n--) { ch(num_buf[n], col); line_buf[p++] = num_buf[n]; }
                break;
            }
            case 'x': {
                uint64_t v = va_arg(ap, uint64_t);
                ch('0', col); ch('x', col);
                line_buf[p++] = '0'; line_buf[p++] = 'x';
                static const char *h = "0123456789abcdef";
                for (int i = 60; i >= 0; i -= 4) {
                    char c = h[(v >> i) & 0xF];
                    ch(c, col);
                    line_buf[p++] = c;
                }
                break;
            }
            case 'c': {
                char v = (char)va_arg(ap, int);
                ch(v, col);
                line_buf[p++] = v;
                break;
            }
            case '%': ch('%', col); line_buf[p++] = '%'; break;
            default: ch('?', col); line_buf[p++] = '?'; break;
        }
    }
    ch('\n', col);
    line_buf[p] = 0;

    int i = 0;
    while (line_buf[i] && i < LINE_LEN - 1) { g_ring[g_ring_head][i] = line_buf[i]; i++; }
    g_ring[g_ring_head][i] = 0;
    g_ring_head = (g_ring_head + 1) % RING_SIZE;
}

void logi(subsys_t s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(s, 0, fmt, ap); va_end(ap); }
void logw(subsys_t s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(s, "W", fmt, ap); va_end(ap); }
void loge(subsys_t s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(s, "E", fmt, ap); va_end(ap); }
void logd(subsys_t s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(s, "D", fmt, ap); va_end(ap); }

void log_get_recent(char *out_buf, int max_lines) {
    out_buf[0] = 0;
    int count = 0;
    int idx = g_ring_head - 1;
    if (idx < 0) idx = RING_SIZE - 1;

    for (int i = 0; i < RING_SIZE && count < max_lines; i++) {
        if (g_ring[idx][0] != 0) {
            int p = 0; while(out_buf[p]) p++;
            int j = 0; while(g_ring[idx][j]) { out_buf[p++] = g_ring[idx][j++]; }
            out_buf[p++] = '\n';
            out_buf[p] = 0;
            count++;
        }
        idx = (idx == 0) ? RING_SIZE - 1 : idx - 1;
    }
}