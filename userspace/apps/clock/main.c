#include <unistd.h>
#include <gui.h>

/* «Clock» — пользовательское GUI-приложение с живым обновлением окна.
   Демонстрирует, что приложения получают собственные окна через syscall'ы,
   а НЕ через код ядра. Загружается из /bin/clock.elf в рантайме. */

#define BG    0x00070B12
#define PANEL 0x00222830
#define FG    0x00E0E0E0
#define ACC   0x0044DDFF
#define GREEN 0x0044FF88

#define WIN_W 280
#define WIN_H 150

static void rect(int id, int x, int y, int w, int h, unsigned c) {
    gui_rect_cmd_t r;
    r.x = x; r.y = y; r.w = w; r.h = h; r.color = c;
    gui_win_rect(id, &r);
}

static void text(int id, int x, int y, const char *s, unsigned fg, unsigned bg) {
    gui_text_cmd_t t;
    t.x = x; t.y = y; t.text = s; t.fg = fg; t.bg = bg;
    gui_win_text(id, &t);
}

static void itoa(unsigned v, char *out) {
    char tmp[16];
    int n = 0;
    if (!v) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) *out++ = tmp[n];
    *out = 0;
}

int main(void) {
    int id = gui_win_create("Clock", WIN_W, WIN_H);
    if (id < 0) return 1;

    unsigned last_sec = 0;
    for (;;) {
        gui_event_t ev;
        int r = gui_win_event(id, &ev);
        if (r < 0) break;                      /* окно закрыто WM */
        if (r == 1 && (ev.type == GUI_EV_CLOSE || ev.type == GUI_EV_KEY)) break;

        unsigned ticks = (unsigned)get_ticks();
        unsigned sec = ticks / 100;
        if (sec != last_sec) {
            last_sec = sec;

            rect(id, 0, 0, WIN_W, WIN_H, BG);
            rect(id, 0, 0, WIN_W, 44, PANEL);
            text(id, 12, 12, "SYSTEM UPTIME", GREEN, PANEL);

            unsigned hh = sec / 3600, mm = (sec / 60) % 60, ss = sec % 60;
            char b[8];
            char t[16];
            int p = 0;
            itoa(hh, b); for (char *q = b; *q; q++) t[p++] = *q; t[p++] = ':';
            itoa(mm, b); for (char *q = b; *q; q++) t[p++] = *q; t[p++] = ':';
            itoa(ss, b); for (char *q = b; *q; q++) t[p++] = *q; t[p] = 0;

            text(id, 12, 66, t, ACC, BG);

            text(id, 12, 110, "PIT ticks: ", FG, BG);
            itoa(ticks, b);
            text(id, 96, 110, b, FG, BG);
        }
        sleep(200);
    }

    gui_win_close(id);
    return 0;
}
