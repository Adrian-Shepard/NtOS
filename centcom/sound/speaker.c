#include "speaker.h"
#include "../core/io.h"
#include "../log/log.h"

#define PIT_BASE 1193182

static int g_on = 0;

void speaker_init(void) {
    uint8_t t = inb(0x61);
    outb(0x61, t & 0xFC);
    g_on = 0;
    logi(S_DRV, "PC speaker ready");
}

void speaker_on(uint32_t freq_hz) {
    if (freq_hz == 0) { speaker_off(); return; }

    uint32_t div = PIT_BASE / freq_hz;
    if (div < 1) div = 1;
    if (div > 0xFFFF) div = 0xFFFF;

    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));

    uint8_t t = inb(0x61);
    if ((t & 3) != 3) outb(0x61, t | 3);

    g_on = 1;
}

void speaker_off(void) {
    uint8_t t = inb(0x61);
    outb(0x61, t & 0xFC);
    g_on = 0;
}

/* блокирующий beep — для использования без IRQ */
void speaker_beep(uint32_t freq_hz, uint32_t ms) {
    if (freq_hz == 0 || ms == 0) return;

    speaker_on(freq_hz);

    /* длительность тона: 1 мс ≈ 30 000 итераций pause */
    for (volatile uint32_t i = 0; i < ms * 30000; i++)
        __asm__ volatile("pause");

    speaker_off();

    /* пауза после бипа: минимум 5 мс + 30% от длительности */
    uint32_t pause_iters = (5 + ms / 3) * 30000;
    for (volatile uint32_t i = 0; i < pause_iters; i++)
        __asm__ volatile("pause");
}

int speaker_is_on(void) { return g_on; }