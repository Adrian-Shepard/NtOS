#include <stddef.h>
#include "gui.h"
#include "apps.h"
#include "uwm.h"
#include "fb.h"
#include "sound/cue.h"
#include "driver/input/keyboard/keyboard.h"
#include "driver/input/mouse/mouse.h"
#include "driver/usb/xhci/xhci.h"
#include "../time/pit.h"
#include "../mem/heap.h"
#include "../mem/pmm.h"
#include "../fs/ntmem_fs.h"
#include "../sched/sched.h"
#include "../log/log.h"

#define GUI_BG_DESKTOP 0x000A0A0C
#define GUI_BAR_BG     0x00141A20
#define GUI_BAR_ACCENT 0x00C8102E
#define GUI_WIN_BG     0x00141A20
#define GUI_WIN_BORDER 0x00AA2222
#define GUI_WIN_TITLE  0x00AA2222
#define GUI_TITLE_INACTIVE 0x00404040
#define GUI_TEXT       0x00D0D0D0
#define GUI_TEXT_MUTED 0x00808080
#define GUI_WHITE      0x00FFFFFF
#define GUI_RED        0x00FF3333
#define GUI_GREEN      0x0044FF88
#define GUI_CYAN       0x0044DDFF
#define GUI_ORANGE     0x00FFAA00
#define GUI_BTN_BG     0x00303030

int g_gui_active = 0;

static window_t g_windows[MAX_WINDOWS];
static int g_next_win_id = 1;
static int g_drag_idx = -1;
static int g_drag_off_x = 0, g_drag_off_y = 0;
static int g_active_win_id = -1;

/* Состояние выделения ярлыков рабочего стола */
static int      g_selected_icon   = -1;
static int      g_last_click_icon = -1;
static uint64_t g_last_click_tick = 0;
static int g_exit_to_shell = 0;
static int g_shutdown = 0;
static uint32_t *g_gui_buf = 0;

static process_t *g_gui_children[MAX_WINDOWS] = {0};

extern desktop_shortcut_t g_desktop_shortcuts[];
extern int g_desktop_shortcut_count;

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

static void put_str_gui(const char *s, uint32_t fg, uint32_t bg) { while (*s) fb_putc(*s++, fg, bg); }

