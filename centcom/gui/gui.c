#include <stddef.h>
#include "gui.h"
#include "apps.h"
#include "fb.h"
#include "../driver/keyboard.h"
#include "../driver/mouse.h"
#include "../driver/xhci.h"      // <-- ДОБАВЛЕНО: Заголовок xHCI
#include "../time/pit.h"
#include "../mem/heap.h"
#include "../mem/pmm.h"
#include "../fs/ntmem_fs.h"
#include "../sched/sched.h"

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
static int g_exit_to_shell = 0;
static int g_shutdown = 0;

// Массив для отслеживания дочерних процессов, запущенных с рабочего стола
static process_t* g_gui_children[MAX_WINDOWS] = {0};

// Внешние переменные из desktop.c
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

// Вспомогательные функции для строк
static int gui_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int gui_strncmp(const char *a, const char *b, int n) {
    while (n && *a && *b && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static void draw_desktop_icons(void) {
    for (int i = 0; i < g_desktop_shortcut_count; i++) {
        int ix = 32;
        int iy = 80 + i * 80;
        
        fb_fill_rect(ix, iy, 32, 32, GUI_WIN_BG);
        fb_draw_box(ix, iy, 32, 32, g_desktop_shortcuts[i].icon_color);
        fb_set_cursor(ix + 4, iy + 4);
        put_str_gui(g_desktop_shortcuts[i].icon_text, g_desktop_shortcuts[i].icon_color, GUI_WIN_BG);
        
        fb_set_cursor(ix - 10, iy + 36);
        put_str_gui(g_desktop_shortcuts[i].name, GUI_TEXT, GUI_BG_DESKTOP);
    }
}

static void launch_shortcut(int index) {
    if (index < 0 || index >= g_desktop_shortcut_count) return;
    
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
        }
    } 
    else {
        ntmem_file_t *f = ntmem_fs_find(path);
        if (f) {
            process_t *p = process_create_user(f->data, f->size, 1, (const char**)&path);
            if (p) {
                for (int i = 0; i < MAX_WINDOWS; i++) {
                    if (g_gui_children[i] == 0) {
                        g_gui_children[i] = p;
                        break;
                    }
                }
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
            int k = 0; while(title[k] && k < 63) { g_windows[i].title[k] = title[k]; k++; } g_windows[i].title[k] = 0;
            g_windows[i].is_open = 1;
            g_windows[i].draw_content = draw;
            g_windows[i].on_click = click;
            g_windows[i].on_key = key;
            
            window_t tmp = g_windows[i];
            for (int j = i; j < MAX_WINDOWS - 1; j++) g_windows[j] = g_windows[j + 1];
            g_windows[MAX_WINDOWS - 1] = tmp;
            
            g_active_win_id = tmp.id;
            return tmp.id;
        }
    }
    return -1;
}

void gui_write(const char *s, uint64_t len) {
    app_term_write(s, len);
}

static void draw_top_bar(void) {
    uint32_t w = fb_width();
    fb_fill_rect(0, 0, w, 24, GUI_BAR_BG);
    fb_fill_rect(0, 24, w, 2, GUI_BAR_ACCENT);
    fb_fill_rect(0, 0, 120, 24, GUI_BAR_ACCENT); 
    fb_set_cursor(16, 8);
    put_str_gui("NANOTRASEN", GUI_WHITE, GUI_BAR_ACCENT);
    
    char time_str[80];
    int idx = 0;
    const char *prefix = "UPTIME: ";
    for (int i = 0; prefix[i]; i++) time_str[idx++] = prefix[i];
    
    uint64_t total_sec = pit_ticks() / 100;
    uint64_t years   = total_sec / (365ULL * 24 * 3600);
    uint64_t days    = (total_sec / (24 * 3600)) % 365;
    uint64_t hours   = (total_sec / 3600) % 24;
    uint64_t minutes = (total_sec / 60) % 60;
    uint64_t seconds = total_sec % 60;

    char num_buf[24]; int n;
    n = 0;
    if (years == 0) { num_buf[n++] = '0'; } else { uint64_t y = years; char rev[24]; int r = 0; while (y) { rev[r++] = '0' + (y % 10); y /= 10; } while (r--) num_buf[n++] = rev[r]; }
    for (int i = 0; i < n; i++) time_str[idx++] = num_buf[i];
    time_str[idx++] = ':';

    n = 0;
    if (days == 0) { num_buf[n++] = '0'; } else { uint64_t d = days; char rev[24]; int r = 0; while (d) { rev[r++] = '0' + (d % 10); d /= 10; } while (r--) num_buf[n++] = rev[r]; }
    for (int i = n; i < 3; i++) time_str[idx++] = '0';
    for (int i = 0; i < n; i++) time_str[idx++] = num_buf[i];
    time_str[idx++] = ':';

    time_str[idx++] = '0' + (hours / 10) % 10;
    time_str[idx++] = '0' + (hours % 10);
    time_str[idx++] = ':';
    time_str[idx++] = '0' + (minutes / 10) % 10;
    time_str[idx++] = '0' + (minutes % 10);
    time_str[idx++] = ':';
    time_str[idx++] = '0' + (seconds / 10) % 10;
    time_str[idx++] = '0' + (seconds % 10);
    time_str[idx] = 0;

    int len = idx;
    int x = w - (len * 8) - 16;
    if (x < 130) x = 130;
    fb_set_cursor(x, 8);
    put_str_gui(time_str, GUI_TEXT, GUI_BAR_BG);
}

static void draw_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].is_open) continue;
        window_t *win = &g_windows[i];
        
        fb_fill_rect(win->x + 4, win->y + 4, win->w, win->h, 0x00000000);
        fb_fill_rect(win->x, win->y, win->w, win->h, GUI_WIN_BG);
        
        uint32_t border = (win->id == g_active_win_id) ? GUI_WIN_BORDER : GUI_TEXT_MUTED;
        fb_draw_box(win->x, win->y, win->w, win->h, border);
        
        uint32_t title_bg = (win->id == g_active_win_id) ? GUI_WIN_TITLE : GUI_TITLE_INACTIVE;
        fb_fill_rect(win->x + 1, win->y + 1, win->w - 2, TITLE_BAR_H - 4, title_bg);
        fb_set_cursor(win->x + 8, win->y + 8);
        put_str_gui(win->title, GUI_WHITE, title_bg);
        
        fb_fill_rect(win->x + win->w - 20, win->y + 4, 16, 16, GUI_BTN_BG);
        fb_draw_box(win->x + win->w - 20, win->y + 4, 16, 16, GUI_WIN_BORDER);
        fb_set_cursor(win->x + win->w - 16, win->y + 8);
        put_str_gui("X", GUI_TEXT, GUI_BTN_BG);

        if (win->draw_content) win->draw_content(win);
    }
}

