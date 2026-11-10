# Системные вызовы (syscall'ы)

## ABI

- Вызов: инструкция `syscall`, номер в `RAX`, аргументы в `RDI, RSI, RDX, R10, R8, R9`
  (SysV AMD64). Возврат значения в `RAX`.
- Ядро: `centcom/core/syscall.c` (`syscall_dispatch`) + `syscall.asm` (`syscall_entry`,
  `sysretq`). Номера — в `centcom/core/syscall.h`.
- Пользовательская сторона: обёртки в `userspace/libc/syscall.asm`, объявления в
  `userspace/libc/include/unistd.h` и `gui.h`.

## Таблица syscall'ов

| № | Имя | Сигнатура (userspace) | Описание |
|----|-----|----------------------|----------|
| 0 | `EXIT` | `_exit(int code)` | завершение процесса (ZOMBIE) |
| 1 | `WRITE` | `write(fd, buf, len)` | fd 1/2 → GUI-терминал или fb |
| 2 | `OPEN` | `open(name, flags)` | открыть файл initrd → fd |
| 3 | `READ` | `read(fd, buf, len)` | fd 0 = stdin (клавиатура) |
| 4 | `CLOSE` | `close(fd)` | закрыть fd |
| 5 | `LIST` | `sys_list(idx, buf, max)` | имя файла initrd по индексу |
| 6 | `SLEEP` | `sleep(ms)` | блокировка на N мс |
| 7 | `PLAY_PCM` | `play_pcm(buf, samples)` | проиграть PCM через SB16 |
| 8 | `KV_WRITE` | `kv_write(key, data, len)` | запись в key-value |
| 9 | `KV_READ` | `kv_read(key, out, max)` | чтение из key-value |
| 10 | `NTRFS_LIST` | `ntrfs_list(idx, &inode)` | список файлов диска |
| 11 | `NTRFS_READ` | `ntrfs_read(name, buf, max)` | чтение файла с диска |
| 12 | `NTRFS_WRITE` | `ntrfs_write(name, buf, len)` | запись файла на диск |
| 13 | `EXEC` | `exec(path)` | запустить ELF из initrd |
| 14 | `FORK` | `fork()` | клонировать процесс |
| 15 | `WAIT` | `sys_wait(pid, &status)` | ждать завершения |
| 16 | `IPC_CREATE` | `ipc_create()` | создать порт |
| 17 | `IPC_SEND` | `ipc_send(port, buf, len)` | отправить сообщение |
| 18 | `IPC_RECV` | `ipc_recv(port, buf, max)` | получить сообщение |
| 19 | `NTRFS_FORMAT` | `ntrfs_format()` | отформатировать диск |
| 20 | `NTRFS_MOUNT` | `ntrfs_mount()` | смонтировать диск |
| 21 | `NET_SEND` | `net_send(buf, len)` | отправить Ethernet-кадр (e1000) |
| 22 | `NET_RECV` | `net_recv(buf, max)` | получить Ethernet-кадр |
| 23 | `GUI_WIN_CREATE` | `gui_win_create(title, w, h)` | создать окно → id |
| 24 | `GUI_WIN_CLOSE` | `gui_win_close(id)` | закрыть окно |
| 25 | `GUI_WIN_RECT` | `gui_win_rect(id, &cmd)` | заливка прямоугольника |
| 26 | `GUI_WIN_TEXT` | `gui_win_text(id, &cmd)` | вывод текста |
| 27 | `GUI_WIN_EVENT` | `gui_win_event(id, &ev)` | опрос события (1=есть, 0=нет, -1=окно закрыто) |
| 28 | `GUI_WIN_TITLE` | `gui_win_set_title(id, title)` | сменить заголовок |
| 29 | `TICKS` | `get_ticks()` | uptime в тиках PIT (100 Гц) |

## GUI-структуры (`userspace/libc/include/gui.h`)

```c
#define GUI_TITLEBAR_H 24
#define GUI_EV_NONE  0
#define GUI_EV_KEY   1
#define GUI_EV_CLICK 2
#define GUI_EV_CLOSE 3

typedef struct { int x, y, w, h; unsigned int color; } gui_rect_cmd_t;
typedef struct { int x, y; const char *text; unsigned int fg, bg; } gui_text_cmd_t;
typedef struct { int type; int key; int x, y; } gui_event_t;
```

## Как добавить новый syscall

1. **kernel** `centcom/core/syscall.h`: `#define SYS_XXX N`.
2. **kernel** `centcom/core/syscall.c`: функция `static uint64_t sys_xxx(...)`
   (проверяй указатели через `is_user_ptr`) + `case SYS_XXX:` в `syscall_dispatch`.
3. **userspace** `userspace/libc/syscall.asm`:
   ```asm
   global xxx
   xxx:
       mov rax, N
       syscall
       ret
   ```
4. **userspace** `userspace/libc/include/unistd.h` (или `gui.h`): объявление функции.

### Правила проверки указателей

- `is_user_ptr(addr, len)` — адрес должен лежать в диапазоне
  `[USER_CODE_VADDR, USER_STACK_VADDR)`.
- Строки из userspace читай через `copy_user_str()` (с ограничением длины).

## Пример: минимальное GUI-приложение

```c
// userspace/apps/myapp/main.c
#include <unistd.h>
#include <gui.h>
int main(void) {
    int id = gui_win_create("My App", 320, 200);
    if (id < 0) return 1;

    gui_rect_cmd_t r = {0,0,320,200,0x00070B12};
    gui_win_rect(id, &r);
    gui_text_cmd_t t = {12,40,"Hello from userland!",0x0044FF88,0x00070B12};
    gui_win_text(id, &t);

    for (;;) {
        gui_event_t ev;
        int rc = gui_win_event(id, &ev);
        if (rc != 0) break;      /* -1 = закрыто WM; 1 = событие */
        sleep(20);
    }
    gui_win_close(id);
    return 0;
}
```
