#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "synth_user.h"

#define MAX_OUT_SIZE 8192

static uint8_t g_midi_buf[16384];
static char g_out_buf[MAX_OUT_SIZE];

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

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: midi2ntsad <in.mid> <out.ntsad>\n"); return 1; }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { printf("error: open failed\n"); return 1; }
    long n = read(fd, g_midi_buf, sizeof(g_midi_buf));
    close(fd);

    const uint8_t *p = g_midi_buf;
    const uint8_t *end = g_midi_buf + n;
    if (p + 14 > end || memcmp(p, "MThd", 4) != 0) { printf("bad MIDI\n"); return 1; }
    
    p += 4;
    uint32_t hdr_len = read_u32(&p);
    uint16_t division = read_u16(&p);
    if (division == 0) division = 96;
    p += (hdr_len - 6);

    while (p + 8 < end && memcmp(p, "MTrk", 4) != 0) p++;
    if (p + 8 >= end) { printf("no track\n"); return 1; }
    p += 4;
    uint32_t trk_len = read_u32(&p);
    const uint8_t *trk_end = p + trk_len;
    if (trk_end > end) trk_end = end;

    uint32_t tick = 0;
    uint32_t us_per_tick = 500000 / division;
    uint8_t running_status = 0;

    int active_notes[128]; // храним тик, когда нота включилась
    for (int i = 0; i < 128; i++) active_notes[i] = -1;

    int out_pos = 0;
    out_pos += sprintf(g_out_buf + out_pos, "# NTSAD v2 (chords)\n");
    int note_count = 0;

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
                    uint32_t dur_ms = ((tick - active_notes[note]) * us_per_tick) / 1000;
                    if (dur_ms < 20) dur_ms = 20; // минимум 20 мс

                    int f = midi_to_freq(note);
                    out_pos += sprintf(g_out_buf + out_pos, "%d %d\n", f, dur_ms);
                    active_notes[note] = -1;
                    note_count++;
                }
            }
        }
    }

    if (note_count == 0) { printf("no notes\n"); return 1; }

    printf("writing %d bytes, %d notes...\n", out_pos, note_count);
    if (ntrfs_write(argv[2], g_out_buf, out_pos) > 0) {
        printf("ok: done\n");
    } else printf("write failed\n");

    return 0;
}