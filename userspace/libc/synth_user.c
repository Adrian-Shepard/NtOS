#include "synth_user.h"
#include <stdint.h>

static const signed char g_sine[256] = {
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

static int fold_freq(int freq) {
    if (freq <= 0) return 0;
    while (freq < 100) freq *= 2;
    while (freq > 2000) freq /= 2;
    return freq;
}

size_t synth_chord(uint8_t *buf, size_t max_samples, int *freqs, int count, int ms, int wave, int volume) {
    size_t n = (size_t)((long)SAMPLE_RATE_USER * ms / 1000);
    if (n > max_samples) n = max_samples;
    if (n == 0 || count == 0) return 0;

    unsigned int phases[8] = {0};
    unsigned int phase_incs[8] = {0};
    int vcount = 0;

    for (int i = 0; i < count && i < 8; i++) {
        int f = fold_freq(freqs[i]);
        if (f == 0) continue;
        phase_incs[vcount] = (unsigned int)(((unsigned long)f << 32) / SAMPLE_RATE_USER);
        vcount++;
    }
    if (vcount == 0) return 0;

    int amp = volume * 127 / 100;
    if (amp > 127) amp = 127;

    int attack = 50;
    int release = 50;
    if ((size_t)(attack + release) > n) { attack = n / 2; release = n / 2; }

    for (size_t i = 0; i < n; i++) {
        int mix = 0;
        for (int v = 0; v < vcount; v++) {
            int sample = 0;
            unsigned char hi = phases[v] >> 24;
            switch (wave) {
                case 0: sample = g_sine[hi]; break;
                case 1: sample = (hi < 128) ? 127 : -127; break;
                case 2: sample = ((int)hi - 128) * 2; break;
                case 3: {
                    int p = hi;
                    if (p < 128) sample = p * 2 - 128;
                    else         sample = (255 - p) * 2 - 128;
                    sample *= 2;
                    break;
                }
            }
            mix += sample; 
            phases[v] += phase_incs[v];
        }
        
        long env = 127;
        if (i < (size_t)attack) env = (i * 127) / attack;
        else if (i > n - release) env = ((n - i) * 127) / release;

        // Делим на 2, чтобы избежать клиппинга
        int v = 128 + (mix * amp * env / (127 * 127 * 2));
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        buf[i] = (uint8_t)v;
    }
    return n;
}

size_t synth_note(uint8_t *buf, size_t max_samples, int freq, int ms, int wave, int volume) {
    return synth_chord(buf, max_samples, &freq, 1, ms, wave, volume);
}

// Функция для midiplay (собирает все ноты в один трек)
size_t synth_track(uint8_t *buf, size_t max_samples, synth_note_t *notes, int note_count, int total_ms, int wave, int volume) {
    size_t total_samples = (SAMPLE_RATE_USER * total_ms) / 1000;
    if (total_samples > max_samples) total_samples = max_samples;
    if (total_samples == 0) return 0;

    for (size_t i = 0; i < total_samples; i++) buf[i] = 128;

    int amp = volume * 127 / 100;
    if (amp > 127) amp = 127;

    for (int n = 0; n < note_count; n++) {
        synth_note_t *note = &notes[n];
        size_t start_samp = (SAMPLE_RATE_USER * note->start_ms) / 1000;
        size_t dur_samp = (SAMPLE_RATE_USER * note->duration_ms) / 1000;
        
        if (start_samp >= total_samples) continue;
        if (start_samp + dur_samp > total_samples) dur_samp = total_samples - start_samp;
        if (dur_samp == 0) continue;

        int f = fold_freq(note->freq);
        if (f == 0) continue;

        unsigned int phase = 0;
        unsigned int phase_inc = (unsigned int)(((unsigned long)f << 32) / SAMPLE_RATE_USER);

        size_t attack = 50;
        size_t release = 50;
        if (attack + release > dur_samp) { attack = dur_samp / 2; release = dur_samp / 2; }

        for (size_t i = 0; i < dur_samp; i++) {
            int sample = 0;
            unsigned char hi = phase >> 24;
            switch (wave) {
                case 0: sample = g_sine[hi]; break;
                case 1: sample = (hi < 128) ? 127 : -127; break;
                case 2: sample = ((int)hi - 128) * 2; break;
                case 3: {
                    int p = hi;
                    if (p < 128) sample = p * 2 - 128;
                    else         sample = (255 - p) * 2 - 128;
                    sample *= 2;
                    break;
                }
            }
            phase += phase_inc;

            long env = 127;
            if (i < attack) env = (i * 127) / attack;
            else if (i > dur_samp - release) env = ((dur_samp - i) * 127) / release;

            int val = (sample * amp * env) / (127 * 127 * 2);
            size_t out_idx = start_samp + i;
            int mixed = buf[out_idx] + val;
            if (mixed < 0) mixed = 0;
            if (mixed > 255) mixed = 255;
            buf[out_idx] = (uint8_t)mixed;
        }
    }
    return total_samples;
}

int midi_to_freq(int midi) {
    static const int base_c[12] = { 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494 };
    int rel = midi - 60;
    int oct = rel / 12;
    int semi = rel % 12;
    if (semi < 0) { semi += 12; oct -= 1; }
    int f = base_c[semi];
    if (oct > 0) for (int i = 0; i < oct; i++) f *= 2;
    else if (oct < 0) for (int i = 0; i < -oct; i++) { if (f > 1) f /= 2; }
    return f;
}