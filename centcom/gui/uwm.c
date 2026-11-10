#include "uwm.h"
#include "gui.h"
#include "fb.h"
#include "../sound/cue.h"
#include "../mem/heap.h"
#include "../sched/sched.h"

#define UWM_BG        0x00141A20
#define UWM_BORDER    0x00AA2222
#define UWM_TITLE_BG  0x00AA2222
#define UWM_TITLE_FG  0x00FFFFFF
#define UWM_BTN_BG    0x00303030

static uwm_window_t g_wins[UWM_MAX_WINDOWS];
static uint32_t     g_next_id = 1;
static int          g_active_id = -1;

static uwm_window_t *find_win(int id) {
    for (int i = 0; i < UWM_MAX_WINDOWS; i++)
        if (g_wins[i].used && (int)g_wins[i].id == id) return &g_wins[i];
    return 0;
}

static void copy_str(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void push_event(uwm_window_t *w, int type, int key, int x, int y) {
    int next = (w->ev_tail + 1) % 8;
    if (next == w->ev_head) return; /* очередь переполнена — отбрасываем */
    w->evq[w->ev_tail].type = type;
    w->evq[w->ev_tail].key  = key;
    w->evq[w->ev_tail].x    = x;
    w->evq[w->ev_tail].y    = y;
    w->ev_tail = next;
}

int uwm_create(const char *title, int w, int h, uint64_t owner_pid) {
    if (w < 64)  w = 64;
    if (h < 48)  h = 48;
    if (w > (int)fb_width()  - 8) w = (int)fb_width()  - 8;
    if (h > (int)fb_height() - 40) h = (int)fb_height() - 40;

    for (int i = 0; i < UWM_MAX_WINDOWS; i++) {
        if (g_wins[i].used) continue;

        uint32_t *surf = (uint32_t*)kmalloc((uint64_t)w * h * 4);
        if (!surf) return -1;

        /* каскадное расположение, как в Windows */
        int off = (int)(g_next_id % 8) * 28;
        int x = 40 + off, y = 60 + off;
        if (x + w > (int)fb_width())  x = (int)fb_width()  - w - 4;
        if (y + h > (int)fb_height() - 26) y = (int)fb_height() - h - 26;
        if (x < 4) x = 4;
        if (y < 40) y = 40;

        uwm_window_t *win = &g_wins[i];
        win->id    = g_next_id++;
        win->used  = 1;
        win->x = x; win->y = y; win->w = w; win->h = h;
        copy_str(win->title, title, 64);
        win->surface   = surf;
        win->owner_pid = owner_pid;
        win->ev_head   = 0;
        win->ev_tail   = 0;

        fb_fill_buf(surf, (uint32_t)w, 0, 0, (uint32_t)w, (uint32_t)h, UWM_BG);

        g_active_id = (int)win->id;
        cue_play(CUE_UI_REVEAL);   /* звук открытия окна */
        return (int)win->id;
    }
    return -1;
}

int uwm_close(int id) {
    uwm_window_t *w = find_win(id);
    if (!w) return -1;
    if (w->surface) { kfree(w->surface); w->surface = 0; }
    w->used = 0;
    w->id   = 0;
    if (g_active_id == id) g_active_id = -1;
    cue_play(CUE_UI_SOFT);   /* звук закрытия окна */
    return 0;
}

void uwm_close_by_pid(uint64_t pid) {
    for (int i = 0; i < UWM_MAX_WINDOWS; i++) {
        if (g_wins[i].used && g_wins[i].owner_pid == pid) {
            uwm_close((int)g_wins[i].id);
        }
    }
}

void uwm_fill_rect(int id, int x, int y, int w, int h, uint32_t color) {
    uwm_window_t *win = find_win(id);
    if (!win) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x + w > win->w) w = win->w - x;
    if (y + h > win->h) h = win->h - y;
    fb_fill_buf(win->surface, (uint32_t)win->w, (uint32_t)x, (uint32_t)y,
                (uint32_t)w, (uint32_t)h, color);
}

void uwm_draw_text(int id, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    uwm_window_t *win = find_win(id);
    if (!win) return;
    fb_text_buf(win->surface, (uint32_t)win->w, (uint32_t)win->w, (uint32_t)win->h,
                (uint32_t)x, (uint32_t)y, s, fg, bg);
}

void uwm_set_title(int id, const char *title) {
    uwm_window_t *w = find_win(id);
    if (!w) return;
    copy_str(w->title, title, 64);
}

int uwm_poll_event(int id, gui_event_t *ev) {
    uwm_window_t *w = find_win(id);
    if (!w) return -1;
    if (w->ev_head == w->ev_tail) return 0;
    *ev = w->evq[w->ev_head];
    w->ev_head = (w->ev_head + 1) % 8;
    return 1;
}

/* ---------- вызовы из gui.c ---------- */

void uwm_draw_all(void) {
    for (int i = 0; i < UWM_MAX_WINDOWS; i++) {
        uwm_window_t *w = &g_wins[i];
        if (!w->used) continue;

        fb_blit((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h, w->surface);

        uint32_t border = ((int)w->id == g_active_id) ? UWM_BORDER : 0x00404040;
        fb_draw_box((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h, border);

        uint32_t tbg = ((int)w->id == g_active_id) ? UWM_TITLE_BG : 0x00404040;
        fb_fill_rect((uint32_t)w->x + 1, (uint32_t)w->y + 1,
                     (uint32_t)w->w - 2, UWM_TITLE_H - 4, tbg);
        fb_set_cursor((uint32_t)w->x + 8, (uint32_t)w->y + 8);
        const char *t = w->title;
        while (*t) fb_putc(*t++, UWM_TITLE_FG, tbg);

        fb_fill_rect((uint32_t)w->x + w->w - 20, (uint32_t)w->y + 4, 16, 16, UWM_BTN_BG);
        fb_draw_box((uint32_t)w->x + w->w - 20, (uint32_t)w->y + 4, 16, 16, border);
        fb_set_cursor((uint32_t)w->x + w->w - 16, (uint32_t)w->y + 8);
        fb_putc('X', 0x00D0D0D0, UWM_BTN_BG);
    }
}

int uwm_has_windows(void) {
    for (int i = 0; i < UWM_MAX_WINDOWS; i++)
        if (g_wins[i].used) return 1;
    return 0;
}

int uwm_handle_click(int mx, int my) {
    for (int i = UWM_MAX_WINDOWS - 1; i >= 0; i--) {
        uwm_window_t *w = &g_wins[i];
        if (!w->used) continue;
        if (mx < w->x || mx > w->x + w->w || my < w->y || my > w->y + w->h) continue;

        g_active_id = (int)w->id;

        if (my <= w->y + UWM_TITLE_H) {
            if (mx >= w->x + w->w - 20 && mx <= w->x + w->w - 4) {
                uwm_close((int)w->id);
                return 1;
            }
            return 1; /* заголовок: только активация */
        }

        push_event(w, GUI_EV_CLICK, 0, mx - w->x, my - w->y);
        return 1;
    }
    g_active_id = -1;
    return 0;
}

int uwm_deliver_key(int key) {
    uwm_window_t *w = find_win(g_active_id);
    if (!w) { g_active_id = -1; return 0; }
    push_event(w, GUI_EV_KEY, key, 0, 0);
    return 1;
}

void uwm_clear_active(void) {
    g_active_id = -1;
}
