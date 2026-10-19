#pragma once
#include <stdint.h>

int  sb16_init(void);
void sb16_set_volume(int percent);

/* старый интерфейс (один тон) */
int  sb16_play_tone_async(int freq_hz, int ms);
void sb16_play_tone(int freq_hz, int ms);

/* новый интерфейс: проиграть готовый PCM-буфер (8-bit unsigned) */
int  sb16_play_pcm_async(const uint8_t *pcm, uint32_t samples);

int  sb16_is_playing(void);
void sb16_poll(void);
void sb16_stop(void);