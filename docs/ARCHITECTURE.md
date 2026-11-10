# Архитектура NtOS

## Загрузка

Два пути загрузки:

1. **UEFI (bootx64.c)** — находит GOP, читает `centcom.bin` с диска, загружает ядро по
   `KERNEL_LOAD_ADDR = 0x4000000`, заполняет `bootinfo`, вызывает `centcom_boot`.
2. **GRUB / Multiboot 2 (multiboot.asm + dualboot.c)** — GRUB загружает ELF-ядро по
   адресу из линкер-скрипта (`. = 0x100000`), `dualboot_main` заполняет `bootinfo`
   (framebuffer + memory map) и вызывает `centcom_boot`. **Это путь по умолчанию.**

`centcom_boot()` (core/boot.c) выполняет инициализацию по фазам:

1. serial, bootinfo, framebuffer, speaker
2. GDT, IDT (bootscreen phase 1-2)
3. pmm → vmm → heap (phase 3)
4. AHCI, SB16, PIC, IRQ, PIT, клавиатура, мышь, ntsad, ipc, cue (phase 4)
5. e1000, USB (xHCI), ntmem_fs, syscall, scheduler (phase 5)
6. `gui_start()` (GUI) или `shell_run()` (текстовый shell)

## Карта памяти (адреса физические, identity-mapped)

| Диапазон | Назначение |
|----------|-----------|
| `0x000000..0x100000` | низкая память (резерв) |
| `0x100000..kernel_end` | **образ ядра** (text+data+bss+стек 256 КБ) |
| `kernel_end` (~0x759000) | конец BSS, определяется в `centcom.ld` (`kernel_end = ALIGN(4096)`) |
| `0x200000..0x210000` | временные таблицы страниц multiboot.asm |
| `0x4000000..0x4500000` | bootinfo (0x4400000) + EFI memory map (0x4500000) |
| `0x5000000..0x6000000` | **heap ядра**, 16 МБ (`HEAP_START`, `HEAP_SIZE`) |
| `0x800000..0x900000` | DMA-буфер SB16 (64 КБ, `DMA_BUF_PHYS`) |
| `0x8000000` | framebuffer (GOP), типично 1280x800x32 |
| `0x8100000` | MMIO e1000 / AHCI |

### Виртуальные адреса пользовательского режима

| Адрес | Назначение |
|-------|-----------|
| `0x0000010000000000` (2^40) | `USER_CODE_VADDR` — код/данные ELF |
| `0x0000020000000000` (2^41) | `USER_STACK_VADDR` — стек (16 страниц = 64 КБ) |

Пейджинг: 4 уровня. Ядро делает identity-map первых 4 ГБ 2-МБ huge-страницами.
Пользовательский процесс получает **собственный PML4** (`vmm_alloc_pml4`), у которого
`PML4[0]` = общее ядро (shared), а `PML4[2]` (код) и `PML4[4]` (стек) — приватные.

## Подсистемы ядра (centcom/)

### mem/
- **pmm** — bitmap-аллокатор страниц (4 КБ). `pmm_alloc/pmm_free/pmm_reserve`.
  Резервирует `0..kernel_end` и `0x4000000..0x4500000`.
- **vmm** — 4-уровневый пейджинг. `vmm_alloc_pml4`, `vmm_map`, `vmm_unmap`,
  `vmm_switch`, `vmm_clone_address_space`, `vmm_free_address_space` (пропускает PML4[0]).
- **heap** — free-list аллокатор поверх 16 МБ. `kmalloc/kfree/ksize`.

### sched/
- Round-robin, `PROC_MAX = 16`. Состояния: `PROC_READY/RUNNING/BLOCKED/ZOMBIE/FREE`.
- `process_create_kernel` (поток ядра, стек 4 КБ), `process_create_user` (ELF, стек ядра
  **16 КБ** — `KSTACK_PAGES = 4`), `process_fork`.
- Переключение контекста: `context_switch` (asm), CR3 переключается при смене процесса.
- `sched_tick()` вызывается из IRQ0 (PIT, 100 Гц).

