#include "synth.h"
#include "driver/sb16.h" // опционально, если нужно
#include "../log/log.h"
#include <stdint.h>

#define SAMPLE_RATE 22050

typedef struct {
    uint32_t phase;
    uint32_t phase_inc;
    uint8_t  wave;
    uint8_t  volume;
    uint8_t  active;
    int32_t  env;       /* 0..32767 */
    int8_t   env_dir;   /* 1 = attack, -1 = release, 0 = sustain */
} voice_t;

static voice_t g_voices[SYNTH_VOICES];

/* sine table */
static const int8_t g_sine[256] = {
      0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 124, 125, 125, 126, 126, 126, 126, 126,
    127, 126, 126, 126, 126, 126, 125, 125, 124, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
   -117,-118,-120,-121,-122,-122,-123,-124,-124,-125,-125,-126,-126,-126,-126,-126,
   -127,-126,-126,-126,-126,-126,-125,-125,-124,-124,-123,-122,-122,-121,-120,-118,
   -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3
};

void synth_init(void) {
    for (int i = 0; i < SYNTH_VOICES; i++) {
        g_voices[i].active = 0;
        g_voices[i].phase  = 0;
        g_voices[i].env    = 0;
        g_voices[i].env_dir = 0;
    }
    logi(S_DRV, "synth initialized (%d voices)", SYNTH_VOICES);
}

void synth_note_on(uint8_t voice, uint32_t freq, wave_t wave, uint8_t volume) {
    if (voice >= SYNTH_VOICES) return;
    voice_t *v = &g_voices[voice];
    v->phase = 0;
    v->phase_inc = (uint32_t)(((uint64_t)freq << 32) / SAMPLE_RATE);
    v->wave = (uint8_t)wave;
    v->volume = volume;
    v->active = 1;
    v->env_dir = 1; /* attack */
    if (v->env == 0) v->env = 1;
}

void synth_note_off(uint8_t voice) {
    if (voice >= SYNTH_VOICES) return;
    g_voices[voice].env_dir = -1; /* release */
}

void synth_render(uint8_t *out, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
        int32_t mix = 0;

        for (int v_idx = 0; v_idx < SYNTH_VOICES; v_idx++) {
            voice_t *v = &g_voices[v_idx];
            if (!v->active) continue;

            /* envelope */
            v->env += v->env_dir * 100; /* ~0.3 сек на полный ход */
            if (v->env >= 32767) { v->env = 32767; v->env_dir = 0; }
            if (v->env <= 0 && v->env_dir < 0) {
                v->env = 0; v->env_dir = 0; v->active = 0; continue;
            }

            /* generate wave */
            int32_t sample = 0;
            uint8_t phase_hi = v->phase >> 24;
            
            switch (v->wave) {
                case WAVE_SINE:
                    sample = g_sine[phase_hi];
                    break;
                case WAVE_SQUARE:
                    sample = (phase_hi < 128) ? 127 : -127;
                    break;
                case WAVE_SAW:
                    sample = (int32_t)(v->phase >> 24) - 128;
                    sample *= 2;
                    break;
                case WAVE_TRIANGLE: {
                    int32_t p = (int32_t)(v->phase >> 24);
                    if (p < 128) sample = p * 2 - 128;
                    else sample = (255 - p) * 2 - 128;
                    sample *= 2;
                    break;
                }
            }

            v->phase += v->phase_inc;

            /* scale by volume (0..255) and envelope (0..32767) */
            int32_t amp = (v->volume * (v->env >> 8)) >> 8;
            mix += (sample * amp) >> 7;
        }

        /* clamp to 8-bit unsigned */
        mix = mix + 128;
        if (mix < 0) mix = 0;
        if (mix > 255) mix = 255;
        out[i] = (uint8_t)mix;
    }
}