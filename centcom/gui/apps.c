#include "apps.h"

app_meta_t app_terminal = {
    .name = "CENTCOM TERMINAL", .icon_text = ">_", .icon_color = 0x0044FF88,
    .init = app_term_init, .draw = app_term_draw, .key = app_term_key
};

app_meta_t app_taskman = {
    .name = "TASK MANAGER", .icon_text = "|||", .icon_color = 0x00FF3333,
    .draw = app_taskman_draw
};

app_meta_t app_editor = {
    .name = "TEXT EDITOR", .icon_text = "TXT", .icon_color = 0x00FFAA00,
    .draw = app_editor_draw, .key = app_editor_key, .click = app_editor_click
};