### time/ (PIT)
- `pit_init(100)`, `pit_ticks()` (счётчик тиков, 1 тик = 10 мс), `pit_on_tick`
  (вызывает `xhci_poll` для USB-опроса, если USB готов).

### irq/
- PIC 8259 (remap), ISR-стабы (256 векторов, asm), `isr_dispatch`.
- IRQ0 → таймер (EOI до context_switch!), IRQ1 → клавиатура, IRQ12 → мышь.

### tty/ (fb)
- Кадровый буфер с **двойной буферизацией** (`fb_set_buffer`, `fb_swap`, `fb_swap_rect`).
- API: `fb_clear`, `fb_putc/puts`, `fb_fill_rect`, `fb_draw_box/hline/vline`,
  `fb_set_cursor/scale`, `fb_get_ptr` (back-buffer), `fb_get_real_ptr` (видеопамять),
  `fb_pitch_px`, `fb_blit`, `fb_fill_buf`, `fb_text_buf`.
- Шрифт 8x8 (`font8x8.h`). `fb_swap`/`fb_fill_rect` используют `rep movsq/stosl`.

### gui/ — см. [GUI.md](GUI.md)

### driver/ — см. [DRIVERS.md](DRIVERS.md)

### fs/
- **ntmem_fs** — файлы initrd (tar) в памяти. `ntmem_fs_find` понимает пути
  (`/bin/x`, `bin/x`, `x` → поиск в `bin/`).
- **ntrfs** — простая ФС на SATA-диске (AHCI).
- **tar** — парсер tar (обычные файлы).

### elf/
- Загрузчик ELF64 (только PT_LOAD, проверки границ/выравнивания).

### core/
- `syscall.c` (диспетчер), `syscall.asm` (entry, `syscall`/`sysretq`),
  `usermode.c/.asm` (вход в ring3), `panic.c` (экран смерти), `gdt/idt`.

### sound/
- `speaker` (PC-спикер), `cue` (последовательности нот, `CUE_SOUND_ENABLED=1`),
  `synth` (8-голосный синтезатор → SB16).

### Прочее
- `log/` — `logi/logw/loge/logd` (serial + fb), теги `subsys_t`.
- `ntsad/` — шина событий (подписка на события).
- `ipc/` — порты сообщений.
- `store/kv` — key-value хранилище.
- `bus/pci` — сканирование PCI.

## Ключевые константы

```c
#define KSTACK_PAGES    4    // стек ядра пользовательского процесса (16 КБ)
#define STACK_PAGES     16   // пользовательский стек (64 КБ)
#define PROC_MAX        16   // максимум процессов
#define HEAP_START      0x5000000ULL
#define HEAP_SIZE       (16ULL*1024*1024)
#define DMA_BUF_PHYS    0x800000
#define DMA_BUF_SIZE    65536
#define SAMPLE_RATE     22050 // SB16
#define BOOTINFO_FIXED_ADDR 0x4400000ULL
#define USER_CODE_VADDR  0x0000010000000000ULL
#define USER_STACK_VADDR 0x0000020000000000ULL
```

## Процесс загрузки пользовательского ELF (важно!)

`process_create_user()`:
1. `irq_save()` — **прерывания запрещены** на время смены CR3 и загрузки ELF
   (иначе планировщик мог бы переключить процесс посреди `vmm_map` и ELF отмапился бы
   в чужое адресное пространство).
2. `vmm_alloc_pml4()` → `vmm_switch(new_cr3)` → `elf_load` → маппинг стека →
   `vmm_switch(old_cr3)` → `irq_restore()`.
3. Стек ядра 16 КБ, стартовый кадр: `process_exit` (return-адрес) + `user_entry_thunk`.
4. Первый запуск: `context_switch` → `user_entry_thunk` → `usermode_enter(entry, rsp)`.

При выходе (`SYS_EXIT`) процесс становится `PROC_ZOMBIE`, GUI-цикл вызывает
`process_reap` (логирует `reap pid=...`), который освобождает адресное пространство
(`vmm_free_address_space`) и стеки.
