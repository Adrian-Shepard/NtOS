#include "bootscreen.h"
#include "fb.h"
#include "../sound/speaker.h"
#include "../core/gdt.h"
#include "../core/idt.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../mem/heap.h"
#include "../time/pit.h"
#include "../fs/ntmem_fs.h"
#include <stdint.h>

/* ============================================================= */
/*                     ГЛОБАЛЬНЫЙ РЫЧАГ ЗВУКА                    */
/* ============================================================= */
/* 1 = звуки включены, 0 = полная тишина во время boot screen.   */
/* Одно место — отключает все бипы ниже.                         */
#define BOOT_SCREEN_SOUND 0

/* ---------- цвета ---------- */
#define BG_BLACK 0x00000000
#define FG_GREEN 0x0033FF33
#define FG_AMBER 0x00FFC844
#define BG_RED   0x00901828
#define FG_CYAN  0x0040E8E8

/* ---------- тайминги ---------- */
#define TYPE_PAUSE   140
#define LINE_PAUSE   900
#define BLANK_PAUSE  200
#define BLOCK_PAUSE  120

/* ---------- звуковые макросы ---------- */
/*
 * Профиль SOFT: частоты 110–196 Hz, длительности 8–100 мс.
 * Пауза после бипа пропорциональна длительности (в speaker_beep).
 * Субъективно воспринимается как тихие щелчки, не как «гудки».
 */
#if BOOT_SCREEN_SOUND
  #define BEEP_PHASE   speaker_beep(147, 15)   /* D3   15 мс — старт фазы */
  #define BEEP_END     speaker_beep(196, 18)   /* G3   18 мс — конец блока */
  #define BEEP_TICK    speaker_beep(110,  8)   /* A2    8 мс — тик данных */
  #define BEEP_REVEAL  speaker_beep(165, 30)   /* E3   30 мс — появление */
  #define BEEP_LOUD    speaker_beep(131, 60)   /* C3   60 мс — большой акцент */
  #define BEEP_TRUST   speaker_beep(110, 100)  /* A2  100 мс — закрытие */
#else
  #define BEEP_PHASE
  #define BEEP_END
  #define BEEP_TICK
  #define BEEP_REVEAL
  #define BEEP_LOUD
  #define BEEP_TRUST
#endif

/* ---------- утилиты ---------- */

static void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms * 500; i++)
        __asm__ volatile("pause");
}

static void tw(const char *s, uint32_t fg) {
    while (*s) {
        fb_putc(*s++, fg, BG_BLACK);
        for (uint32_t i = 0; i < TYPE_PAUSE; i++) __asm__ volatile("pause");
    }
}

static void twline(const char *s) {
    tw(s, FG_GREEN);
    fb_putc('\n', FG_GREEN, BG_BLACK);
    delay_ms(LINE_PAUSE);
}

static void blank(void) {
    fb_putc('\n', FG_GREEN, BG_BLACK);
    delay_ms(BLANK_PAUSE);
}

/* ---------- вывод чисел ---------- */

static void puthex(uint32_t v) {
    const char *h = "0123456789abcdef";
    for (int i = 7; i >= 0; i--)
        fb_putc(h[(v >> (i * 4)) & 0xF], FG_GREEN, BG_BLACK);
}

static void puthex64(uint64_t v) {
    const char *h = "0123456789abcdef";
    fb_puts("0x", FG_GREEN, BG_BLACK);
    for (int i = 15; i >= 0; i--)
        fb_putc(h[(v >> (i * 4)) & 0xF], FG_GREEN, BG_BLACK);
}

static void putdec(uint64_t v) {
    char b[24]; int i = 0;
    if (!v) { fb_putc('0', FG_GREEN, BG_BLACK); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) fb_putc(b[i], FG_GREEN, BG_BLACK);
}

static void putdec_pad(uint64_t v, int width) {
    char b[24]; int i = 0;
    if (!v) b[i++] = '0';
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i < width) b[i++] = ' ';
    while (i--) fb_putc(b[i], FG_GREEN, BG_BLACK);
}

/* печатает 64 бита двумя строками */
static void print_bin_split(uint64_t v, uint32_t fg, uint32_t bg) {
    for (int i = 63; i >= 32; i--) {
        fb_putc((v >> i) & 1 ? '1' : '0', fg, bg);
        if (i % 8 == 0 && i != 32) fb_putc(' ', fg, bg);
    }
    fb_putc('\n', fg, bg);
    for (int i = 0; i < 12; i++) fb_putc(' ', fg, bg);
    for (int i = 31; i >= 0; i--) {
        fb_putc((v >> i) & 1 ? '1' : '0', fg, bg);
        if (i % 8 == 0 && i != 0) fb_putc(' ', fg, bg);
    }
    fb_putc('\n', fg, bg);
}

