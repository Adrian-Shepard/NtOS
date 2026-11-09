#pragma once
#include <stdint.h>

#define MAX_WINDOWS 8
#define TITLE_BAR_H 24

typedef struct window window_t;

struct window {
    int id;
    int x, y, w, h;
    char title[64];
    int is_open;
    
    void (*draw_content)(window_t *win);
    void (*on_click)(window_t *win, int mx, int my);
    void (*on_key)(window_t *win, int key);
};

// Структура для регистрации встроенного (kernel) приложения
typedef struct {
    const char *name;       // Имя для заголовка окна и подписи
    const char *icon_text;  // Текст внутри иконки (например ">_")
    uint32_t icon_color;    // Цвет рамки иконки
    
    void (*init)(void);     // Вызывается при первом запуске (опционально)
    void (*draw)(window_t*);
    void (*click)(window_t*, int, int);
    void (*key)(window_t*, int);
} app_meta_t;

// НОВОЕ: Структура ярлыка на рабочем столе
typedef struct {
    const char *name;        // Имя для отображения на рабочем столе
    const char *exec_path;   // "KERNEL://app_name" или "program.elf"
    const char *icon_text;   // Текст иконки (например ">_")
    uint32_t icon_color;     // Цвет иконки
} desktop_shortcut_t;

extern int g_gui_active;
void gui_write(const char *s, uint64_t len);
int gui_start(void);

int wm_create_window(const char *title, int x, int y, int w, int h,
                     void (*draw)(window_t*), 
                     void (*click)(window_t*, int, int), 
                     void (*key)(window_t*, int));