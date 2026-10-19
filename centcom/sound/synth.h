#pragma once
#include <stdint.h>

#define SYNTH_VOICES 8

typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_TRIANGLE,
} wave_t;

void synth_init(void);

/* Начать ноту на голосе (0..7) */
void synth_note_on(uint8_t voice, uint32_t freq, wave_t wave, uint8_t volume);

/* Отпустить ноту (переход в Release) */
void synth_note_off(uint8_t voice);

/* Сгенерировать PCM-буфер, смешав все голоса */
void synth_render(uint8_t *out, uint32_t samples);