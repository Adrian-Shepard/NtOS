#include "sb16.h"
#include "dma.h"
#include "../core/io.h"
#include "../log/log.h"
#include "../log/serial.h"
#include "../time/pit.h"
#include "../mem/pmm.h"

#define SB16_BASE    0x220
#define SB16_RESET   (SB16_BASE + 0x6)
#define SB16_READ    (SB16_BASE + 0xA)
#define SB16_WRITE   (SB16_BASE + 0xC)
#define SB16_RSTATUS (SB16_BASE + 0xE)
#define SB16_WSTATUS (SB16_BASE + 0xC)
#define SB16_MIXADDR (SB16_BASE + 0x4)
#define SB16_MIXDATA (SB16_BASE + 0x5)

#define DMA_BUF_PHYS 0x800000
#define DMA_BUF_SIZE 65536

static uint8_t *const g_buf = (uint8_t*)DMA_BUF_PHYS;
static int g_ok = 0;
static volatile int g_dma_playing = 0;
static volatile uint64_t g_dma_start_tick = 0;
static volatile uint64_t g_dma_max_ticks = 0;

#define SAMPLE_RATE 22050

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

static void dbg(const char *tag) {
    serial_puts("[sb16] ");
    serial_puts(tag);
    serial_puts(" t=");
    serial_putdec(pit_ticks());
    serial_puts("\n");
}

static int wait_read_short(void) {
    for (int i = 0; i < 10000; i++)
        if (inb(SB16_RSTATUS) & 0x80) return 1;
    return 0;
}

static void sb_write(uint8_t v) { outb(SB16_WRITE, v); }

void sb16_poll(void) {
    if (!g_dma_playing) return;
    uint64_t now = pit_ticks();
    // ИСПРАВЛЕНО: >= вместо >, чтобы карта корректно сбрасывала флаг занятости
    if (g_dma_max_ticks && (now - g_dma_start_tick) >= g_dma_max_ticks) {
        g_dma_playing = 0;
    }
}

int sb16_init(void) {
    dbg("init: start");
    outb(SB16_RESET, 1);
    for (volatile int i = 0; i < 100000; i++);
    outb(SB16_RESET, 0);

    if (!wait_read_short()) { loge(S_DRV, "sb16: reset timeout"); return 0; }
    uint8_t ack = inb(SB16_READ);
    if (ack != 0xAA) { loge(S_DRV, "sb16: bad ack %x", ack); return 0; }

    logi(S_DRV, "SB16 DSP ready (ack=0x%x)", ack);
    g_ok = 1;

    pmm_reserve(DMA_BUF_PHYS, DMA_BUF_SIZE);
    sb16_set_volume(30);

    dbg("init: setting sample rate");
    sb_write(0x41);
    sb_write((uint8_t)(SAMPLE_RATE >> 8));
    sb_write((uint8_t)(SAMPLE_RATE & 0xFF));
    dbg("init: done");

    return 1;
}

void sb16_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t v = (uint8_t)((percent * 15 + 50) / 100);
    outb(SB16_MIXADDR, 0x22);
    outb(SB16_MIXDATA, (uint8_t)((v << 4) | v));
}

int sb16_is_playing(void) {
    sb16_poll();
    return g_dma_playing;
}

void sb16_stop(void) {
    dma_mask_channel(1);
    g_dma_playing = 0;

    outb(SB16_RESET, 1);
    for (volatile int i = 0; i < 100000; i++);
    outb(SB16_RESET, 0);

    if (wait_read_short()) {
        inb(SB16_READ);
        sb_write(0x41);
        sb_write((uint8_t)(SAMPLE_RATE >> 8));
        sb_write((uint8_t)(SAMPLE_RATE & 0xFF));
    }
}

int sb16_play_tone_async(int freq_hz, int ms) {
    if (!g_ok) return 0;
    sb16_poll();
    if (g_dma_playing) return 0;

    if (freq_hz < 20)    freq_hz = 20;
    if (freq_hz > 10000) freq_hz = 10000;
    if (ms < 10)   ms = 10;
    if (ms > 3000) ms = 3000;

    int samples = (SAMPLE_RATE * ms) / 1000;
    if (samples > DMA_BUF_SIZE) samples = DMA_BUF_SIZE;

    for (int i = 0; i < samples; i++) {
        int phase = (int)(((int64_t)i * freq_hz * 256) / SAMPLE_RATE) & 0xFF;
        int s = g_sine[phase];
        int env = 128;
        if (i < 100)                env = (i * 128) / 100;
        else if (i > samples - 100) env = ((samples - i) * 128) / 100;
        int v = 128 + (s * env / 128);
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        g_buf[i] = (uint8_t)v;
    }

    dma_setup_8bit(1, DMA_BUF_PHYS, (uint16_t)samples, 0x49);

    sb_write(0x14);
    sb_write((uint8_t)((samples - 1) & 0xFF));
    sb_write((uint8_t)(((samples - 1) >> 8) & 0xFF));

    dma_unmask_channel(1);

    g_dma_start_tick = pit_ticks();
    g_dma_max_ticks  = (samples / 220) + 20;
    g_dma_playing    = 1;

    return 1;
}

int sb16_play_pcm_async(const uint8_t *pcm, uint32_t samples) {
    if (!g_ok) return 0;
    sb16_poll();
    if (g_dma_playing) return 0;
    if (samples == 0) return 0;
    if (samples > DMA_BUF_SIZE) samples = DMA_BUF_SIZE;

    for (uint32_t i = 0; i < samples; i++) {
        g_buf[i] = pcm[i];
    }

    dma_setup_8bit(1, DMA_BUF_PHYS, (uint16_t)samples, 0x49);

    sb_write(0x14);
    sb_write((uint8_t)((samples - 1) & 0xFF));
    sb_write((uint8_t)(((samples - 1) >> 8) & 0xFF));

    dma_unmask_channel(1);

    g_dma_start_tick = pit_ticks();
    g_dma_max_ticks  = (samples / 220) + 20; 
    g_dma_playing    = 1;

    return 1;
}

void sb16_play_tone(int freq_hz, int ms) {
    if (!sb16_play_tone_async(freq_hz, ms)) return;
    uint64_t start = pit_ticks();
    uint64_t timeout = (uint64_t)(ms / 10) + 20;
    while (sb16_is_playing() && pit_ticks() - start < timeout);
    g_dma_playing = 0;
}