static int gui_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int gui_strncmp(const char *a, const char *b, int n) {
    while (n && *a && *b && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

/* Простое альфа-смешивание двух RGB-цветов (для полупрозрачной подсветки) */
static uint32_t blend_color(uint32_t bg, uint32_t fg, uint32_t alpha) {
    uint32_t br = (bg >> 16) & 0xFF, bgg = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint32_t fr = (fg >> 16) & 0xFF, fgg = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    uint32_t r = (br * (255 - alpha) + fr * alpha) / 255;
    uint32_t g = (bgg * (255 - alpha) + fgg * alpha) / 255;
    uint32_t b = (bb * (255 - alpha) + fb * alpha) / 255;
    return 0x00000000u | (r << 16) | (g << 8) | b;
}

/* Позиция ярлыка на сетке рабочего стола (как иконки в Windows/Linux) */
static void desktop_icon_pos(int i, int *ox, int *oy) {
    int col = i % 4;
    int row = i / 4;
    *ox = 28 + col * 112;
    *oy = 92 + row * 106;
}

static void draw_desktop_icons(void) {
    for (int i = 0; i < g_desktop_shortcut_count; i++) {
        int ix, iy;
        desktop_icon_pos(i, &ix, &iy);

        /* полупрозрачная синеватая подсветка выбранного ярлыка */
        if (i == g_selected_icon) {
            uint32_t hl = blend_color(GUI_BG_DESKTOP, 0x0044DDFF, 110);
            fb_fill_rect(ix - 6, iy - 6, 60, 60, hl);
            fb_draw_box(ix - 6, iy - 6, 60, 60, 0x00449ACC);
        }

        /* иконка 48x48 */
        fb_fill_rect(ix, iy, 48, 48, GUI_WIN_BG);
        fb_draw_box(ix, iy, 48, 48, g_desktop_shortcuts[i].icon_color);
        fb_set_cursor(ix + 8, iy + 12);
        put_str_gui(g_desktop_shortcuts[i].icon_text, g_desktop_shortcuts[i].icon_color, GUI_WIN_BG);

        /* подпись: по центру, обрезаем до 13 символов, чтобы не налезать на соседей */
        const char *name = g_desktop_shortcuts[i].name;
        int len = 0;
        while (name[len] && len < 13) len++;
        int lx = ix + 24 - len * 4;
        if (lx < ix - 10) lx = ix - 10;
        fb_set_cursor(lx, iy + 54);
        for (int k = 0; k < len; k++) fb_putc(name[k], GUI_TEXT, GUI_BG_DESKTOP);
    }
}

static void launch_shortcut(int index) {
    if (index < 0 || index >= g_desktop_shortcut_count) return;
    cue_play(CUE_NOTIFY);   /* звук запуска приложения */
    const char *path = g_desktop_shortcuts[index].exec_path;

    if (gui_strncmp(path, "KERNEL://", 9) == 0) {
        const char *sys_app = path + 9;
        if (gui_strcmp(sys_app, "terminal") == 0) {
            if (app_terminal.init) app_terminal.init();
            wm_create_window(app_terminal.name, 200, 100, 500, 350, app_terminal.draw, app_terminal.click, app_terminal.key);
        } else if (gui_strcmp(sys_app, "taskman") == 0) {
            wm_create_window(app_taskman.name, 200, 100, 600, 400, app_taskman.draw, app_taskman.click, app_taskman.key);
        } else if (gui_strcmp(sys_app, "editor") == 0) {
            wm_create_window(app_editor.name, 200, 100, 500, 350, app_editor.draw, app_editor.click, app_editor.key);
        } else if (gui_strcmp(sys_app, "doom") == 0) {
            extern void doom_main(void);
            doom_main();     // Запуск DOOM. При выходе (ESC) управление вернётся сюда.
            keyboard_flush(); // Сбрасываем очереди, чтобы ESC из Doom не закрыл GUI
        }
    } else {
        ntmem_file_t *f = ntmem_fs_find(path);
        if (f) {
            process_t *p = process_create_user(f->data, f->size, 1, (const char **)&path);
            if (p) {
                int slot = -1;
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (g_gui_children[i] == 0) { slot = i; break; }
                if (slot >= 0) g_gui_children[slot] = p;
            }
        }
    }
}

int wm_create_window(const char *title, int x, int y, int w, int h,
                     void (*draw)(window_t*), void (*click)(window_t*, int, int), void (*key)(window_t*, int)) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_open) {
            g_windows[i].id = g_next_win_id++;
            g_windows[i].x = x; g_windows[i].y = y; g_windows[i].w = w; g_windows[i].h = h;
            int k = 0; while (title[k] && k < 63) { g_windows[i].title[k] = title[k]; k++; } g_windows[i].title[k] = 0;
            g_windows[i].is_open = 1;
            g_windows[i].draw_content = draw;
            g_windows[i].on_click = click;
            g_windows[i].on_key = key;

            window_t tmp = g_windows[i];
            for (int j = i; j < MAX_WINDOWS - 1; j++) g_windows[j] = g_windows[j + 1];
            g_windows[MAX_WINDOWS - 1] = tmp;

            g_active_win_id = tmp.id;
            cue_play(CUE_UI_REVEAL);   /* звук открытия окна (C4-E4-G4) */
            return tmp.id;
        }
    }
    return -1;
}

void gui_write(const char *s, uint64_t len) { app_term_write(s, len); }

