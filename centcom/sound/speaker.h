#pragma once
#include <stdint.h>

void speaker_init(void);
void speaker_on(uint32_t freq_hz);
void speaker_off(void);
void speaker_beep(uint32_t freq_hz, uint32_t ms);  /* блокирующий */
int  speaker_is_on(void);