static void restore_cursor_bg(void) {
    if (last_mx == -100) return;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t *ptr = (uint32_t*)fb_get_real_ptr(last_mx + x, last_my + y);
        if (ptr) *ptr = saved_bg[y][x];
    }
}

static void draw_cursor(int mx, int my) {
    if (mx < 0 || my < 0 || mx + 16 >= (int)fb_width() || my + 16 >= (int)fb_height()) return;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        uint32_t *ptr = (uint32_t*)fb_get_real_ptr(mx + x, my + y);
        if (!ptr) continue;
        saved_bg[y][x] = *ptr; 
        if (cursor_bitmap[y][x] == 1) *ptr = 0x00000000;
        else if (cursor_bitmap[y][x] == 2) *ptr = 0x00FFFFFF;
    }
    last_mx = mx; last_my = my;
}

static void redraw_gui(int mx, int my) {
    fb_clear(GUI_BG_DESKTOP);    
    draw_top_bar();
    draw_desktop_icons();
    draw_windows();
    fb_swap(); 
    last_mx = -100; 
    draw_cursor(mx, my); 
}

int gui_start(void) {
    uint32_t buf_size = fb_width() * fb_height() * 4;
    uint32_t *gui_buf = (uint32_t*)kmalloc(buf_size);
    if (gui_buf) fb_set_buffer(gui_buf);

    g_gui_active = 1; 
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_windows[i].is_open = 0;
        g_gui_children[i] = 0;
    }
    
    redraw_gui(400, 300);

    int last_left = 0;
    uint64_t last_sec = pit_ticks() / 100; 
    uint64_t last_draw_ticks = 0;

    while (!g_exit_to_shell && !g_shutdown) {
        __asm__ volatile("sti"); 
        
        // ОПРОС USB КОНТРОЛЛЕРА
        xhci_poll();

        int c = keyboard_getchar();
        if (c == 27) break;

        if (c && g_active_win_id >= 0) {
            window_t *win = &g_windows[MAX_WINDOWS - 1];
            if (win->is_open && win->on_key) {
                win->on_key(win, c);
                win->draw_content(win);
                restore_cursor_bg();
                fb_swap_rect(win->x, win->y, win->w, win->h);
                draw_cursor(mouse_get_state().x, mouse_get_state().y);
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
            
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (g_gui_children[i] != 0) {
                    if (g_gui_children[i]->state == PROC_ZOMBIE) {
                        process_reap(g_gui_children[i]);
                        g_gui_children[i] = 0;
                    }
                }
            }

            if (g_active_win_id >= 0) {
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
            int icon_clicked = 0;
            for (int i = 0; i < g_desktop_shortcut_count; i++) {
                int ix = 32;
                int iy = 80 + i * 80;
                if (m.x >= ix && m.x <= ix + 32 && m.y >= iy && m.y <= iy + 32) {
                    launch_shortcut(i);
                    redraw_gui(m.x, m.y);
                    icon_clicked = 1;
                    break;
                }
            }
            
            if (!icon_clicked) {
                for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                    if (!g_windows[i].is_open) continue;
                    window_t *win = &g_windows[i];
                    if (m.x >= win->x && m.x <= win->x + win->w && m.y >= win->y && m.y <= win->y + win->h) {
                        g_active_win_id = win->id;
                        window_t tmp = g_windows[i];
                        for (int k = i; k < MAX_WINDOWS - 1; k++) g_windows[k] = g_windows[k + 1];
                        g_windows[MAX_WINDOWS - 1] = tmp;
                        win = &g_windows[MAX_WINDOWS - 1];

                        if (m.y <= win->y + TITLE_BAR_H) {
                            if (m.x >= win->x + win->w - 20 && m.x <= win->x + win->w - 4) {
                                win->is_open = 0;
                                g_active_win_id = -1;
                            } else {
                                g_drag_idx = MAX_WINDOWS - 1;
                                g_drag_off_x = m.x - win->x;
                                g_drag_off_y = m.y - win->y;
                            }
                        } else if (win->on_click) {
                            win->on_click(win, m.x, m.y);
                        }
                        redraw_gui(m.x, m.y);
                        break;
                    }
                }
            }
        }

        if (!m.left && last_left) g_drag_idx = -1;
        
        if (m.left && g_drag_idx >= 0) {
            window_t *win = &g_windows[g_drag_idx];
            int nx = m.x - g_drag_off_x, ny = m.y - g_drag_off_y;
            if (nx < 0) nx = 0; 
            if (ny < 24) ny = 24;
            if (nx + win->w > (int)fb_width()) nx = fb_width() - win->w;
            if (ny + win->h > (int)fb_height()) ny = fb_height() - win->h;
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

    __asm__ volatile("cli");
    restore_cursor_bg();
    fb_set_buffer(0); 
    fb_clear(0x00101018);
    fb_set_cursor(0, 0);
    __asm__ volatile("sti");
    
    g_gui_active = 0;
    if (gui_buf) kfree(gui_buf);
    return g_exit_to_shell;
}