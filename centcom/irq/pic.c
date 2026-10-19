#include "pic.h"
#include "../core/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static inline void io_wait(void) { outb(0x80, 0); }

void pic_init(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    // PIC1: Разрешаем IRQ0 (PIT), IRQ1 (KBD), IRQ2 (Cascade), IRQ5 (SB16)
    // 1100 1000 = 0xC8
    outb(PIC1_DATA, 0xC8);
    
    // PIC2: Разрешаем только IRQ12 (Mouse)
    // 1110 1111 = 0xEF
    outb(PIC2_DATA, 0xEF);
}

void pic_send_eoi(unsigned char irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}