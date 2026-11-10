#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

void keyboard_init(void);
void keyboard_irq(void);
int  keyboard_getchar(void);
int  keyboard_has_char(void);
void keyboard_push_char(char c);   /* ввод ASCII от других источников (USB) */

// DOOM — raw scancode API
void keyboard_push_scancode(uint8_t sc);
int  keyboard_pop_scancode(void);   // -1 если пусто

// Сброс очередей (после выхода из Doom, чтобы ESC не «утекал» в GUI)
void keyboard_flush(void);

#endif