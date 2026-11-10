# GUI: оконный менеджер и рабочий стол

GUI находится в `centcom/gui/` и работает в **главном потоке** ядра (pid 0).
Запускается из `centcom_boot` → `gui_start()` (возвращает 1 = выйти в shell).

## Модули

| Файл | Назначение |
|------|-----------|
| `gui.c` | главный цикл, отрисовка (верхняя/нижняя панели, градиент), ввод, drag |
| `desktop.c` | рабочий стол: чтение `desktop.cfg` + авто-обнаружение `bin/*.elf` |
| `uwm.c/.h` | **UWM** — окна пользовательских процессов |
| `apps.c/.h` | реестр встроенных приложений (terminal, taskman, editor) |
| `gui.h` | `window_t`, `desktop_shortcut_t`, `app_meta_t` |

Встроенные приложения (в ядре): `centcom/apps/terminal.c`, `taskman.c`, `editor.c`,
`doomgeneric_ntos.c` (Doom). Остальные приложения — **отдельные ELF** в userspace.

## Ключевые структуры

```c
#define MAX_WINDOWS 8
#define TITLE_BAR_H 24

struct window {
    int id, x, y, w, h;
    char title[64];
    int is_open;
    void (*draw_content)(window_t *);
    void (*on_click)(window_t *, int mx, int my);
    void (*on_key)(window_t *, int key);
};

typedef struct {
    const char *name, *exec_path, *icon_text;
    uint32_t icon_color;
} desktop_shortcut_t;
```

## Рабочий стол

- Ярлыки в сетке (4 колонки, иконки 48×48, подпись до 13 символов).
- **Одинарный клик** — выделение (синяя полупрозрачная подсветка).
- **Двойной клик** (в пределах 500 мс) — запуск.
- `KERNEL://name` → встроенное приложение; иначе → загрузка ELF из `ntmem_fs` через
  `process_create_user`.
- Список ярлыков обновляется вызовом `desktop_init()` (читает `/etc/desktop.cfg` +
  сканирует `bin/*.elf`).

## Встроенные окна (kernel-apps)

Создаются через `wm_create_window(title, x, y, w, h, draw, click, key)`.
Приложение = `app_meta_t { name, icon_text, icon_color, init, draw, click, key }`.
Зарегистрировано в `apps.c`; запуск — по имени в `desktop.cfg` (`KERNEL://...`).

## UWM — окна пользовательских процессов

Пользовательские ELF создают окна через syscall'ы `gui_win_*` (см. SYSCALLS.md).
Каждое окно имеет **pixel-буфер** (surface) в куче ядра:

- Приложение рисует в буфер (`gui_win_rect`, `gui_win_text`).
- WM рисует рамку/заголовок/кнопку «X» и блитит буфер (`fb_blit`) в back-buffer
  каждый кадр (при `uwm_has_windows()` — полная перерисовка каждые 10 тиков).
- Ввод: клик по окну → активация, клик в область → событие `GUI_EV_CLICK`; клавиши →
  `GUI_EV_KEY`; кнопка «X» → закрытие. Приложение опрашивает `gui_win_event`.

При выходе процесса GUI-цикл вызывает `uwm_close_by_pid(pid)` и `process_reap`.

## Кадровый буфер (tty/fb.c)

- Двойная буферизация: всё рисуется в `g_back_buffer` (GUI-буфер `g_gui_buf`), затем
  `fb_swap()` копирует в видеопамять (`rep movsq`).
- `fb_swap_rect` — частичное обновление.
- Буфер GUI аллоцируется размером `fb_pitch_px() * fb_height() * 4` (по pitch!).
- `fb_blit(x,y,w,h,src)` — копирование пиксельного буфера окна в back-buffer.

## Ввод

- Клавиатура: `keyboard_getchar()` (PS/2 + USB объединены). `ESC` — выход из GUI.
- Мышь: `mouse_get_state()` → `{x, y, left}`. Drag окна за заголовок.
- Курсор рисуется/восстанавливается через `draw_cursor`/`restore_cursor_bg`.

## Звуки GUI

- клик по ярлыку → `CUE_CLICK`, запуск → `CUE_NOTIFY`, открытие окна → `CUE_UI_REVEAL`,
  закрытие → `CUE_UI_SOFT` (через PC-спикер, см. `sound/cue.c`).

## Как добавить встроенное GUI-приложение

1. Создай `centcom/apps/<name>.c` с функциями `app_<name>_init/draw/key/click`.
2. Добавь `app_meta_t app_<name>` в `centcom/gui/apps.c` + extern в `apps.h`.
3. Добавь объект в `COBJS` (Makefile).
4. Добавь ярлык в `userspace/etc/desktop.cfg` (`KERNEL://<name>`).

> Для нового приложения **без пересборки ядра** лучше делать его userspace-ELF с
> `gui_win_*` (см. USERSPACE.md) — оно автоматически появится на рабочем столе.
