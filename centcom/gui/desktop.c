#include "gui.h"

// Массив ярлыков на рабочем столе
desktop_shortcut_t g_desktop_shortcuts[] = {
    // Встроенные (в ядро) приложения:
    { "TERMINAL", "KERNEL://terminal", ">_",  0x0044FF88 },
    { "TASK MGR", "KERNEL://taskman",  "|||", 0x00FF3333 },
    { "EDITOR",   "KERNEL://editor",   "TXT", 0x00FFAA00 },
    
    // Пользовательская программа из initrd.tar:
    { "HELLO",    "hello.elf",         "EXE", 0x0044DDFF }
};

int g_desktop_shortcut_count = sizeof(g_desktop_shortcuts) / sizeof(g_desktop_shortcuts[0]);