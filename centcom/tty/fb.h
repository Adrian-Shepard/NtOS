#pragma once
#include <stdint.h>
#include "../../bootinfo.h"

void fb_init(bootinfo_t *bi);
void fb_set_scale(uint32_t scale);   /* 1 = 8x8, 2 = 16x16, 3 = 24x24 */
uint32_t fb_get_scale(void);
void fb_clear(uint32_t color);
void fb_putc(char c, uint32_t fg, uint32_t bg);
void fb_puts(const char *s, uint32_t fg, uint32_t bg);
uint32_t fb_width(void);
uint32_t fb_height(void);
void fb_set_cursor(uint32_t x, uint32_t y);
void fb_draw_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t c);
void fb_draw_vline(uint32_t x, uint32_t y, uint32_t h, uint32_t c);
void fb_draw_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c); // <-- ДОБАВЛЕНО