/* ============================================================= */
/*          ФАЗА 1: HANDOFF — реальные данные из bootinfo        */
/* ============================================================= */
void bootscreen_phase1(const bootinfo_t *bi) {
    BEEP_PHASE;
    fb_set_scale(2);
    fb_clear(BG_BLACK);
    delay_ms(300);

    twline("BOOT HANDOFF — FIRMWARE TO KERNEL");
    blank();

    twline("FRAMEBUFFER");
    tw("  ADDRESS: ", FG_GREEN);  puthex64(bi->fb_addr);  twline("");
    tw("  WIDTH:   ", FG_GREEN);  putdec(bi->fb_width);   twline(" px");
    tw("  HEIGHT:  ", FG_GREEN);  putdec(bi->fb_height);  twline(" px");
    tw("  PITCH:   ", FG_GREEN);  putdec(bi->fb_pitch);   twline(" bytes/row");
    tw("  BPP:     ", FG_GREEN);  putdec(bi->fb_bpp);     twline(" bits/pixel");
    tw("  SIZE:    ", FG_GREEN);
        putdec((uint64_t)bi->fb_width * bi->fb_height * 4);
    twline(" bytes");
    blank();

    twline("BOOTINFO STRUCTURE");
    tw("  MAGIC:   ", FG_GREEN);  puthex64(bi->magic);    twline("");
    tw("  ADDRESS: ", FG_GREEN);  puthex64(BOOTINFO_FIXED_ADDR); twline("");
    blank();

    twline("MEMORY MAP");
    tw("  ADDRESS: ", FG_GREEN);  puthex64(bi->mmap_addr);       twline("");
    tw("  SIZE:    ", FG_GREEN);  putdec(bi->mmap_size);         twline(" bytes");
    tw("  ENTRY:   ", FG_GREEN);  putdec(bi->mmap_desc_size);    twline(" bytes");
    tw("  VERSION: ", FG_GREEN);  putdec(bi->mmap_desc_ver);     twline("");
    blank();

    BEEP_END;
    tw("[ HANDOFF VERIFIED ]\n", FG_AMBER);
    delay_ms(BLOCK_PAUSE);
}

/* ============================================================= */
/*          ФАЗА 2: CPU — реальные GDT/IDT/TSS                   */
/* ============================================================= */
void bootscreen_phase2(void) {
    BEEP_PHASE;
    fb_set_scale(2);
    fb_clear(BG_BLACK);
    delay_ms(300);

    twline("CPU SUBSYSTEM");
    blank();

    twline("GLOBAL DESCRIPTOR TABLE");
    tw("  ENTRIES: ", FG_GREEN);  putdec(gdt_count());       twline(" loaded");
    tw("  TSS:     ", FG_GREEN);  puthex64(gdt_tss_base());  twline("");
    blank();

    twline("INTERRUPT DESCRIPTOR TABLE");
    tw("  VECTORS:  ", FG_GREEN);  putdec(idt_count());  twline(" loaded");
    twline("  EXC GATES: 0..31");
    twline("  IRQ GATES: 32..47");
    blank();

    twline("EXECUTION MODE");
    twline("  LONG MODE: 64-BIT");
    twline("  RING:      KERNEL (0)");
    blank();

    BEEP_END;
    tw("[ CPU LAYER ARMED ]\n", FG_AMBER);
    delay_ms(BLOCK_PAUSE);
}

/* ============================================================= */
/*          ФАЗА 3: MEMORY — реальные цифры PMM/VMM/HEAP         */
/* ============================================================= */
void bootscreen_phase3(void) {
    BEEP_PHASE;
    fb_set_scale(2);
    fb_clear(BG_BLACK);
    delay_ms(300);

    uint64_t total_pages = pmm_total_pages();
    uint64_t free_pages  = pmm_free_pages();
    uint64_t used_pages  = total_pages - free_pages;
    uint64_t total_mb    = (total_pages * 4) / 1024;
    uint64_t free_mb     = (free_pages * 4) / 1024;
    uint64_t used_mb     = (used_pages * 4) / 1024;

    twline("PHYSICAL MEMORY MANAGER");
    tw("  TOTAL: ", FG_GREEN);  putdec(total_mb);    twline(" MiB");
    tw("  FREE:  ", FG_GREEN);  putdec(free_mb);     twline(" MiB");
    tw("  USED:  ", FG_GREEN);  putdec(used_mb);     twline(" MiB");
    tw("  PAGES: ", FG_GREEN);  putdec(total_pages); twline(" total");
    blank();

    twline("VIRTUAL MEMORY MANAGER");
    tw("  CR3:   ", FG_GREEN);  puthex64(vmm_get_cr3()); twline("");
    twline("  4-LEVEL PAGING");
    twline("  IDENTITY MAP: 0..4 GiB");
    blank();

    twline("KERNEL HEAP");
    tw("  BASE:  ", FG_GREEN);  puthex64(0x5000000);  twline("");
    tw("  USED:  ", FG_GREEN);  putdec(heap_used());   twline(" bytes");
    tw("  TOTAL: ", FG_GREEN);  putdec(heap_total());  twline(" bytes");
    blank();

    BEEP_END;
    tw("[ MEMORY LAYER ONLINE ]\n", FG_AMBER);
    delay_ms(BLOCK_PAUSE);
}