static void draw_top_bar(void) {
    uint32_t w = fb_width();
    fb_fill_rect(0, 0, w, 24, GUI_BAR_BG);
    fb_fill_rect(0, 24, w, 2, GUI_BAR_ACCENT);
    fb_fill_rect(0, 0, 120, 24, GUI_BAR_ACCENT);
    fb_set_cursor(16, 8);
    put_str_gui("NANOTRASEN", GUI_WHITE, GUI_BAR_ACCENT);

    char ts[80]; int idx = 0;
    const char *pfx = "UPTIME: ";
    for (int i = 0; pfx[i]; i++) ts[idx++] = pfx[i];

    uint64_t total_sec = pit_ticks() / 100;
    uint64_t years = total_sec / (365ULL * 24 * 3600);
    uint64_t days  = (total_sec / (24 * 3600)) % 365;
    uint64_t hours = (total_sec / 3600) % 24;
    uint64_t mins  = (total_sec / 60) % 60;
    uint64_t secs  = total_sec % 60;

    char nb[24]; int n;
    n = 0;
    if (years == 0) nb[n++] = '0';
    else { uint64_t y = years; char rv[24]; int r = 0; while (y) { rv[r++] = '0' + (y % 10); y /= 10; } while (r--) nb[n++] = rv[r]; }
    for (int i = 0; i < n; i++) ts[idx++] = nb[i];
    ts[idx++] = ':';

    n = 0;
    if (days == 0) nb[n++] = '0';
    else { uint64_t d = days; char rv[24]; int r = 0; while (d) { rv[r++] = '0' + (d % 10); d /= 10; } while (r--) nb[n++] = rv[r]; }
    for (int i = n; i < 3; i++) ts[idx++] = '0';
    for (int i = 0; i < n; i++) ts[idx++] = nb[i];
    ts[idx++] = ':';
    ts[idx++] = '0' + (hours / 10) % 10;
    ts[idx++] = '0' + (hours % 10);
    ts[idx++] = ':';
    ts[idx++] = '0' + (mins / 10) % 10;
    ts[idx++] = '0' + (mins % 10);
    ts[idx++] = ':';
    ts[idx++] = '0' + (secs / 10) % 10;
    ts[idx++] = '0' + (secs % 10);
    ts[idx] = 0;

    int len = idx;
    int x = w - (len * 8) - 16;
    if (x < 130) x = 130;
    fb_set_cursor(x, 8);
    put_str_gui(ts, GUI_TEXT, GUI_BAR_BG);
}

static void draw_bottom_bar(void) {
    uint32_t w = fb_width();
    int by = (int)fb_height() - 24;
    fb_fill_rect(0, by, w, 24, GUI_BAR_BG);
    fb_fill_rect(0, by, w, 2, GUI_BAR_ACCENT);

    fb_set_cursor(16, by + 8);
    put_str_gui("NANOTRASEN SECURITY SYSTEMS // CENTCOM DESKTOP", GUI_TEXT_MUTED, GUI_BAR_BG);

    const char *r = "apps are loaded from /bin and /etc";
    int rlen = 0;
    while (r[rlen]) rlen++;
    fb_set_cursor((int)w - (rlen * 8) - 16, by + 8);
    put_str_gui(r, GUI_TEXT_MUTED, GUI_BAR_BG);
}

static void draw_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_open) continue;
        window_t *win = &g_windows[i];

        fb_fill_rect(win->x + 4, win->y + 4, win->w, win->h, 0x00000000);
        fb_fill_rect(win->x, win->y, win->w, win->h, GUI_WIN_BG);

        uint32_t border = (win->id == g_active_win_id) ? GUI_WIN_BORDER : GUI_TEXT_MUTED;
        fb_draw_box(win->x, win->y, win->w, win->h, border);

        uint32_t tbg = (win->id == g_active_win_id) ? GUI_WIN_TITLE : GUI_TITLE_INACTIVE;
        fb_fill_rect(win->x + 1, win->y + 1, win->w - 2, TITLE_BAR_H - 4, tbg);
        fb_set_cursor(win->x + 8, win->y + 8);
        put_str_gui(win->title, GUI_WHITE, tbg);

        fb_fill_rect(win->x + win->w - 20, win->y + 4, 16, 16, GUI_BTN_BG);
        fb_draw_box(win->x + win->w - 20, win->y + 4, 16, 16, GUI_WIN_BORDER);
        fb_set_cursor(win->x + win->w - 16, win->y + 8);
        put_str_gui("X", GUI_TEXT, GUI_BTN_BG);

        if (win->draw_content) win->draw_content(win);
    }

    /* Окна пользовательских процессов (поверх встроенных) */
    uwm_draw_all();
}

static void restore_cursor_bg(void) {
    if (last_mx == -100) return;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t *ptr = (uint32_t *)fb_get_real_ptr(last_mx + x, last_my + y);
        if (ptr) *ptr = saved_bg[y][x];
    }
}

