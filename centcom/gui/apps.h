#pragma once
#include "gui.h"

// Доступные приложения
extern app_meta_t app_terminal;
extern app_meta_t app_taskman;
extern app_meta_t app_editor;

// Запись в терминал (используется в gui.c)
void app_term_write(const char *s, uint64_t len);