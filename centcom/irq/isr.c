#include "isr.h"
#include "pic.h"
#include "../core/panic.h"
#include "../log/log.h"
#include "../log/serial.h"
#include "../time/pit.h"
#include "../driver/keyboard.h"
#include "../sound/cue.h"
#include "../ntsad/ntsad.h"
#include "../sched/sched.h"

static irq_handler_t g_handlers[16] = {0};
static volatile uint64_t g_tick_count = 0;

void irq_register(int irq, irq_handler_t h) {
    if (irq >= 0 && irq < 16) g_handlers[irq] = h;
}

static void irq0_handler(regs_t *r) {
    (void)r;
    pit_on_tick();
    cue_tick();
    ntsad_tick();

    g_tick_count++;
    if ((g_tick_count % 100) == 0) {
        serial_puts("[tick] "); serial_putdec(g_tick_count); serial_puts("\n");
    }

    /* КРИТИЧНО: EOI отправляем ДО context_switch,
       иначе PIC застрянет и следующий IRQ0 не придёт. */
    pic_send_eoi(0);

    sched_tick();
}

static void irq1_handler(regs_t *r) {
    (void)r;
    keyboard_irq();
}

void isr_irq_init(void) {
    irq_register(0, irq0_handler);
    irq_register(1, irq1_handler);
    logi(S_IRQ, "IRQ handlers registered: 0 (PIT), 1 (keyboard)");
}

void isr_dispatch(regs_t *r) {
    if (r->int_no < 32) {
        panic_exception(r);
        return;
    }
    if (r->int_no >= 32 && r->int_no < 48) {
        int irq = r->int_no - 32;

        if (irq != 0 && irq != 1) {
            serial_puts("[IRQ] got irq=");
            serial_putdec((uint64_t)irq);
            serial_puts("\n");
        }

        if (g_handlers[irq]) g_handlers[irq](r);

        /* IRQ0 уже отправил EOI внутри обработчика — не дублируем */
        if (irq != 0) {
            pic_send_eoi((unsigned char)irq);
        }
        return;
    }
    panic_unknown(r);
}