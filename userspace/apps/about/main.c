#include <unistd.h>
#include <gui.h>

/* Окно "About" — демонстрация GUI-приложения, работающего полностью
 * в пользовательском режиме. Загружается из /bin/about.elf в рантайме. */

#define BG     0x00141A20
#define PANEL  0x00222830
#define ACCENT 0x00C8102E
#define FG     0x00E0E0E0
#define MUTED  0x00808080
#define GREEN  0x0044FF88
#define CYAN   0x0044DDFF

#define WIN_W 400
#define WIN_H 280

static void rect(int id, int x, int y, int w, int h, unsigned color) {
    gui_rect_cmd_t c;
    c.x = x; c.y = y; c.w = w; c.h = h; c.color = color;
    gui_win_rect(id, &c);
}

static void text(int id, int x, int y, const char *s, unsigned fg, unsigned bg) {
    gui_text_cmd_t c;
    c.x = x; c.y = y; c.text = s; c.fg = fg; c.bg = bg;
    gui_win_text(id, &c);
}

static void draw_all(int id) {
    rect(id, 0, 0, WIN_W, WIN_H, BG);          /* фон */
    rect(id, 0, 0, WIN_W, 48, PANEL);          /* шапка окна */

    text(id, 16, 12, "NtOS", ACCENT, PANEL);
    text(id, 64, 12, "CENTCOM Desktop", FG, PANEL);

    text(id, 16, 60, "This window is a user-space ELF program.", FG, BG);
    text(id, 16, 76, "Loaded at runtime from /bin/about.elf -", FG, BG);
    text(id, 16, 92, "no kernel rebuild required.", FG, BG);

    text(id, 16, 124, "Filesystem layout:", GREEN, BG);
    text(id, 16, 140, "  /bin    executables (ELF)", MUTED, BG);
    text(id, 16, 156, "  /etc    configuration", MUTED, BG);
    text(id, 16, 172, "  /home   user files", MUTED, BG);

    text(id, 16, 208, "Click [X] or press any key to close.", CYAN, BG);
}

int main(void) {
    int id = gui_win_create("About NtOS", WIN_W, WIN_H);
    if (id < 0) return 1;

    draw_all(id);

    for (;;) {
        gui_event_t ev;
        int r = gui_win_event(id, &ev);
        if (r < 0) break;                 /* окно закрыто системой */
        if (r == 1) {
            if (ev.type == GUI_EV_CLOSE) break;
            if (ev.type == GUI_EV_KEY)   break;
            if (ev.type == GUI_EV_CLICK) break;
        }
        sleep(20);
    }

    gui_win_close(id);
    return 0;
}