/* ============================================================= */
/*          ФАЗА 4: PERIPHERALS — реальное состояние             */
/* ============================================================= */
void bootscreen_phase4(void) {
    BEEP_PHASE;
    fb_set_scale(2);
    fb_clear(BG_BLACK);
    delay_ms(300);

    twline("INTERRUPT CONTROLLER");
    twline("  8259 PIC: REMAPPED");
    twline("  VECTORS:  0x20..0x2F");
    twline("  IRQ0:     PIT");
    twline("  IRQ1:     KEYBOARD");
    blank();

    twline("PROGRAMMABLE INTERVAL TIMER");
    twline("  CHANNEL: 0");
    twline("  MODE:    SQUARE WAVE");
    tw("  FREQ:    ", FG_GREEN);  putdec(100);          twline(" Hz");
    tw("  TICKS:   ", FG_GREEN);  putdec(pit_ticks());  twline("");
    blank();

    twline("INPUT");
    twline("  PS/2 CONTROLLER: INITIALIZED");
    twline("  KEYBOARD:        READY");
    blank();

    twline("SERIAL PORT");
    twline("  COM1 @ 0x3F8");
    twline("  BAUD: 38400 8N1");
    blank();

    twline("FRAMEBUFFER");
    twline("  LINEAR 32BPP");
    blank();

    twline("SOUND");
    twline("  PC SPEAKER: READY");
    twline("  NTSAD BUS:  ONLINE");
    blank();

    BEEP_END;
    tw("[ PERIPHERALS READY ]\n", FG_AMBER);
    delay_ms(BLOCK_PAUSE);
}

/* ============================================================= */
/*          ФАЗА 5: BINARY — реальный дамп ядра                  */
/* ============================================================= */
void bootscreen_phase5(const bootinfo_t *bi) {
    fb_set_scale(1);
    fb_clear(BG_RED);

    BEEP_REVEAL;
    delay_ms(1200);

    fb_set_scale(6);
    fb_puts("     N T O S      \n", FG_CYAN, BG_RED);
    BEEP_LOUD;
    delay_ms(2500);

    fb_set_scale(3);
    fb_puts("  SYSTEM BOOT BINARY  \n", FG_CYAN, BG_RED);
    delay_ms(2500);

    fb_set_scale(1);
    fb_puts("\n", FG_CYAN, BG_RED);
    delay_ms(1500);

    uint64_t total_pages = pmm_total_pages();
    uint64_t free_pages  = pmm_free_pages();
    uint64_t heap_u      = heap_used();
    uint64_t heap_t      = heap_total();

    #define BIN(label, value) do { \
        BEEP_TICK; \
        fb_puts(label, FG_CYAN, BG_RED); \
        print_bin_split(value, FG_CYAN, BG_RED); \
        delay_ms(1200); \
    } while (0)

    BIN("BOOTINFO    ", BOOTINFO_FIXED_ADDR);
    BIN("BOOT_MAGIC  ", bi->magic);
    BIN("FB_ADDR     ", bi->fb_addr);
    BIN("FB_DIMS     ", ((uint64_t)bi->fb_width << 32) | bi->fb_height);
    BIN("FB_PITCH    ", bi->fb_pitch);
    BIN("FB_BPP      ", bi->fb_bpp);
    BIN("MMAP_ADDR   ", bi->mmap_addr);
    BIN("MMAP_SIZE   ", bi->mmap_size);
    BIN("MMAP_DESC   ", bi->mmap_desc_size);
    BIN("RAM_TOTAL   ", total_pages);
    BIN("RAM_FREE    ", free_pages);
    BIN("HEAP_USED   ", heap_u);
    BIN("HEAP_TOTAL  ", heap_t);

    #undef BIN

    fb_puts("\n", FG_CYAN, BG_RED);
    delay_ms(500);

    BEEP_TICK;
    fb_puts("FS_MOUNTED  ", FG_CYAN, BG_RED);
    putdec_pad(ntmem_fs_count(), 2);
    fb_puts(" file(s) in ntmem_fs\n", FG_CYAN, BG_RED);
    delay_ms(800);

    fb_puts("\n", FG_CYAN, BG_RED);
    delay_ms(500);

    BEEP_REVEAL;
    fb_puts("           STATUS: KERNEL OPERATIONAL\n", FG_CYAN, BG_RED);
    delay_ms(2500);

    BEEP_TRUST;
    fb_puts("           TRUST THE CORPORATION\n", FG_CYAN, BG_RED);
    delay_ms(4500);
}