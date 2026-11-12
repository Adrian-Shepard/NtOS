# NtOS / CENTCOM — документация разработчика

NtOS (стилизованно **CENTCOM**, «Nanotrasen Security Systems») — хобби-операционная
система для **x86-64**, написанная с нуля на C и ассемблере (NASM). Загружается через
UEFI или GRUB (Multiboot 2), имеет собственное ядро, оконный менеджер с рабочим столом,
многозадачность, драйверы (PS/2, SB16, AHCI, e1000, USB/xHCI), файловую систему в памяти
(initrd) и пользовательские ELF-приложения.

## Возможности

- **Загрузка**: UEFI (bootx64) и Multiboot 2 / GRUB (multiboot + dualboot).
- **Память**: bitmap-аллокатор страниц (PMM), 4-уровневый пейджинг (VMM), heap 16 МБ.
- **Многозадачность**: round-robin планировщик, 16 процессов, kernel-потоки и
  пользовательские ELF-процессы.
- **GUI**: оконный менеджер, рабочий стол (ярлыки, двойной клик), пользовательские окна
  (UWM), двойная буферизация кадрового буфера.
- **Драйверы**: PS/2 клавиатура/мышь, SB16 (звук), AHCI (SATA), DMA, e1000 (сеть),
  USB xHCI + HID-клавиатура, PC-спикер, синтезатор.
- **Файловая система**: `ntmem_fs` (initrd tar) и `ntrfs` (диск).
- **Пользовательский режим**: libc, syscall'ы, GUI-приложения (`about`, `clock`), сеть,
  звук (`play_pcm`), IPC.
- **Игры**: порт **Doom** (встроенный doom1.wad).

## Быстрый старт

```bash
make ntos.iso   # собрать ядро + initrd + ISO
make run        # запустить в QEMU (UEFI/GRUB, с аудио и сетью)
make log        # смотреть serial.log
make clean      # полная очистка
```

Требования к инструментам: `gcc`, `ld`, `nasm`, `objcopy`, `grub-mkrescue`, `xorriso`.
QEMU в `Makefile` указан как Windows-путь (`X:/OSdevTools/...`), при необходимости
поправьте переменные `QEMU`, `OVMF_CODE`, `OVMF_VARS`, `IMG_WIN`, `DISK_WIN`, `SERIAL_WIN`.

## Структура репозитория

```
boot/          загрузчики (bootx64.c, multiboot.asm, dualboot.c, grub.cfg)
centcom/       ядро (CENTCOM)
  core/        старт, GDT/IDT, syscall, usermode, panic
  mem/         pmm, vmm, heap
  tty/         framebuffer (fb), shell, bootscreen, шрифты
  gui/         WM, рабочий стол, uwm (окна пользователей), kernel-apps
  apps/        встроенные приложения (terminal, taskman, editor)
  driver/      драйверы (input/, audio/, storage/, network/, usb/)
  fs/          ntmem_fs, ntrfs, tar
  sched/       планировщик
  time/        PIT
  irq/         PIC, ISR
  log/         логгирование (serial + fb)
  sound/       speaker, cue, synth
  elf/         загрузчик ELF
  bus/         PCI
  store/       key-value хранилище
  ipc/         межпроцессное взаимодействие
  ntsad/       шина событий
userspace/     пользовательский режим
  libc/        crt0, printf, string, syscall.asm, заголовки (unistd.h, gui.h)
  apps/        приложения (hello, ls, sh, about, clock, audio_server, ...)
  tests/       тесты (ping, net_test, dhcp_test, udp_test)
  etc/         конфигурация (motd, desktop.cfg)
doomgeneric/   порт Doom
docs/          эта документация
```

## Индекс документации

| Файл | Содержание |
|------|-----------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Загрузка, карта памяти, модули ядра, ключевые константы |
| [BUILD.md](BUILD.md) | Сборка, Makefile, как добавить модуль/приложение |
| [SYSCALLS.md](SYSCALLS.md) | Полный справочник syscall'ов и как добавить новый |
| [DRIVERS.md](DRIVERS.md) | Каркас драйверов, структура папок, как добавить драйвер |
| [USERSPACE.md](USERSPACE.md) | Пользовательский режим: libc, приложения, файловая раскладка |
| [GUI.md](GUI.md) | Оконный менеджер, рабочий стол, UWM, GUI-syscall'ы |
| [ROADMAP.md](ROADMAP.md) | Известные ограничения и план дальнейшей разработки |
