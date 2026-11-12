# Сборка NtOS

## Инструменты

- `gcc` (ядро: freestanding, `-mcmodel=kernel`, `-mno-sse`)
- `ld` (линковка по `centcom/centcom.ld`)
- `nasm` (ассемблер: boot, isr, syscall, context, usermode)
- `objcopy` (встраивание initrd и doom1.wad как бинарных объектов)
- `grub-mkrescue` + `xorriso` (сборка ISO)

## Цели Makefile

| Цель | Описание |
|------|----------|
| `all` / `ntos.iso` | собрать всё и сделать ISO |
| `run` | запуск QEMU (q35 + OVMF + GRUB, SB16, e1000, AHCI, serial-лог) |
| `run-legacy` | запуск QEMU в legacy-режиме (BIOS/GRUB) |
| `log` | `tail -f serial.log` |
| `stop` | убить QEMU |
| `clean` | очистка (build/, ISO, disk, serial) |

### Сборка ядра

Объекты ядра перечислены в `COBJS` (Makefile). Правила по каталогам:
`$(BUILD)/%.o: centcom/core/%.c`, `centcom/tty/%.c`, `centcom/gui/%.c` и т.д.
Драйверы — отдельные правила (`centcom/driver/input/keyboard/keyboard.c` и т.п.).

Флаги: `CC_CFLAGS` (общие), `BOOT_CFLAGS` (загрузчик).

### Сборка userspace

`make -C userspace`:
- `libc.a` = crt0.o + printf.o + string.o + synth_user.o + syscall.o
- приложения: каждая папка `apps/*/main.c` → `apps/*/*.elf`
- `apps/doom/` собирается отдельно (Doom + свой мини-libc `doom_libc.c`,
  линкуется с `crt0.o` + `syscall.o` вместо `libc.a`)
- `initrd.tar` = `stage/bin/*.elf` + `stage/etc/*` + `stage/home/*` + `stage/share/*`
  (в `/share/doom1.wad` лежит IWAD)

Линковка ELF: `-T libc/user.ld` (база `0x10000000000`), `-mcmodel=large`.

## Как добавить модуль ядра

1. Создай `centcom/<каталог>/<name>.c` (и `.h` при необходимости).
2. Добавь объект в `COBJS` (Makefile) или воспользуйся существующим pattern-правилом.
3. Вызови инициализацию в `centcom/core/boot.c` (`centcom_boot`).
4. `make ntos.iso`.

## Как добавить драйвер

См. [DRIVERS.md](DRIVERS.md) — структура `centcom/driver/<категория>/<модель>/`.

## Как добавить пользовательское приложение

1. Создай `userspace/apps/<name>/main.c` (см. [USERSPACE.md](USERSPACE.md)).
2. `make` (userspace) — автоматически соберётся в `/bin/<name>.elf`.
3. Приложение **автоматически появится на рабочем столе** (desktop сканирует `bin/*.elf`).
   Ядро пересобирать не нужно.

## Диагностика

- `serial.log` — весь вывод ядра (`logi/logw/loge`, `[tick] N`, `[SCH]`, `[ELF]`).
- Паника — «CENTCOM CORE BOOT FAULT» на экране + строка в serial.
- Сборка завершилась с ошибкой? Смотри `make ... 2>&1 | grep error`.
