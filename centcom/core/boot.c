#include <stdint.h>
#include "../../bootinfo.h"
#include "../log/serial.h"
#include "../log/log.h"
#include "../tty/fb.h"
#include "../tty/bootscreen.h"
#include "../tty/shell.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/heap.h"
#include "../irq/pic.h"
#include "../irq/isr.h"
#include "../time/pit.h"
#include "../driver/keyboard.h"
#include "../driver/sb16.h"
#include "../fs/ntmem_fs.h"
#include "../ntsad/ntsad.h"
#include "../sound/speaker.h"
#include "../sound/cue.h"
#include "../sched/sched.h"
#include "../driver/ahci.h"
#include "../driver/mouse.h"
#include "panic.h"
#include "gdt.h"
#include "idt.h"
#include "syscall.h"

void centcom_boot(bootinfo_t *bi_unused) {
    (void)bi_unused;
    serial_init();

    bootinfo_t *bi = (bootinfo_t*)BOOTINFO_FIXED_ADDR;

    if (bi->magic != BOOTINFO_MAGIC) {
        serial_puts("FATAL: bad bootinfo\n");
        for (;;) __asm__ volatile("hlt");
    }

    /* --- Framebuffer: нужен для вывода --- */
    fb_init(bi);

    /* --- Спикер: чтобы можно было пищать уже с первой фазы --- */
    speaker_init();

    /* ================ ФАЗА 1: HANDOFF ================ */
    bootscreen_phase1(bi);

    /* --- CPU: GDT, IDT --- */
    gdt_init();
    idt_init();

    /* ================ ФАЗА 2: CPU ================ */
    bootscreen_phase2();

    /* --- Память --- */
    pmm_init(bi);
    vmm_init(bi);
    heap_init();

    /* ================ ФАЗА 3: MEMORY ================ */
    bootscreen_phase3();
    /* --- v0.15: диск --- */
    ahci_init();
    /* --- Прерывания, таймер, клавиатура --- */
    pic_init();
    isr_irq_init();
    pit_init(100);
    keyboard_init();
    mouse_init();
    /* --- Шина событий, звук, подписки --- */
    ntsad_init();
    cue_init();

    /* --- SB16 --- */
    sb16_init();

    /* ================ ФАЗА 4: PERIPHERALS ================ */
    bootscreen_phase4();

    /* --- Файловая система --- */
    extern const uint8_t initrd_start[];
    extern const uint8_t initrd_end[];
    uint64_t initrd_size = (uint64_t)(initrd_end - initrd_start);
    ntmem_fs_init(initrd_start, initrd_size);

    /* --- Syscall --- */
    syscall_init();

    /* --- v0.14a: планировщик --- */
    sched_init();

    /* ================ ФАЗА 5: BINARY ================ */
    bootscreen_phase5(bi);

    /* --- Включаем прерывания: теперь тикает PIT и работает scheduler --- */
    __asm__ volatile("sti");
    logi(S_CPU, "interrupts enabled");

    /* --- Логи в serial (полный отчёт) --- */
    logi(S_BOO, "CENTCOM core online");
    logi(S_BOO, "NtOS v0.14a boot sequence complete");
    logi(S_VMM, "framebuffer = %dx%d @ %x",
         bi->fb_width, bi->fb_height, bi->fb_addr);
    logi(S_PMM, "RAM: %d MiB total, %d MiB free",
         (pmm_total_pages() * 4) / 1024,
         (pmm_free_pages()  * 4) / 1024);
    logi(S_HEP, "heap: %d/%d bytes used",
         heap_used(), heap_total());
    logi(S_TMR, "PIT configured at 100 Hz");
    logi(S_KBD, "PS/2 keyboard ready");
    logi(S_NTM, "ntmem_fs: %d file(s)", ntmem_fs_count());
    logi(S_SCH, "scheduler: %d threads", process_count());
    logi(S_BOO, "boot complete. Trust the Corporation.");
    logi(S_SEC, "access level: %d/10", 1);

    /* --- Финальный сигнал через NTSAD --- */
    ntsad_state(NTSAD_CH_CORE, NTSAD_STATE_READY);
    ntsad_cue(NTSAD_CUE_BOOT);

    /* --- Shell --- */
    shell_run();

    for (;;) __asm__ volatile("hlt");
}