#pragma once
#include <stdint.h>
void  keyboard_init(void);
void  keyboard_irq(void);
int   keyboard_getchar(void);   /* 0 если нет */