#include "fb.h"
#include "font8x8.h"

static uint32_t *g_fb;          // Реальная видеопамять
static uint32_t *g_back_buffer; // Теневой буфер
static uint32_t g_w, g_h, g_pitch_px;
static uint32_t g_cx, g_cy;
static uint32_t g_scale = 1;

void fb_init(bootinfo_t *bi) {
    g_fb = (uint32_t*)bi->fb_addr;
    g_w = bi->fb_width;
    g_h = bi->fb_height;
    g_pitch_px = bi->fb_pitch / 4;
    g_cx = g_cy = 0;
    g_scale = 1;
    g_back_buffer = g_fb; 
}

void fb_set_buffer(uint32_t *buf) {
    g_back_buffer = buf ? buf : g_fb;
}

void fb_swap(void) {
    if (g_back_buffer == g_fb) return;
    uint64_t *dst = (uint64_t*)g_fb;
    uint64_t *src = (uint64_t*)g_back_buffer;
    uint32_t count = (g_h * g_pitch_px) / 2;
    __asm__ volatile ("cld\n\trep movsq" : "+D"(dst), "+S"(src), "+c"(count) : : "memory");
}

void fb_swap_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (g_back_buffer == g_fb) return;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;

    for (uint32_t yy = y; yy < y + h; yy++) {
        uint64_t *dst = (uint64_t*)&g_fb[yy * g_pitch_px + x];
        uint64_t *src = (uint64_t*)&g_back_buffer[yy * g_pitch_px + x];
        uint32_t count = w / 2;
        __asm__ volatile ("cld\n\trep movsq" : "+D"(dst), "+S"(src), "+c"(count) : : "memory");
        if (w % 2) g_fb[yy * g_pitch_px + x + w - 1] = g_back_buffer[yy * g_pitch_px + x + w - 1];
    }
}

void fb_set_scale(uint32_t s) {
    if (s < 1) s = 1;
    if (s > 8) s = 8;
    g_scale = s;
    uint32_t cell = 8 * s;
    g_cy = ((g_cy + cell - 1) / cell) * cell;
    g_cx = 0;
}

uint32_t fb_get_scale(void) { return g_scale; }

static void putpixel(uint32_t x, uint32_t y, uint32_t c) {
    if (x >= g_w || y >= g_h) return;
    g_back_buffer[y * g_pitch_px + x] = c;
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < g_h; y++) {
        for (uint32_t x = 0; x < g_w; x++) {
            g_back_buffer[y * g_pitch_px + x] = color;
        }
    }
    g_cx = g_cy = 0;
}

static void scroll(uint32_t bg) {
    uint32_t line_h = 8 * g_scale;
    if (g_cy + line_h <= g_h) return;
    for (uint32_t y = 0; y + line_h < g_h; y++)
        for (uint32_t x = 0; x < g_w; x++)
            g_back_buffer[y * g_pitch_px + x] = g_back_buffer[(y + line_h) * g_pitch_px + x];
    for (uint32_t y = g_h - line_h; y < g_h; y++)
        for (uint32_t x = 0; x < g_w; x++)
            g_back_buffer[y * g_pitch_px + x] = bg;
    g_cy -= line_h;
}

void fb_putc(char c, uint32_t fg, uint32_t bg) {
    uint32_t char_w = 8 * g_scale;
    if (c == '\n') { g_cx = 0; g_cy += char_w; scroll(bg); return; }
    if (c == '\r') { g_cx = 0; return; }
    if (c == '\b') {
        if (g_cx >= char_w) {
            g_cx -= char_w;
            for (uint32_t y = 0; y < char_w; y++)
                for (uint32_t x = 0; x < char_w; x++)
                    putpixel(g_cx + x, g_cy + y, bg);
        }
        return;
    }
    if ((uint8_t)c < 32 || (uint8_t)c > 127) c = '?';

    const uint8_t *g = font8x8_basic[(uint8_t)c];
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            uint32_t color = (g[y] & (1 << x)) ? fg : bg;
            for (uint32_t dy = 0; dy < g_scale; dy++)
                for (uint32_t dx = 0; dx < g_scale; dx++)
                    putpixel(g_cx + x * g_scale + dx, g_cy + y * g_scale + dy, color);
        }
    }
    g_cx += char_w;
    if (g_cx + char_w > g_w) { g_cx = 0; g_cy += char_w; scroll(bg); }
}

void fb_puts(const char *s, uint32_t fg, uint32_t bg) { while (*s) fb_putc(*s++, fg, bg); }
uint32_t fb_width(void)  { return g_w; }
uint32_t fb_height(void) { return g_h; }
void fb_set_cursor(uint32_t x, uint32_t y) { g_cx = x; g_cy = y; }

void fb_draw_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t c) { for (uint32_t i = 0; i < w; i++) putpixel(x + i, y, c); }
void fb_draw_vline(uint32_t x, uint32_t y, uint32_t h, uint32_t c) { for (uint32_t i = 0; i < h; i++) putpixel(x, y + i, c); }
void fb_draw_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    fb_draw_hline(x, y, w, c); fb_draw_hline(x, y + h - 1, w, c);
    fb_draw_vline(x, y, h, c); fb_draw_vline(x + w - 1, y, h, c);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    uint32_t x_end = x + w;
    uint32_t y_end = y + h;
    if (x_end > g_w) x_end = g_w;
    if (y_end > g_h) y_end = g_h;

    for (uint32_t yy = y; yy < y_end; yy++) {
        for (uint32_t xx = x; xx < x_end; xx++) {
            g_back_buffer[yy * g_pitch_px + xx] = c;
        }
    }
}

void *fb_get_ptr(uint32_t x, uint32_t y) { if (x >= g_w || y >= g_h) return 0; return (void*)&g_back_buffer[y * g_pitch_px + x]; }
void *fb_get_real_ptr(uint32_t x, uint32_t y) { if (x >= g_w || y >= g_h) return 0; return (void*)&g_fb[y * g_pitch_px + x]; }