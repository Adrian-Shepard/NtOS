#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "synth_user.h"

#define MAX_NOTES 2048
#define MAX_PCM_SIZE (1024 * 1024) // 1 MB ~ 47 секунд аудио

typedef struct {
    int start_ms;
    int duration_ms;
    int freq;
} note_t;

static uint8_t g_midi_buf[65536];
static uint8_t g_pcm[MAX_PCM_SIZE];
static note_t g_notes[MAX_NOTES];
static int g_note_count = 0;
static int g_song_len_ms = 0;

static uint32_t read_vlq(const uint8_t **p, const uint8_t *end) {
    uint32_t v = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}
static uint32_t read_u32(const uint8_t **p) {
    uint32_t v = ((*p)[0] << 24) | ((*p)[1] << 16) | ((*p)[2] << 8) | (*p)[3];
    *p += 4; return v;
}
static uint16_t read_u16(const uint8_t **p) {
    uint16_t v = ((*p)[0] << 8) | (*p)[1];
    *p += 2; return v;
}

static int parse_midi(const uint8_t *data, long size) {
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    if (p + 14 > end || memcmp(p, "MThd", 4) != 0) return 0;
    
    p += 4;
    uint32_t hdr_len = read_u32(&p);
    uint16_t division = read_u16(&p);
    if (division == 0) division = 96;
    p += (hdr_len - 6);

    while (p + 8 < end && memcmp(p, "MTrk", 4) != 0) p++;
    if (p + 8 >= end) return 0;
    p += 4;
    uint32_t trk_len = read_u32(&p);
    const uint8_t *trk_end = p + trk_len;
    if (trk_end > end) trk_end = end;

    uint32_t tick = 0;
    uint32_t us_per_tick = 500000 / division;
    uint8_t running_status = 0;
    int active_notes[128];
    for (int i = 0; i < 128; i++) active_notes[i] = -1;

    while (p < trk_end) {
        tick += read_vlq(&p, trk_end);
        if (p >= trk_end) break;

        uint8_t status = *p++;
        if (status < 0x80) { p--; status = running_status; }
        else if (status < 0xF0) { running_status = status; }

        if (status == 0xFF) {
            uint8_t type = *p++;
            uint32_t len = read_vlq(&p, trk_end);
            if (type == 0x51 && len == 3) {
                uint32_t tempo = (p[0] << 16) | (p[1] << 8) | p[2];
                us_per_tick = tempo / division;
                if (us_per_tick == 0) us_per_tick = 1;
            }
            p += len;
        } else if (status == 0xF0 || status == 0xF7) {
            p += read_vlq(&p, trk_end);
        } else {
            uint8_t event = status & 0xF0;
            uint8_t note = *p++;
            uint8_t vel = 0;
            if (event != 0xC0 && event != 0xD0) vel = *p++;

            if (event == 0x90 && vel > 0) {
                active_notes[note] = tick;
            } else if (event == 0x80 || (event == 0x90 && vel == 0)) {
                if (active_notes[note] >= 0) {
                    uint32_t dur = ((tick - active_notes[note]) * us_per_tick) / 1000;
                    uint32_t start = (active_notes[note] * us_per_tick) / 1000;
                    if (dur < 50) dur = 50;

                    if (g_note_count < MAX_NOTES) {
                        g_notes[g_note_count].start_ms = start;
                        g_notes[g_note_count].duration_ms = dur;
                        g_notes[g_note_count].freq = midi_to_freq(note);
                        g_note_count++;
                        
                        int end_ms = start + dur;
                        if (end_ms > g_song_len_ms) g_song_len_ms = end_ms;
                    }
                    active_notes[note] = -1;
                }
            }
        }
    }
    return g_note_count > 0;
}

static void play_thread(void) {
    printf("Generating %d ms of audio (%d notes)...\n", g_song_len_ms, g_note_count);
    
    size_t n = synth_track(g_pcm, MAX_PCM_SIZE, (synth_note_t*)g_notes, g_note_count, g_song_len_ms, 0, 80);
    if (n == 0) {
        printf("synth_track failed\n");
        _exit(1);
    }

    printf("Playing %d samples...\n", (int)n);
    int sent = 0;
    while (sent < (int)n) {
        int chunk = n - sent;
        if (chunk > 32768) chunk = 32768;
        
        play_pcm(g_pcm + sent, chunk);
        
        // Ждем, пока звук проиграется (с запасом 50 мс)
        int ms = (chunk * 1000) / SAMPLE_RATE_USER;
        sleep(ms + 200); // Даем звуковой карте гарантированно доиграть кусок
        sent += chunk;
    }
    printf("Done.\n");
    _exit(0);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: midiplay <in.mid>\n");
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { printf("error: cannot open %s\n", argv[1]); return 1; }
    long n = read(fd, g_midi_buf, sizeof(g_midi_buf));
    close(fd);

    if (!parse_midi(g_midi_buf, n)) {
        printf("error: bad midi or no notes\n");
        return 1;
    }

    printf("MIDI parsed: %d notes, %d ms total.\n", g_note_count, g_song_len_ms);

    int pid = fork();
    if (pid == 0) {
        play_thread(); // Дочерний процесс: генерирует и играет
    } else if (pid > 0) {
        printf("Playing in background (PID=%d)...\n", pid);
        int status = 0;
        sys_wait(pid, &status);
        printf("Playback finished.\n");
    } else {
        printf("fork failed!\n");
    }

    return 0;
}