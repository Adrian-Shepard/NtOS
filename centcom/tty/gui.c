#include "gui.h"
#include "fb.h"
#include "../driver/keyboard.h"
#include "../driver/mouse.h"
#include "../time/pit.h"
#include "../core/io.h" // для outb

#define GUI_BG_DESKTOP 0x000A0A0C
#define GUI_BAR_BG     0x00141A20
#define GUI_BAR_ACCENT 0x00C8102E
#define GUI_WIN_BG     0x00141A20
#define GUI_WIN_BORDER 0x00AA2222
#define GUI_WIN_TITLE  0x00AA2222
#define GUI_TEXT       0x00D0D0D0
#define GUI_TEXT_MUTED 0x00808080
#define GUI_WHITE      0x00FFFFFF
#define GUI_RED        0x00FF3333
#define GUI_GREEN      0x0044FF88
#define GUI_CYAN       0x0044DDFF

static const uint8_t cursor_bitmap[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,1,1,1,1,1,0,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0}
};

static uint32_t saved_bg[16][16];
static int last_mx = -100, last_my = -100;

static void put_str(const char *s, uint32_t fg, uint32_t bg) {
    while (*s) fb_putc(*s++, fg, bg);
}

static void draw_top_bar(void) {
    uint32_t w = fb_width();
    fb_fill_rect(0, 0, w, 24, GUI_BAR_BG);
    fb_fill_rect(0, 24, w, 2, GUI_BAR_ACCENT);
    
    fb_set_cursor(16, 8);
    put_str("NANOTRASEN", GUI_BAR_ACCENT, GUI_BAR_BG);
    put_str(" // SECURE", GUI_TEXT_MUTED, GUI_BAR_BG);
    
    fb_set_cursor(w - 100, 8);
    put_str("UPTIME: ", GUI_TEXT_MUTED, GUI_BAR_BG);
    uint64_t sec = pit_ticks() / 100;
    char time_str[16];
    time_str[0] = '0' + (sec / 100) % 10;
    time_str[1] = '0' + (sec / 10) % 10;
    time_str[2] = '0' + (sec % 10);
    time_str[3] = 's';
    time_str[4] = 0;
    put_str(time_str, GUI_GREEN, GUI_BAR_BG);
}

static void draw_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *title) {
    fb_fill_rect(x + 4, y + 4, w, h, 0x00000000);
    fb_fill_rect(x, y, w, h, GUI_WIN_BG);
    fb_draw_box(x, y, w, h, GUI_WIN_BORDER);
    fb_fill_rect(x + 1, y + 1, w - 2, 20, GUI_WIN_TITLE);
    
    fb_set_cursor(x + 8, y + 6);
    put_str(title, GUI_WHITE, GUI_WIN_TITLE);
    
    fb_fill_rect(x + w - 20, y + 4, 16, 14, GUI_RED);
    fb_set_cursor(x + w - 16, y + 8);
    put_str("X", GUI_WHITE, GUI_RED);

    fb_set_cursor(x + 16, y + 40);
    put_str("SYSTEM DIAGNOSTICS", GUI_CYAN, GUI_WIN_BG);
    fb_fill_rect(x + 16, y + 56, w - 32, 1, GUI_TEXT_MUTED);
    
    fb_set_cursor(x + 16, y + 72);
    put_str("Kernel: ", GUI_TEXT, GUI_WIN_BG); put_str("CENTCOM v0.15.2", GUI_GREEN, GUI_WIN_BG);
    fb_set_cursor(x + 16, y + 88);
    put_str("Status: ", GUI_TEXT, GUI_WIN_BG); put_str("OPERATIONAL", GUI_GREEN, GUI_WIN_BG);
    fb_set_cursor(x + 16, y + 104);
    put_str("Graphics: ", GUI_TEXT, GUI_WIN_BG); put_str("Framebuffer (32bpp)", GUI_TEXT_MUTED, GUI_WIN_BG);
    fb_set_cursor(x + 16, y + 120);
    put_str("Memory: ", GUI_TEXT, GUI_WIN_BG); put_str("ACTIVE", GUI_GREEN, GUI_WIN_BG);
}

static void restore_cursor_bg(void) {
    if (last_mx == -100) return;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint32_t *ptr = (uint32_t*)fb_get_ptr(last_mx + x, last_my + y);
            if (ptr) *ptr = saved_bg[y][x];
        }
    }
}

static void draw_cursor(int mx, int my) {
    if (mx < 0 || my < 0 || mx + 16 >= (int)fb_width() || my + 16 >= (int)fb_height()) return;
    
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint32_t *ptr = (uint32_t*)fb_get_ptr(mx + x, my + y);
            if (!ptr) continue;
            
            saved_bg[y][x] = *ptr;
            
            if (cursor_bitmap[y][x] == 1) *ptr = 0x00000000;
            else if (cursor_bitmap[y][x] == 2) *ptr = 0x00FFFFFF;
        }
    }
    last_mx = mx;
    last_my = my;
}

void gui_demo(void) {
    fb_clear(GUI_BG_DESKTOP);
    draw_top_bar();
    
    uint32_t w = fb_width();
    uint32_t h = fb_height();
    
    uint32_t win_w = 400, win_h = 250;
    int win_x = (w - win_w) / 2;
    int win_y = (h - win_h) / 2 + 20;
    
    draw_window(win_x, win_y, win_w, win_h, "CENTCOM SYSTEM MONITOR");
    fb_set_cursor(w / 2 - 100, h - 40);
    put_str("Press [ESC] or [Q] to exit GUI", GUI_TEXT_MUTED, GUI_BG_DESKTOP);

    last_mx = -100;
    int last_left = 0;

    for (;;) {
        int c = keyboard_getchar();
        if (c == 27 || c == 'q' || c == 'Q') break;

        mouse_state_t m = mouse_get_state();
        
        if (m.x != last_mx || m.y != last_my) {
            __asm__ volatile("cli"); 
            restore_cursor_bg();
            draw_cursor(m.x, m.y);
            __asm__ volatile("sti");
        }

        if (m.left && !last_left) {
            int click_x = win_x + win_w - 20;
            int click_y = win_y + 4;
            if (m.x >= click_x && m.x <= click_x + 16 &&
                m.y >= click_y && m.y <= click_y + 14) {
                break;
            }
        }
        last_left = m.left;

        // Инструкция pause снижает нагрузку на CPU,
        // но позволяет мгновенно проснуться при прерывании от мыши
        __asm__ volatile("pause");
    }

    __asm__ volatile("cli");
    restore_cursor_bg();
    fb_clear(0x00101018);
    fb_set_cursor(0, 0);
    __asm__ volatile("sti");
}