# Пользовательский режим (userspace)

Пользовательские программы — это ELF64, собранные отдельно от ядра и загружаемые в
рантайме из initrd. **Добавление приложения не требует пересборки ядра.**

## Структура

```
userspace/
├── libc/
│   ├── crt0.asm        (старт: argc/argv, вызов main)
│   ├── syscall.asm     (обёртки syscall'ов)
│   ├── printf.c, string.c, synth_user.c
│   └── include/
│       ├── unistd.h    (write, read, sleep, exec, ipc, net, ntrfs, ...)
│       ├── gui.h       (GUI-окна)
│       ├── stdio.h, stdint.h, string.h, stddef.h, stdarg.h
├── apps/    (hello, ls, sh, cat, echo, sleep, about, clock, audio_server, ...)
├── tests/   (ping, net_test, dhcp_test, udp_test)
├── etc/     (motd, desktop.cfg)
└── stage/   (промежуточная сборка initrd)
```

## Файловая раскладка initrd

```
/bin/    все исполняемые ELF
/etc/    конфигурация (motd, desktop.cfg, ...)
/home/   пользовательские файлы
```

`ntmem_fs_find(name)` резолвит пути: точное имя → без ведущих `/`/`./` → поиск в `bin/`.
То есть `hello.elf`, `bin/hello.elf` и `/bin/hello.elf` — эквивалентны.

## libc API (кратко)

```c
// unistd.h
long write(int fd, const void *buf, size_t len);
long read(int fd, void *buf, size_t len);
void sleep(unsigned long ms);
int  open(const char *name, int flags);
int  close(int fd);
long sys_list(int idx, char *buf, size_t max);
int  exec(const char *path);
int  fork(void);
int  sys_wait(int pid, int *status);
long play_pcm(const void *buf, unsigned long samples);
int  kv_write(const char *key, const void *data, unsigned long len);
long kv_read(const char *key, void *out, unsigned long max);
int  ntrfs_list(int idx, struct ntrfs_inode_user *out);
long ntrfs_read(const char *name, void *buf, unsigned long max);
int  ntrfs_write(const char *name, const void *buf, unsigned long len);
long ipc_create(void);
long ipc_send(long port, const void *buf, unsigned long len);
long ipc_recv(long port, void *buf, unsigned long max);
long net_send(const void *buf, unsigned long len);
long net_recv(void *buf, unsigned long max);

// gui.h
int  gui_win_create(const char *title, int w, int h);
int  gui_win_close(int id);
int  gui_win_rect(int id, const gui_rect_cmd_t *cmd);
int  gui_win_text(int id, const gui_text_cmd_t *cmd);
int  gui_win_event(int id, gui_event_t *ev);
int  gui_win_set_title(int id, const char *title);
unsigned long get_ticks(void);

// stdio.h
int printf(const char *fmt, ...);
int puts(const char *s);
```

## Модель процесса

- Каждый ELF запускается как отдельный процесс (PID, своё адресное пространство).
- `printf` пишет в fd 1 → если GUI активен, вывод идёт в буфер терминала (окно
  «Terminal»); иначе — прямо в framebuffer.
- `read(0, ...)` — блокирующий ввод с клавиатуры (PS/2 + USB).
- `sleep(ms)` — блокирующая пауза (планировщик переключается на другие задачи).
- При завершении процесс становится zombie, GUI/шелл вызывает `process_reap`.

## Как написать приложение

1. `userspace/apps/<name>/main.c`:
   ```c
   #include <stdio.h>
   #include <unistd.h>
   int main(int argc, char **argv) {
       printf("Hello, argc=%d\n", argc);
       return 0;
   }
   ```
2. `make -C userspace` → соберётся `bin/<name>.elf`.
3. Запуск: `run <name>` / `spawn <name>` в шелле, или **двойной клик по ярлыку**
   на рабочем столе (ярлык появится автоматически).

### GUI-приложение (с окном)

См. пример в [SYSCALLS.md](SYSCALLS.md) и готовые `userspace/apps/about/` и
`userspace/apps/clock/`.

## Звук из userspace

```c
// проиграть готовый 8-бит PCM (до 32768 сэмплов, 22050 Гц)
static uint8_t pcm[1000];   // заполнить сэмплами
play_pcm(pcm, sizeof(pcm));
```

Готовые аудио-приложения: `audio_server`, `audio_test`, `midiplay`, `midi2ntsad`, `synth`.

## Сеть из userspace

```c
uint8_t frame[64];   // готовый Ethernet-кадр (14 байт заголовка + payload)
net_send(frame, sizeof(frame));
uint8_t rx[2048];
long n = net_recv(rx, sizeof(rx));   // 0 = нет данных
```

См. примеры: `tests/ping`, `tests/udp_test`, `tests/net_test`, `tests/dhcp_test`.

## Авто-обнаружение на рабочем столе

Рабочий стол (`centcom/gui/desktop.c`) читает `/etc/desktop.cfg` (встроенные
`KERNEL://` приложения) и автоматически добавляет ярлык для каждого `bin/*.elf`.
Формат `desktop.cfg`:

```
# NAME | EXEC | ICON | COLOR
TERMINAL | KERNEL://terminal | >_ | 0x44FF88
MYAPP    | bin/myapp.elf     | A  | 0xFFAA00
```
