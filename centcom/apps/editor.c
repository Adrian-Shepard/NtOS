#include "../gui/apps.h"
#include "../tty/fb.h"

static void put_str(const char *s, uint32_t fg, uint32_t bg) { while (*s) fb_putc(*s++, fg, bg); }

void app_editor_draw(window_t *win) {
    fb_fill_rect(win->x + 1, win->y + TITLE_BAR_H, win->w - 2, win->h - TITLE_BAR_H - 1, 0x00141A20);
    fb_set_cursor(win->x + 16, win->y + 40);
    put_str("TEXT EDITOR (Not Implemented)", 0x00FFAA00, 0x00141A20);
}

void app_editor_key(window_t *win, int c) { (void)win; (void)c; }
void app_editor_click(window_t *win, int mx, int my) { (void)win; (void)mx; (void)my; }