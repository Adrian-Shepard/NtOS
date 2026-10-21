#include "keyboard.h"
#include "../core/io.h"
#include "../log/log.h"
#include "../log/serial.h"

#define KBD_BUF_SIZE 256
static volatile char     g_buf[KBD_BUF_SIZE];
static volatile uint32_t g_head = 0;
static volatile uint32_t g_tail = 0;

static const char kbd_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0, ' ',
};

static const char kbd_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0, ' ',
};

static int g_shift = 0;

static void kbd_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 0x02)) return;
}
static void kbd_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 0x01) return;
}

void keyboard_init(void) {
    g_head = g_tail = 0;
    g_shift = 0;
    for (int i = 0; i < 32; i++) {
        if (inb(0x64) & 0x01) (void)inb(0x60);
        else break;
    }
    kbd_wait_write(); outb(0x64, 0xAE);
    kbd_wait_write(); outb(0x60, 0xF4);
    kbd_wait_read();
    if (inb(0x64) & 0x01) (void)inb(0x60);
    logi(S_KBD, "PS/2 keyboard initialized");
}

void keyboard_irq(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x01)) return;
    uint8_t sc = inb(0x60);
    
    if (status & 0x20) return; // Данные от мыши — игнорируем

    if (sc == 0x2A || sc == 0x36) { g_shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { g_shift = 0; return; }
    if (sc & 0x80) return;
    if (sc >= 128) return;

    char c = g_shift ? kbd_shift[sc] : kbd_map[sc];
    if (!c) return;

    uint32_t next = (g_head + 1) % KBD_BUF_SIZE;
    if (next != g_tail) {
        g_buf[g_head] = c;
        g_head = next;
    }
}

int keyboard_getchar(void) {
    if (g_head == g_tail) return 0;
    char c = g_buf[g_tail];
    g_tail = (g_tail + 1) % KBD_BUF_SIZE;
    return (unsigned char)c;
}

int keyboard_has_char(void) {
    return g_head != g_tail;
}