#include "pit.h"
#include "../core/io.h"
#include "../log/log.h"

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_BASE 1193182

static volatile uint64_t g_ticks = 0;

void pit_init(uint32_t hz) {
    uint32_t div = PIT_BASE / hz;
    outb(PIT_CMD, 0x36);            /* channel 0, lobyte/hibyte, mode 3 */
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));
    g_ticks = 0;
    logi(S_TMR, "PIT configured at %d Hz (divisor=%d)", hz, div);
}

uint64_t pit_ticks(void) { return g_ticks; }
void pit_on_tick(void)    { g_ticks++; }