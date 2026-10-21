#pragma once
#include <stdint.h>
void  keyboard_init(void);
void  keyboard_irq(void);
int   keyboard_getchar(void);
int   keyboard_has_char(void);