#include "uwm.h"
#include "gui.h"
#include "fb.h"
#include "../sound/cue.h"
#include "../mem/heap.h"
#include "../sched/sched.h"

#define UWM_BG        0x00141A24
#define UWM_BORDER    0x002A3A48
#define UWM_BORDER_ACT 0x002EE6C8
#define UWM_TITLE_BG  0x001C2A36
#define UWM_TITLE_FG  0x00FFFFFF
#define UWM_BTN_BG    0x0024303C
#define UWM_BTN_ACT   0x00FF5555

static uwm_window_t g_wins[UWM_MAX_WINDOWS];
static uint32_t     g_next_id = 1;
static int          g_active_id = -1;

/* Состояние перетаскивания user-окна за заголовок */
static int          g_drag_id = -1;
static int          g_drag_off_x = 0, g_drag_off_y = 0;

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

        /* Текстовый буфер stdout процесса */
        win->text_lines = 0;
        win->text_col   = 0;
        for (int t = 0; t < 64; t++) win->text[t][0] = 0;

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

/* Запись stdout процесса в текстовый буфер окна (мини-терминал). */
void uwm_win_write(int id, const char *s, uint64_t len) {
    uwm_window_t *win = find_win(id);
    if (!win) return;
    for (uint64_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\n') {
            win->text[win->text_lines][win->text_col] = 0;
            win->text_col = 0;
            if (win->text_lines < 63) {
                win->text_lines++;
                win->text[win->text_lines][0] = 0;
            } else {
                /* скролл: сдвигаем все строки вверх */
                for (int j = 0; j < 63; j++)
                    for (int k = 0; k < 96; k++)
                        win->text[j][k] = win->text[j + 1][k];
                win->text[63][0] = 0;
            }
        } else if (c == '\r') {
            win->text_col = 0;
        } else if (c >= 32 && c < 127) {
            if (win->text_col < 95)
                win->text[win->text_lines][win->text_col++] = c;
        }
    }
    win->text[win->text_lines][win->text_col] = 0;   /* терминатор текущей строки */
}

/* Копирование пользовательского pixel-буфера в поверхность окна (для Doom).
 * Буфер формата RGB32, w x h пикселей; лишнее обрезается по размеру окна. */
void uwm_blit(int id, const uint32_t *src, int w, int h) {
    uwm_window_t *win = find_win(id);
    if (!win || !src) return;
    if (w > win->w) w = win->w;
    if (h > win->h - UWM_TITLE_H) h = win->h - UWM_TITLE_H;
    /* Рисуем кадр НИЖЕ заголовка окна, чтобы не перекрывать его */
    for (int y = 0; y < h; y++) {
        uint32_t *dst = &win->surface[(uint32_t)(UWM_TITLE_H + y) * (uint32_t)win->w];
        const uint32_t *s  = &src[y * w];
        for (int x = 0; x < w; x++) dst[x] = s[x];
    }
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

        int active = ((int)w->id == g_active_id);

        /* Текстовый stdout процесса — рисуем в поверхность окна */
        if (w->text_lines > 0 || w->text_col > 0) {
            int maxln = ((int)w->h - UWM_TITLE_H - 6) / 10;
            if (maxln > 63) maxln = 63;
            for (int ln = 0; ln <= w->text_lines && ln < maxln; ln++) {
                fb_text_buf(w->surface, (uint32_t)w->w, (uint32_t)w->w, (uint32_t)w->h,
                            6, UWM_TITLE_H + 4 + ln * 10, w->text[ln],
                            0x00E2E8F0, UWM_BG);
            }
        }

        fb_blit((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h, w->surface);

        uint32_t border = active ? UWM_BORDER_ACT : UWM_BORDER;
        fb_draw_box((uint32_t)w->x, (uint32_t)w->y, (uint32_t)w->w, (uint32_t)w->h, border);

        uint32_t tbg = active ? UWM_TITLE_BG : 0x00182028;
        fb_fill_rect((uint32_t)w->x + 1, (uint32_t)w->y + 1,
                     (uint32_t)w->w - 2, UWM_TITLE_H - 4, tbg);
        if (active) fb_fill_rect((uint32_t)w->x + 1, (uint32_t)w->y + UWM_TITLE_H - 3,
                                 (uint32_t)w->w - 2, 2, UWM_BORDER_ACT);
        fb_set_cursor((uint32_t)w->x + 8, (uint32_t)w->y + 8);
        const char *t = w->title;
        while (*t) fb_putc(*t++, active ? UWM_TITLE_FG : 0x008A94A3, tbg);

        uint32_t cbg = active ? UWM_BTN_ACT : UWM_BTN_BG;
        fb_fill_rect((uint32_t)w->x + w->w - 20, (uint32_t)w->y + 4, 16, 16, cbg);
        fb_draw_box((uint32_t)w->x + w->w - 20, (uint32_t)w->y + 4, 16, 16, border);
        fb_set_cursor((uint32_t)w->x + w->w - 16, (uint32_t)w->y + 8);
        fb_putc('X', UWM_TITLE_FG, cbg);
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
            /* Кнопка закрытия (16x16) */
            if (mx >= w->x + w->w - 20 && mx <= w->x + w->w - 4 &&
                my >= w->y + 4 && my <= w->y + 20) {
                uwm_close((int)w->id);
                return 1;
            }
            /* Заголовок: активация + начало перетаскивания */
            g_drag_id = (int)w->id;
            g_drag_off_x = mx - w->x;
            g_drag_off_y = my - w->y;
            return 1;
        }

        push_event(w, GUI_EV_CLICK, 0, mx - w->x, my - w->y);
        return 1;
    }
    g_active_id = -1;
    return 0;
}

/* Перетаскивание user-окна за заголовок. Возвращает 1, если окно сдвинулось. */
int uwm_handle_drag(int mx, int my) {
    if (g_drag_id < 0) return 0;
    uwm_window_t *w = find_win(g_drag_id);
    if (!w) { g_drag_id = -1; return 0; }

    int nx = mx - g_drag_off_x;
    int ny = my - g_drag_off_y;
    if (nx < 0) nx = 0;
    if (ny < 24) ny = 24;
    if (nx + w->w > (int)fb_width())  nx = (int)fb_width() - w->w;
    if (ny + w->h > (int)fb_height() - 26) ny = (int)fb_height() - w->h - 26;

    if (nx == w->x && ny == w->y) return 0;
    w->x = nx;
    w->y = ny;
    return 1;
}

void uwm_end_drag(void) {
    g_drag_id = -1;
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
