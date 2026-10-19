#pragma once
#include <stdint.h>
#include <stddef.h>

#define SAMPLE_RATE_USER 22050
#define MAX_PCM_BUFFER   65536

typedef struct {
    int start_ms;
    int duration_ms;
    int freq;
} synth_note_t;

size_t synth_note(uint8_t *buf, size_t max_samples, int freq, int ms, int wave, int volume);
size_t synth_chord(uint8_t *buf, size_t max_samples, int *freqs, int count, int ms, int wave, int volume);
size_t synth_track(uint8_t *buf, size_t max_samples, synth_note_t *notes, int note_count, int total_ms, int wave, int volume);
int midi_to_freq(int midi);