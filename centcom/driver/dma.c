#include "dma.h"
#include "../core/io.h"

static inline void io_wait(void) { outb(0x80, 0); }

void dma_mask_channel(int ch) {
    outb(0x0A, 0x04 | (ch & 3));
    io_wait();
}

void dma_unmask_channel(int ch) {
    outb(0x0A, ch & 3);
    io_wait();
}

void dma_setup_8bit(int channel, uint32_t addr, uint16_t len, uint8_t mode) {
    dma_mask_channel(channel);

    outb(0x0C, 0); io_wait();           /* clear flip-flop */
    outb(0x0B, mode); io_wait();        /* mode register */

    uint16_t port = 0x00 + (channel & 3) * 2;
    outb(port,     addr & 0xFF); io_wait();
    outb(port,     (addr >> 8) & 0xFF); io_wait();

    uint8_t page = (addr >> 16) & 0xFF;
    switch (channel) {
        case 0: outb(0x87, page); break;
        case 1: outb(0x83, page); break;
        case 2: outb(0x81, page); break;
        case 3: outb(0x82, page); break;
    }
    io_wait();

    uint16_t count = len - 1;
    outb(port + 1, count & 0xFF); io_wait();
    outb(port + 1, (count >> 8) & 0xFF); io_wait();
}