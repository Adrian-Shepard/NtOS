#pragma once
#include <stdint.h>

extern int g_gui_active;

int gui_start(void);
void gui_write(const char *s, uint64_t len);