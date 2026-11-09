#pragma once
#include <stdint.h>
#include "gui.h"

// Структура для регистрации приложения (уже есть в gui.h, но продублируем для ясности)
// Убедитесь, что app_meta_t определена в gui.h

// Внешние объявления (определены в apps.c)
extern app_meta_t app_terminal;
extern app_meta_t app_taskman;
extern app_meta_t app_editor;

// Функция для записи в терминал из ядра (используется в gui.c: gui_write)
void app_term_write(const char *s, uint64_t len);

// Прототипы для инициализации (определены в terminal.c, taskman.c, editor.c)
void app_term_init(void);
void app_term_draw(window_t *win);
void app_term_key(window_t *win, int c);

void app_taskman_draw(window_t *win);

void app_editor_draw(window_t *win);
void app_editor_key(window_t *win, int c);
void app_editor_click(window_t *win, int mx, int my);