#pragma once
#include <stdint.h>

/* ==================================================================
 * UWM — User Window Manager.
 * Окна пользовательских процессов (запущенных из ELF). Приложение
 * рисует в собственный pixel-буфер через syscall'ы, а GUI-цикл ядра
 * блитит эти буферы на экран. Это позволяет добавлять GUI-приложения
 * БЕЗ пересборки ядра.
 * ================================================================== */

#define UWM_MAX_WINDOWS 16
#define UWM_TITLE_H     24

/* События */
#define GUI_EV_NONE  0
#define GUI_EV_KEY   1
#define GUI_EV_CLICK 2
#define GUI_EV_CLOSE 3

/* Эти структуры ДОЛЖНЫ совпадать с userspace/libc/include/gui.h */
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
    int key;    /* ASCII-код для GUI_EV_KEY */
    int x, y;   /* координаты клика для GUI_EV_CLICK */
} gui_event_t;

typedef struct {
    uint32_t   id;
    int        used;
    int        x, y, w, h;
    char       title[64];
    uint32_t  *surface;      /* w * h пикселей */
    uint64_t   owner_pid;

    /* Буфер текстового stdout процесса (для текстовых приложений) */
    char       text[64][96];
    int        text_lines;
    int        text_col;

    gui_event_t evq[8];
    int         ev_head, ev_tail;
} uwm_window_t;

/* API для syscall'ов */
int  uwm_create(const char *title, int w, int h, uint64_t owner_pid);
int  uwm_close(int id);
void uwm_close_by_pid(uint64_t pid);
void uwm_fill_rect(int id, int x, int y, int w, int h, uint32_t color);
void uwm_draw_text(int id, int x, int y, const char *s, uint32_t fg, uint32_t bg);
void uwm_blit(int id, const uint32_t *src, int w, int h);
void uwm_win_write(int id, const char *s, uint64_t len);
void uwm_set_title(int id, const char *title);
int  uwm_poll_event(int id, gui_event_t *ev);

/* Вызывается из gui.c (цикл GUI) */
void uwm_draw_all(void);
int  uwm_has_windows(void);              /* есть ли открытые user-окна */
int  uwm_handle_click(int mx, int my);   /* 1 = клик обработан user-окном */
int  uwm_handle_drag(int mx, int my);    /* 1 = окно перемещено */
void uwm_end_drag(void);                 /* отпустили кнопку мыши */
int  uwm_deliver_key(int key);           /* 1 = клавиша доставлена user-окну */
void uwm_clear_active(void);