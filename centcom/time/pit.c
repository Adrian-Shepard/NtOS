#include "pit.h"
#include "../core/io.h"
#include "../log/log.h"
#include "../driver/xhci.h"

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_BASE 1193182

static volatile uint64_t g_ticks = 0;
static volatile int g_usb_ready = 0;

void pit_init(uint32_t hz) {
    uint32_t div = PIT_BASE / hz;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));
    g_ticks = 0;
    logi(S_TMR, "PIT configured at %d Hz (divisor=%d)", hz, div);
}

uint64_t pit_ticks(void) { return g_ticks; }

void pit_on_tick(void) {
    g_ticks++;

    // Отладка: выводим каждые 10 тиков (100 мс), но только первые 5 раз
    if (g_ticks % 10 == 0) {
        static int debug_count = 0;
        if (debug_count < 5) {
            logi(S_DRV, "[PIT] tick %d, usb_ready=%d", (int)g_ticks, g_usb_ready);
            debug_count++;
        }
        if (g_usb_ready) {
            xhci_poll();
        }
    }
}

void pit_enable_usb_polling(void) {
    g_usb_ready = 1;
    logi(S_DRV, "[PIT] USB polling ENABLED!");
}