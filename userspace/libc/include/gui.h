#pragma once

/* Графический API для пользовательских приложений.
 * Структуры ДОЛЖНЫ совпадать с ядром (centcom/gui/uwm.h). */

#define GUI_TITLEBAR_H 24

/* События окна */
#define GUI_EV_NONE  0
#define GUI_EV_KEY   1
#define GUI_EV_CLICK 2
#define GUI_EV_CLOSE 3

typedef struct {
    int x, y, w, h;
    unsigned int color;
} gui_rect_cmd_t;

typedef struct {
    int x, y;
    const char *text;
    unsigned int fg, bg;
} gui_text_cmd_t;

typedef struct {
    int type;   /* GUI_EV_* */
    int key;    /* ASCII для GUI_EV_KEY */
    int x, y;   /* координаты клика для GUI_EV_CLICK */
} gui_event_t;

int gui_win_create(const char *title, int w, int h);
int gui_win_close(int id);
int gui_win_rect(int id, const gui_rect_cmd_t *cmd);
int gui_win_text(int id, const gui_text_cmd_t *cmd);
int gui_win_event(int id, gui_event_t *ev);
int gui_win_set_title(int id, const char *title);

/* uptime системы в тиках PIT (100 Гц) */
unsigned long get_ticks(void);
