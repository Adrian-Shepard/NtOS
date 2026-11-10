# Драйверы

## Структура каталога

Драйверы разложены по **подкатегориям** и **моделям**:

```
centcom/driver/
├── input/
│   ├── keyboard/   keyboard.c/.h   (PS/2 клавиатура)
│   └── mouse/      mouse.c/.h      (PS/2 мышь)
├── audio/
│   └── sb16/       sb16.c/.h       (Sound Blaster 16)
├── storage/
│   ├── ahci/       ahci.c/.h       (SATA AHCI)
│   └── dma/        dma.c/.h        (ISA DMA — для SB16)
├── network/
│   └── e1000/      e1000.c/.h      (Intel e1000)
└── usb/
    ├── controller/ usb.c/.h        (скан PCI, запуск xHCI)
    └── xhci/       xhci.c/.h       (xHCI + HID-клавиатура)
```

Включать заголовки рекомендуется через корень `centcom` (флаг `-I centcom`):
`#include "driver/input/keyboard/keyboard.h"`.

## Правила сборки

В `Makefile` для каждого драйвера отдельное правило, например:

```make
$(BUILD)/keyboard.o: centcom/driver/input/keyboard/keyboard.c ; @mkdir -p $(BUILD); $(CC) $(CC_CFLAGS) -c $< -o $@
```

Объекты должны быть в `COBJS`.

## Инициализация

Все драйверы инициализируются в `centcom/core/boot.c` (`centcom_boot`) в строгом
порядке (память → диск → PIC/IRQ → таймер → ввод → шины → сеть → USB → ФС).

```c
pmm_init → vmm_init → heap_init
ahci_init → sb16_init → pic_init → isr_irq_init → pit_init → keyboard_init → mouse_init
ntsad_init → ipc_init → cue_init → e1000_init → usb_init → pit_enable_usb_polling → ntmem_fs_init
```

## Описание драйверов

### Клавиатура (input/keyboard)
- PS/2 IRQ1. Буфер символов (ASCII) + **очередь скан-кодов** (для Doom).
- API: `keyboard_init`, `keyboard_irq`, `keyboard_getchar`, `keyboard_has_char`,
  `keyboard_push_char` (ввод от USB), `keyboard_push_scancode`,
  `keyboard_pop_scancode`, `keyboard_flush` (сброс очередей — после выхода из Doom).

### Мышь (input/mouse)
- PS/2 IRQ12. `mouse_init`, `mouse_irq`, `mouse_get_state` (x, y, left).

### SB16 (audio/sb16)
- 8-бит PCM, 22050 Гц, DMA-буфер по адресу `DMA_BUF_PHYS` (0x800000, 64 КБ).
- API: `sb16_init`, `sb16_play_tone_async(freq,ms)`, `sb16_play_tone(freq,ms)`,
  `sb16_play_pcm_async(pcm,samples)`, `sb16_is_playing`, `sb16_poll`, `sb16_stop`,
  `sb16_set_volume`.
- `sb16_poll()` нужно вызывать периодически (сбрасывает флаг занятости по тикам).

### AHCI (storage/ahci)
- SATA через PCI. `ahci_init`, чтение/запись секторов (используется ntrfs).

### DMA (storage/dma)
- ISA DMA канал 1 (8-бит), для SB16: `dma_setup_8bit`, `dma_unmask_channel`, `dma_mask_channel`.

### e1000 (network/e1000)
- Intel e1000 через PCI/MMIO. `e1000_init`, `e1000_send_packet`, `e1000_receive_packet`,
  `e1000_get_mac`. Работает через syscall'ы `NET_SEND`/`NET_RECV`.

### USB (usb/controller + usb/xhci)
- `usb_init` — сканирует PCI в поисках xHCI, назначает BAR и вызывает `xhci_init`.
- `xhci.c` — инициализация xHCI, перечисление устройств, **HID-клавиатура**.
- Опрос: `pit_on_tick` (каждый тик, если USB включён) + GUI-цикл вызывают `xhci_poll`.
- **Мостик ввода**: USB-клавиатура пишет и в ASCII-буфер (`keyboard_push_char`), и в
  очередь скан-кодов (`keyboard_push_scancode`, таблица HID Usage → PS/2 scancode set 1),
  поэтому она работает в shell, GUI и Doom.

## Как добавить новый драйвер

1. Создай `centcom/driver/<категория>/<модель>/<модель>.c` (+ `.h`).
2. Добавь правило сборки в `Makefile` и объект в `COBJS`.
3. Объяви `int <model>_init(void)` и вызови его в `boot.c` в нужном месте.
4. Для устройств с прерываниями зарегистрируй обработчик через `irq_register(irq, h)`
   (см. `isr_irq_init`).

## Рекомендации

- PCI-устройства ищи через `bus/pci.h` (`pci_read32`, `pci_find_device`).
- Физические буферы для DMA выделяй через `pmm_alloc`/`pmm_reserve` (адреса < 16 МБ).
- Не блокируй надолго в обработчиках прерываний; используй `sb16_poll`/флаги по тикам.
- Логируй через `logi(S_DRV, ...)` — вывод пойдёт в serial.log и на экран.
