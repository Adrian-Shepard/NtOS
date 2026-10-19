#include "mouse.h"
#include "../core/io.h"
#include "../log/log.h"
#include "../tty/fb.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

static int g_mouse_x = 400;
static int g_mouse_y = 300;
static int g_left = 0;
static int g_right = 0;
static int g_middle = 0;

static uint8_t g_packet[3];
static int g_cycle = 0;

// Аккумуляторы для плавности
static int acc_x = 0;
static int acc_y = 0;

static void mouse_wait(uint8_t a_type) {
    uint32_t timeout = 100000;
    if (a_type == 0) {
        while (timeout--) if ((inb(PS2_STATUS) & 0x01) == 1) return;
    } else {
        while (timeout--) if ((inb(PS2_STATUS) & 0x02) == 0) return;
    }
}

static void mouse_write(uint8_t a_write) {
    mouse_wait(1);
    outb(PS2_COMMAND, 0xD4);
    mouse_wait(1);
    outb(PS2_DATA, a_write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(PS2_DATA);
}

void mouse_init(void) {
    uint8_t status;
    
    for (int i = 0; i < 32; i++) {
        if (inb(0x64) & 0x01) (void)inb(0x60);
        else break;
    }

    mouse_wait(1);
    outb(PS2_COMMAND, 0xA8);

    mouse_wait(1);
    outb(PS2_COMMAND, 0x20);
    mouse_wait(0);
    status = inb(PS2_DATA);
    
    status |= 0x02;
    status &= ~0x10;
    status |= 0x40;
    
    mouse_wait(1);
    outb(PS2_COMMAND, 0x60);
    mouse_wait(1);
    outb(PS2_DATA, status);

    mouse_write(0xF6); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(100); mouse_read(); // 100Hz
    mouse_write(0xE8); mouse_read(); mouse_write(0x03); mouse_read(); // 8 counts/mm
    mouse_write(0xF4); mouse_read();

    g_mouse_x = fb_width() / 2;
    g_mouse_y = fb_height() / 2;
    acc_x = g_mouse_x;
    acc_y = g_mouse_y;

    logi(S_KBD, "PS/2 Mouse initialized (sensitivity limited)");
}

void mouse_irq(regs_t *r) {
    (void)r;
    uint8_t status = inb(PS2_STATUS);
    if (!(status & 0x01)) return;
    
    uint8_t data = inb(PS2_DATA);
    if (!(status & 0x20)) return;

    if (g_cycle == 0 && !(data & 0x08)) return; 

    g_packet[g_cycle] = data;
    g_cycle++;

    if (g_cycle == 3) {
        g_cycle = 0;

        uint8_t flags = g_packet[0];
        if (flags & 0xC0) return;

        int dx = g_packet[1];
        int dy = g_packet[2];

        if (flags & 0x10) dx |= 0xFFFFFF00;
        if (flags & 0x20) dy |= 0xFFFFFF00;

        // Накопление дробных значений для плавности (делим на 3)
        acc_x += dx;
        acc_y += dy;

        g_mouse_x = acc_x / 3;
        g_mouse_y -= (acc_y / 3); // Y инвертирован
        acc_y -= (acc_y / 3) * 3; // Остаток сохраняем

        if (g_mouse_x < 0) g_mouse_x = 0;
        if (g_mouse_y < 0) g_mouse_y = 0;
        if (g_mouse_x >= (int)fb_width()) g_mouse_x = fb_width() - 1;
        if (g_mouse_y >= (int)fb_height()) g_mouse_y = fb_height() - 1;

        g_left   = (flags & 0x01) ? 1 : 0;
        g_right  = (flags & 0x02) ? 1 : 0;
        g_middle = (flags & 0x04) ? 1 : 0;
    }
}

mouse_state_t mouse_get_state(void) {
    mouse_state_t st;
    st.x = g_mouse_x;
    st.y = g_mouse_y;
    st.left = g_left;
    st.right = g_right;
    st.middle = g_middle;
    return st;
}