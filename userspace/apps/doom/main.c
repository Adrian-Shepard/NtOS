#include <gui.h>

extern void doomgeneric_Create(int argc, char **argv);
extern void doom_platform_set_window(int id);

int main(void) {
    int win = gui_win_create("DOOM", 640, 424);   /* 400 + 24px заголовок */
    if (win < 0) return 1;
    doom_platform_set_window(win);

    /* Показываем статус, пока Doom грузит IWAD и инициализируется */
    gui_text_cmd_t st = { 8, 28, "Loading DOOM...", 0x00E2E8F0, 0x000B0E14 };
    gui_win_text(win, &st);

    /* IWAD теперь лежит в initrd в каталоге /share */
    char *argv[] = { "doom", "-iwad", "share/doom1.wad", 0 };
    doomgeneric_Create(3, argv);

    /* Игра завершилась (quit) — закрываем окно и выходим. */
    gui_win_close(win);
    return 0;
}