static void draw_cursor(int mx, int my) {
    if (mx < 0 || my < 0 || mx + 16 >= (int)fb_width() || my + 16 >= (int)fb_height()) return;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t *ptr = (uint32_t *)fb_get_real_ptr(mx + x, my + y);
        if (!ptr) continue;
        saved_bg[y][x] = *ptr;
        if (cursor_bitmap[y][x] == 1) *ptr = 0x00000000;
        else if (cursor_bitmap[y][x] == 2) *ptr = 0x00FFFFFF;
    }
    last_mx = mx; last_my = my;
}

/* Синеватый вертикальный градиент рабочего стола */
static void draw_desktop_bg(void) {
    uint32_t w = fb_width(), h = fb_height();
    uint32_t top    = 0x00131C28;
    uint32_t bottom = 0x00070B12;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t c = blend_color(bottom, top, ((h - y) * 255) / h);
        fb_fill_rect(0, y, w, 1, c);
    }
}

static void redraw_gui(int mx, int my) {
    draw_desktop_bg();
    draw_top_bar();
    draw_desktop_icons();
    draw_windows();
    draw_bottom_bar();
    fb_swap();
    last_mx = -100;
    draw_cursor(mx, my);
}

/* ================================================================== */
int gui_start(void) {
    /* Освобождаемый буфер. Размер по pitch (в пикселях), а не по ширине,
       иначе fb_swap() читает за границей при pitch > width. */
    uint32_t buf_size = fb_pitch_px() * fb_height() * 4;
    g_gui_buf = (uint32_t *)kmalloc(buf_size);
    if (g_gui_buf) fb_set_buffer(g_gui_buf);

    /* <<< Подавляем вывод отладки в framebuffer, чтобы не засорять UI */
    log_set_fb_output(0);

    g_gui_active = 1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_open = 0;
        g_gui_children[i] = 0;
    }

    /* Заполняем рабочий стол из конфига и /bin (автообнаружение приложений) */
    desktop_init();

    redraw_gui(400, 300);

    int last_left = 0;
    uint64_t last_sec = pit_ticks() / 100;
    uint64_t last_draw_ticks = 0;

    while (!g_exit_to_shell && !g_shutdown) {
        __asm__ volatile("sti");

        /* <<< Локальный опрос xHCI для свежих нажатий */
        xhci_poll();

        /* PS/2 и USB-клавиатура объединены в keyboard_getchar()
           (USB мостится в PS/2-буферы в xhci.c) */
        int c = keyboard_getchar();
        if (c == 27) break;

        if (c) {
            /* Сначала доставляем клавишу активному пользовательскому окну */
            if (!uwm_deliver_key(c) && g_active_win_id >= 0) {
                window_t *win = &g_windows[MAX_WINDOWS - 1];
                if (win->is_open && win->on_key) {
                    win->on_key(win, c);
                    win->draw_content(win);
                    restore_cursor_bg();
                    fb_swap_rect(win->x, win->y, win->w, win->h);
                    draw_cursor(mouse_get_state().x, mouse_get_state().y);
                }
            }
        }

        uint64_t current_sec = pit_ticks() / 100;
        if (current_sec != last_sec) {
            last_sec = current_sec;
            draw_top_bar();
            fb_swap_rect(0, 0, fb_width(), 26);
        }

        uint64_t current_ticks = pit_ticks();
        if (current_ticks - last_draw_ticks >= 10) {
            last_draw_ticks = current_ticks;

            /* reap завершившихся и закрываем их окна */
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (g_gui_children[i] && g_gui_children[i]->state == PROC_ZOMBIE) {
                    uwm_close_by_pid(g_gui_children[i]->pid);
                    process_reap(g_gui_children[i]);
                    g_gui_children[i] = 0;
                }
            }

            if (uwm_has_windows()) {
                /* Пользовательские окна обновляются асинхронно — полная перерисовка */
                redraw_gui(mouse_get_state().x, mouse_get_state().y);
            } else if (g_active_win_id >= 0) {
                window_t *win = &g_windows[MAX_WINDOWS - 1];
                if (win->is_open && win->draw_content) {
                    win->draw_content(win);
                    restore_cursor_bg();
                    fb_swap_rect(win->x, win->y, win->w, win->h);
                    draw_cursor(mouse_get_state().x, mouse_get_state().y);
                }
            }
        }

        mouse_state_t m = mouse_get_state();
        if (m.left && !last_left) {
            int handled = 0;

            /* иконки рабочего стола: одинарный клик — выделение,
               двойной клик (в пределах 500 мс) — запуск */
            for (int i = 0; i < g_desktop_shortcut_count; i++) {
                int ix, iy;
                desktop_icon_pos(i, &ix, &iy);
                if (m.x >= ix - 8 && m.x <= ix + 56 && m.y >= iy - 6 && m.y <= iy + 66) {
                    uint64_t now = pit_ticks();
                    if (g_last_click_icon == i && (now - g_last_click_tick) <= 50) {
                        launch_shortcut(i);       /* двойной клик */
                    } else {
                        cue_play(CUE_CLICK);      /* одинарный клик — выделение */
                    }
                    g_selected_icon   = i;
                    g_last_click_icon = i;
                    g_last_click_tick = now;
                    redraw_gui(m.x, m.y);
                    handled = 1;
                    break;
                }
            }

            /* клик мимо ярлыка — снимаем выделение */
            if (!handled && g_selected_icon != -1) {
                g_selected_icon = -1;
                redraw_gui(m.x, m.y);
            }

            /* встроенные окна */
            if (!handled) {
                for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                    if (!g_windows[i].is_open) continue;
                    window_t *win = &g_windows[i];
                    if (m.x >= win->x && m.x <= win->x + win->w && m.y >= win->y && m.y <= win->y + win->h) {
                        g_active_win_id = win->id;
                        uwm_clear_active();
                        window_t tmp = g_windows[i];
                        for (int k = i; k < MAX_WINDOWS - 1; k++) g_windows[k] = g_windows[k + 1];
                        g_windows[MAX_WINDOWS - 1] = tmp;
                        win = &g_windows[MAX_WINDOWS - 1];

                        if (m.y <= win->y + TITLE_BAR_H) {
                            if (m.x >= win->x + win->w - 20 && m.x <= win->x + win->w - 4) {
                                win->is_open = 0;
                                g_active_win_id = -1;
                                cue_play(CUE_UI_SOFT);   /* звук закрытия окна */
                            } else {
                                g_drag_idx = MAX_WINDOWS - 1;
                                g_drag_off_x = m.x - win->x;
                                g_drag_off_y = m.y - win->y;
                            }
                        } else if (win->on_click) {
                            win->on_click(win, m.x, m.y);
                        }
                        redraw_gui(m.x, m.y);
                        handled = 1;
                        break;
                    }
                }
            }

            /* окна пользовательских процессов */
            if (!handled) {
                if (uwm_handle_click(m.x, m.y)) {
                    g_active_win_id = -1;
                    redraw_gui(m.x, m.y);
                }
            }
        }

        if (!m.left && last_left) g_drag_idx = -1;

        if (m.left && g_drag_idx >= 0) {
            window_t *win = &g_windows[g_drag_idx];
            int nx = m.x - g_drag_off_x, ny = m.y - g_drag_off_y;
            if (nx < 0) nx = 0;
            if (ny < 24) ny = 24;
            if (nx + win->w > (int)fb_width())  nx = fb_width() - win->w;
            if (ny + win->h > (int)fb_height() - 26) ny = fb_height() - win->h - 26;
            if (nx != win->x || ny != win->y) {
                win->x = nx; win->y = ny;
                redraw_gui(m.x, m.y);
            }
        }

        if (m.x != last_mx || m.y != last_my) {
            restore_cursor_bg();
            draw_cursor(m.x, m.y);
        }
        last_left = m.left;
        __asm__ volatile("hlt");
    }

cleanup:
    __asm__ volatile("cli");
    restore_cursor_bg();

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_gui_children[i] && g_gui_children[i]->state == PROC_ZOMBIE)
            process_reap(g_gui_children[i]);
        g_gui_children[i] = 0;
    }

    fb_set_buffer(0);
    fb_clear(0x00101018);
    fb_set_cursor(0, 0);

    log_set_fb_output(1);   /* восстанавливаем вывод отладки в fb */
    __asm__ volatile("sti");

    g_gui_active = 0;
    if (g_gui_buf) { kfree(g_gui_buf); g_gui_buf = 0; }   /* гарантированное освобождение */
    return g_exit_to_shell;
}