#include "xhci.h"
#include "driver/usb/controller/usb.h"
#include "driver/input/keyboard/keyboard.h"
#include "log/log.h"
#include "mem/heap.h"
#include "time/pit.h"

/* ================================================================
 *  Глобальное состояние контроллера
 * ================================================================ */
static volatile uint8_t  *g_cap_base = 0;
static volatile uint32_t *g_op_base  = 0;
static volatile uint32_t *g_ir_base  = 0;

static xhci_trb_t g_cmd_ring_mem[256]   __attribute__((aligned(64)));
static xhci_trb_t g_event_ring_mem[256] __attribute__((aligned(64)));
static uint64_t   g_dcbaa[256]          __attribute__((aligned(64)));
static xhci_erst_entry_t g_erst[1]      __attribute__((aligned(64)));

static xhci_ring_t g_cmd_ring;
static xhci_ring_t g_event_ring;
static uint8_t     g_max_slots = 0;
static int         g_max_ports = 0;

static xhci_device_ctx_t g_dev_ctxs[8] __attribute__((aligned(64)));
static xhci_input_ctx_t  g_input_ctx   __attribute__((aligned(64)));

static xhci_trb_t g_ep0_rings[8][64] __attribute__((aligned(64)));
static xhci_ring_t g_ep0_ring_structs[8];

/* ================================================================
 *  HID-клавиатура
 * ================================================================ */
static int     g_kbd_slot = -1;
static int     g_kbd_dci  = 0;
static uint8_t g_kbd_buf[8] __attribute__((aligned(16)));
static xhci_trb_t g_kbd_ring_mem[32] __attribute__((aligned(64)));
static xhci_ring_t g_kbd_ring;

#define KBD_QUEUE_SIZE 128
static char         g_kbd_q[KBD_QUEUE_SIZE];
static volatile int g_kbd_q_head = 0;
static volatile int g_kbd_q_tail = 0;

static void kbd_push(char c) {
    int nxt = (g_kbd_q_tail + 1) % KBD_QUEUE_SIZE;
    if (nxt != g_kbd_q_head) {
        g_kbd_q[g_kbd_q_tail] = c;
        g_kbd_q_tail = nxt;
    }
}

int xhci_get_key(void) {
    if (g_kbd_q_head == g_kbd_q_tail) return 0;
    char c = g_kbd_q[g_kbd_q_head];
    g_kbd_q_head = (g_kbd_q_head + 1) % KBD_QUEUE_SIZE;
    return (int)(uint8_t)c;
}

int xhci_kbd_ready(void) { return g_kbd_slot > 0; }

static const char g_hid_ascii[128] = {
    [0x04]='a',[0x05]='b',[0x06]='c',[0x07]='d',[0x08]='e',[0x09]='f',
    [0x0A]='g',[0x0B]='h',[0x0C]='i',[0x0D]='j',[0x0E]='k',[0x0F]='l',
    [0x10]='m',[0x11]='n',[0x12]='o',[0x13]='p',[0x14]='q',[0x15]='r',
    [0x16]='s',[0x17]='t',[0x18]='u',[0x19]='v',[0x1A]='w',[0x1B]='x',
    [0x1C]='y',[0x1D]='z',[0x1E]='1',[0x1F]='2',[0x20]='3',[0x21]='4',
    [0x22]='5',[0x23]='6',[0x24]='7',[0x25]='8',[0x26]='9',[0x27]='0',
    [0x28]='\n',[0x29]=27,[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='-',[0x2E]='=',[0x2F]='[',[0x30]=']',[0x31]='\\',
    [0x33]=';',[0x34]='\'',[0x35]='`',[0x36]=',',[0x37]='.',[0x38]='/',
};
static const char g_hid_ascii_shift[128] = {
    [0x04]='A',[0x05]='B',[0x06]='C',[0x07]='D',[0x08]='E',[0x09]='F',
    [0x0A]='G',[0x0B]='H',[0x0C]='I',[0x0D]='J',[0x0E]='K',[0x0F]='L',
    [0x10]='M',[0x11]='N',[0x12]='O',[0x13]='P',[0x14]='Q',[0x15]='R',
    [0x16]='S',[0x17]='T',[0x18]='U',[0x19]='V',[0x1A]='W',[0x1B]='X',
    [0x1C]='Y',[0x1D]='Z',[0x1E]='!',[0x1F]='@',[0x20]='#',[0x21]='$',
    [0x22]='%',[0x23]='^',[0x24]='&',[0x25]='*',[0x26]='(',[0x27]=')',
    [0x28]='\n',[0x29]=27,[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='_',[0x2E]='+',[0x2F]='{',[0x30]='}',[0x31]='|',
    [0x33]=':',[0x34]='"',[0x35]='~',[0x36]='<',[0x37]='>',[0x38]='?',
};

static uint8_t g_prev_keys[6] = {0};

/* HID Usage Code -> PS/2 scancode set 1 (чтобы USB-клавиатура работала
   в Doom, который читает скан-коды из keyboard_pop_scancode) */
static uint8_t hid_to_scancode(uint8_t code) {
    switch (code) {
        case 0x04: return 0x1E; case 0x05: return 0x30;
        case 0x06: return 0x2E; case 0x07: return 0x20;
        case 0x08: return 0x12; case 0x09: return 0x21;
        case 0x0A: return 0x22; case 0x0B: return 0x23;
        case 0x0C: return 0x17; case 0x0D: return 0x24;
        case 0x0E: return 0x25; case 0x0F: return 0x26;
        case 0x10: return 0x32; case 0x11: return 0x31;
        case 0x12: return 0x18; case 0x13: return 0x19;
        case 0x14: return 0x10; case 0x15: return 0x13;
        case 0x16: return 0x1F; case 0x17: return 0x14;
        case 0x18: return 0x16; case 0x19: return 0x2F;
        case 0x1A: return 0x11; case 0x1B: return 0x2D;
        case 0x1C: return 0x15; case 0x1D: return 0x2C;
        case 0x1E: return 0x02; case 0x1F: return 0x03;
        case 0x20: return 0x04; case 0x21: return 0x05;
        case 0x22: return 0x06; case 0x23: return 0x07;
        case 0x24: return 0x08; case 0x25: return 0x09;
        case 0x26: return 0x0A; case 0x27: return 0x0B;
        case 0x28: return 0x1C; case 0x29: return 0x01;
        case 0x2A: return 0x0E; case 0x2B: return 0x0F;
        case 0x2C: return 0x39; case 0x2D: return 0x0C;
        case 0x2E: return 0x0D; case 0x2F: return 0x1A;
        case 0x30: return 0x1B; case 0x33: return 0x27;
        case 0x34: return 0x28; case 0x36: return 0x33;
        case 0x37: return 0x34; case 0x38: return 0x35;
        case 0x4F: return 0x4D; case 0x50: return 0x4B;
        case 0x51: return 0x50; case 0x52: return 0x48;
        case 0xE0: return 0x1D; case 0xE1: return 0x2A;
        case 0xE5: return 0x36; case 0xE2: return 0x38;
        default: return 0;
    }
}

static void process_hid_report(uint8_t *rpt) {
    uint8_t mod   = rpt[0];
    int     shift = (mod & 0x22) != 0;
    int     ctrl  = (mod & 0x11) != 0;

    /* новые нажатия */
    for (int i = 0; i < 6; i++) {
        uint8_t code = rpt[2 + i];
        if (!code) continue;
        int was = 0;
        for (int j = 0; j < 6; j++) if (g_prev_keys[j] == code) { was = 1; break; }
        if (was) continue;

        char c = 0;
        if (ctrl && code >= 0x04 && code <= 0x1D) {
            c = (char)(1 + (code - 0x04));
        } else if (code < 128) {
            c = shift ? g_hid_ascii_shift[code] : g_hid_ascii[code];
        }
        if (c) {
            kbd_push(c);               /* GUI (совместимость) */
            keyboard_push_char(c);     /* shell / sys_read */
        }
        uint8_t sc = hid_to_scancode(code);
        if (sc) keyboard_push_scancode(sc);   /* Doom */
    }

    /* отпускания */
    for (int j = 0; j < 6; j++) {
        uint8_t code = g_prev_keys[j];
        if (!code) continue;
        int still = 0;
        for (int i = 0; i < 6; i++) if (rpt[2 + i] == code) { still = 1; break; }
        if (!still) {
            uint8_t sc = hid_to_scancode(code);
            if (sc) keyboard_push_scancode((uint8_t)(sc | 0x80));
        }
    }

    for (int i = 0; i < 6; i++) g_prev_keys[i] = rpt[2 + i];
}

/* ================================================================
 *  Базовые ring-операции
 * ================================================================ */
static int wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected, int ms) {
    for (volatile uint64_t i = 0; i < (uint64_t)ms * 1000000ULL; i++)
        if ((*reg & mask) == expected) return 0;
    return -1;
}

static void ring_init(xhci_ring_t *ring, xhci_trb_t *mem, uint32_t size) {
    ring->ring = mem;
    ring->size = size;
    ring->enqueue = 0;
    ring->dequeue = 0;
    ring->ccs = 1;
    ring->ccs_event = 1;
    for (uint32_t i = 0; i < size; i++) {
        ring->ring[i].param_lo = 0;
        ring->ring[i].param_hi = 0;
        ring->ring[i].status   = 0;
        ring->ring[i].control  = 0;
    }
    ring->ring[size-1].param_lo = (uint32_t)(uintptr_t)ring->ring;
    ring->ring[size-1].param_hi = (uint32_t)((uint64_t)(uintptr_t)ring->ring >> 32);
    ring->ring[size-1].control  = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
}

/* Принудительный сброс EP0 ring для слота */
static void reset_ep0_ring(int slot_id) {
    ring_init(&g_ep0_ring_structs[slot_id], g_ep0_rings[slot_id], 64);
}

static void advance_event_dequeue(void) {
    g_event_ring.dequeue++;
    if (g_event_ring.dequeue >= g_event_ring.size) {
        g_event_ring.dequeue = 0;
        g_event_ring.ccs_event ^= 1;
    }
    g_ir_base[6] = (uint32_t)(uintptr_t)&g_event_ring.ring[g_event_ring.dequeue] | (1 << 3);
    g_ir_base[7] = (uint32_t)((uint64_t)(uintptr_t)&g_event_ring.ring[g_event_ring.dequeue] >> 32);
}

/* Звонок в doorbell с барьером памяти (важно для QEMU) */
static void doorbell(int slot_id, int dci) {
    __asm__ volatile("sfence" ::: "memory");
    volatile uint32_t *db = (volatile uint32_t *)(g_cap_base +
                            *(volatile uint32_t *)(g_cap_base + XHCI_DBOFF));
    db[slot_id] = (uint32_t)dci;
}

/* Обработать «чужое» событие. Возвращает 1 если пришло want_type, 0 если
 * событий нет, -1 если обработали не то. */
static int consume_non_target_events(uint32_t want_type, int *out_slot, int *out_code) {
    if ((g_event_ring.ring[g_event_ring.dequeue].control & 1) != g_event_ring.ccs_event)
        return 0;

    xhci_trb_t *ev = &g_event_ring.ring[g_event_ring.dequeue];
    uint32_t type = (ev->control >> 10) & 0x3F;

    if (type == want_type) {
        if (out_slot) *out_slot = (ev->control >> 24) & 0xFF;
        if (out_code) *out_code = (ev->status >> 24) & 0xFF;
        advance_event_dequeue();
        return 1;
    }

    if (type == TRB_TYPE_TRANSFER_EVENT) {
        uint32_t slot_id = (ev->control >> 24) & 0xFF;
        uint32_t dci     = (ev->control >> 16) & 0x1F;
        uint32_t code    = (ev->status >> 24) & 0xFF;

        if (g_kbd_slot > 0 && slot_id == (uint32_t)g_kbd_slot &&
            dci == (uint32_t)g_kbd_dci && (code == 1 || code == 13)) {
            process_hid_report(g_kbd_buf);
            xhci_ring_t *r = &g_kbd_ring;
            if (r->enqueue >= r->size - 1) {
                r->ring[r->size-1].control = (TRB_TYPE_LINK << 10) | (1 << 1) | (r->ccs & 1);
                r->enqueue = 0;
                r->ccs ^= 1;
            }
            xhci_trb_t *t = &r->ring[r->enqueue];
            t->param_lo = (uint32_t)(uintptr_t)g_kbd_buf;
            t->param_hi = (uint32_t)((uint64_t)(uintptr_t)g_kbd_buf >> 32);
            t->status   = (8 << 16);
            t->control  = (TRB_TYPE_NORMAL << 10) | TRB_IOC | (r->ccs & 1);
            r->enqueue++;
            doorbell(g_kbd_slot, g_kbd_dci);
        }
    }
    advance_event_dequeue();
    return -1;
}

/* ================================================================
 *  Init контроллера
 * ================================================================ */
int xhci_init(uint64_t mmio_base) {
    if (!mmio_base) return 0;
    g_cap_base = (volatile uint8_t *)mmio_base;
    uint8_t caplength = *(volatile uint8_t *)(g_cap_base + XHCI_CAPLENGTH);
    g_op_base = (volatile uint32_t *)(g_cap_base + caplength);

    uint32_t rtsoff = *(volatile uint32_t *)(g_cap_base + XHCI_RTSOFF);
    g_ir_base = (volatile uint32_t *)(g_cap_base + rtsoff + 0x20);

    uint32_t hcsparams1 = *(volatile uint32_t *)(g_cap_base + XHCI_HCSPARAMS1);
    g_max_slots = hcsparams1 & 0xFF;
    g_max_ports = (hcsparams1 >> 24) & 0xFF;
    logi(S_DRV, "[xHCI] Slots=%d, Ports=%d", g_max_slots, g_max_ports);
    if (!g_max_slots || !g_max_ports) return 0;

    g_op_base[XHCI_USBCMD / 4] |= XHCI_CMD_HCRST;
    if (wait_bits(&g_op_base[XHCI_USBCMD / 4], XHCI_CMD_HCRST, 0, 100) != 0) return 0;
    if (wait_bits(&g_op_base[XHCI_USBSTS / 4], XHCI_STS_CNR, 0, 100) != 0) return 0;

    for (int i = 0; i < 256; i++) g_dcbaa[i] = 0;
    g_op_base[XHCI_DCBAAP_LO / 4] = (uint32_t)(uintptr_t)g_dcbaa;
    g_op_base[XHCI_DCBAAP_HI / 4] = (uint32_t)((uint64_t)(uintptr_t)g_dcbaa >> 32);

    ring_init(&g_cmd_ring, g_cmd_ring_mem, 256);
    g_op_base[XHCI_CRCR_LO / 4] = (uint32_t)(uintptr_t)g_cmd_ring.ring | 1;
    g_op_base[XHCI_CRCR_HI / 4] = (uint32_t)((uint64_t)(uintptr_t)g_cmd_ring.ring >> 32);

    ring_init(&g_event_ring, g_event_ring_mem, 256);
    g_erst[0].ring_segment_base = (uint64_t)(uintptr_t)g_event_ring.ring;
    g_erst[0].ring_segment_size = g_event_ring.size;
    g_erst[0].reserved = 0;

    g_ir_base[0] = (1 << 1);
    g_ir_base[2] = 1;
    g_ir_base[4] = (uint32_t)(uintptr_t)g_erst;
    g_ir_base[5] = (uint32_t)((uint64_t)(uintptr_t)g_erst >> 32);
    g_ir_base[6] = (uint32_t)(uintptr_t)g_event_ring.ring | (1 << 3);
    g_ir_base[7] = (uint32_t)((uint64_t)(uintptr_t)g_event_ring.ring >> 32);

    g_op_base[XHCI_CONFIG / 4] = g_max_slots;
    g_op_base[XHCI_USBCMD / 4] |= XHCI_CMD_RUN | XHCI_CMD_INTE;
    if (wait_bits(&g_op_base[XHCI_USBSTS / 4], XHCI_STS_HCH, 0, 100) != 0) return 0;

    for (int i = 1; i <= g_max_ports; i++) {
        uint32_t portsc = g_op_base[XHCI_PORTSC(i) / 4];
        g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PP;
    }
    logi(S_DRV, "[xHCI] Controller running.");
    return 1;
}

/* ================================================================
 *  Commands
 * ================================================================ */
static int send_cmd_ex(xhci_trb_t *trb, int *out_slot) {
    xhci_ring_t *r = &g_cmd_ring;
    r->ring[r->enqueue] = *trb;
    r->ring[r->enqueue].control |= (r->ccs & 1);
    r->enqueue++;
    if (r->enqueue >= r->size - 1) {
        r->ring[r->enqueue].control = (TRB_TYPE_LINK << 10) | (1 << 1) | (r->ccs & 1);
        r->enqueue = 0;
        r->ccs ^= 1;
    }
    doorbell(0, 0);

    for (int t = 0; t < 50000; t++) {
        if ((g_event_ring.ring[g_event_ring.dequeue].control & 1) == g_event_ring.ccs_event) {
            xhci_trb_t *ev = &g_event_ring.ring[g_event_ring.dequeue];
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == TRB_TYPE_CMD_COMPLETE) {
                int code = (ev->status >> 24) & 0xFF;
                int slot = (ev->control >> 24) & 0xFF;
                advance_event_dequeue();
                if (code == 1) { if (out_slot) *out_slot = slot; return 1; }
                logi(S_DRV, "[xHCI] Cmd failed, code=%d", code);
                return 0;
            }
            consume_non_target_events(TRB_TYPE_CMD_COMPLETE, 0, 0);
        } else {
            for (volatile int j = 0; j < 1000; j++);
        }
    }
    logi(S_DRV, "[xHCI] Command timed out!");
    return 0;
}

/* ================================================================
 *  Address Device
 * ================================================================ */
int xhci_address_device(int port, uint8_t speed) {
    xhci_trb_t trb = {0};
    trb.control = (TRB_TYPE_ENABLE_SLOT << 10) | TRB_IOC;
    int slot_id = 0;
    if (!send_cmd_ex(&trb, &slot_id) || slot_id <= 0) {
        logi(S_DRV, "[xHCI] Enable Slot failed");
        return -1;
    }
    logi(S_DRV, "[xHCI] Slot ID = %d", slot_id);

    /* КРИТИЧНО: полностью сбрасываем EP0 ring для этого слота */
    reset_ep0_ring(slot_id);

    for (uint32_t i = 0; i < sizeof(xhci_input_ctx_t) / 4; i++)
        ((uint32_t *)&g_input_ctx)[i] = 0;

    g_input_ctx.add_flags = (1u << 0) | (1u << 1);
    g_input_ctx.slot.dword0 = (speed << 20) | (1u << 27);
    g_input_ctx.slot.dword1 = (port << 16);

    uint32_t max_packet = (speed == 3) ? 64 : 8;
    g_input_ctx.eps[0].dword1 = (max_packet << 16) | (3 << 1) | (EP_TYPE_CONTROL << 3);
    g_input_ctx.eps[0].dword2 = (uint32_t)(uintptr_t)g_ep0_ring_structs[slot_id].ring | 1;
    g_input_ctx.eps[0].dword3 = (uint32_t)((uint64_t)(uintptr_t)g_ep0_ring_structs[slot_id].ring >> 32);

    g_dcbaa[slot_id] = (uint64_t)(uintptr_t)&g_dev_ctxs[slot_id];

    xhci_trb_t addr = {0};
    addr.param_lo = (uint32_t)(uintptr_t)&g_input_ctx;
    addr.param_hi = (uint32_t)((uint64_t)(uintptr_t)&g_input_ctx >> 32);
    addr.control  = (TRB_TYPE_ADDRESS_DEV << 10) | TRB_IOC | (slot_id << 24);
    int got_slot = 0;
    if (!send_cmd_ex(&addr, &got_slot)) return -1;

    logi(S_DRV, "[xHCI] Device addressed on slot %d!", slot_id);
    return slot_id;
}

/* ================================================================
 *  Control transfer (IOC ТОЛЬКО на STATUS TRB)
 * ================================================================ */
int xhci_control_transfer(int slot_id, uint8_t *setup, uint8_t *data, uint32_t len, int direction_in) {
    xhci_ring_t *r = &g_ep0_ring_structs[slot_id];

    /* Защита: ring должен быть чистым перед новым трансфером */
    if (r->enqueue != 0) {
        ring_init(r, r->ring, r->size);
    }

    /* Setup TRB (без IOC) */
    xhci_trb_t *s = &r->ring[r->enqueue];
    s->param_lo = *(uint32_t *)&setup[0];
    s->param_hi = *(uint32_t *)&setup[4];
    s->status   = (8 << 16);
    s->control  = (TRB_TYPE_SETUP << 10) | TRB_IDT | (r->ccs & 1);
    r->enqueue++;

    if (len > 0 && data) {
        /* Data TRB (без IOC) */
        xhci_trb_t *d = &r->ring[r->enqueue];
        d->param_lo = (uint32_t)(uintptr_t)data;
        d->param_hi = (uint32_t)((uint64_t)(uintptr_t)data >> 32);
        d->status   = (len << 16);
        uint32_t dir = direction_in ? (1 << 16) : 0;
        d->control  = (TRB_TYPE_DATA << 10) | dir | (r->ccs & 1);
        r->enqueue++;
    }

    /* Status TRB (IOC) */
    xhci_trb_t *st = &r->ring[r->enqueue];
    st->param_lo = 0;
    st->param_hi = 0;
    st->status   = 0;
    uint32_t sdir = direction_in ? 0 : (1 << 16);
    st->control  = (TRB_TYPE_STATUS << 10) | TRB_IOC | sdir | (r->ccs & 1);
    r->enqueue++;

    doorbell(slot_id, 1);

    for (int t = 0; t < 50000; t++) {
        if ((g_event_ring.ring[g_event_ring.dequeue].control & 1) == g_event_ring.ccs_event) {
            xhci_trb_t *ev = &g_event_ring.ring[g_event_ring.dequeue];
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint32_t slot = (ev->control >> 24) & 0xFF;
                uint32_t dci  = (ev->control >> 16) & 0x1F;
                uint32_t code = (ev->status >> 24) & 0xFF;

                if (slot == (uint32_t)slot_id && dci == 1) {
                    advance_event_dequeue();
                    if (code == 1 || code == 13) return 1;
                    logi(S_DRV, "[xHCI] Transfer failed, code=%d", code);
                    return 0;
                }
            }
            consume_non_target_events(TRB_TYPE_TRANSFER_EVENT, 0, 0);
        } else {
            for (volatile int j = 0; j < 1000; j++);
        }
    }
    logi(S_DRV, "[xHCI] Transfer timed out!");
    return 0;
}

int xhci_get_descriptor(int slot_id, uint8_t type, uint8_t index, uint16_t lang, void *buffer, uint32_t len) {
    (void)index;
    uint8_t setup[8] = {0};
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0; setup[3] = type;
    setup[4] = (uint8_t)(lang & 0xFF); setup[5] = (uint8_t)(lang >> 8);
    setup[6] = (uint8_t)(len & 0xFF); setup[7] = (uint8_t)(len >> 8);
    return xhci_control_transfer(slot_id, setup, (uint8_t *)buffer, len, 1);
}

int xhci_set_configuration(int slot_id, uint8_t config_value) {
    uint8_t setup[8] = {0};
    setup[0] = 0x00;
    setup[1] = USB_REQ_SET_CONFIGURATION;
    setup[2] = config_value; setup[3] = 0;
    return xhci_control_transfer(slot_id, setup, 0, 0, 0);
}

/* ================================================================
 *  HID: SET_PROTOCOL(Boot) / SET_IDLE
 * ================================================================ */
static int hid_set_protocol(int slot_id, int iface_num) {
    uint8_t setup[8] = {0x21, 0x0B, 0x00, 0x00, (uint8_t)iface_num, 0, 0, 0};
    return xhci_control_transfer(slot_id, setup, 0, 0, 0);
}
static int hid_set_idle(int slot_id, int iface_num) {
    uint8_t setup[8] = {0x21, 0x0A, 0x00, 0x00, (uint8_t)iface_num, 0, 0, 0};
    return xhci_control_transfer(slot_id, setup, 0, 0, 0);
}

/* ================================================================
 *  Configure Endpoint для клавиатуры
 * ================================================================ */
static int xhci_configure_keyboard_ep(int slot_id, int ep_num, int mps, int interval) {
    int dci = XHCI_EP_DCI(ep_num, 1);

    for (uint32_t i = 0; i < sizeof(xhci_input_ctx_t) / 4; i++)
        ((uint32_t *)&g_input_ctx)[i] = 0;

    g_input_ctx.add_flags = (1u << dci);
    g_input_ctx.slot.dword0 = g_dev_ctxs[slot_id].slot.dword0;
    XHCI_SLOT_CTX_SET_ENTRIES(g_input_ctx.slot.dword0, dci);
    g_input_ctx.slot.dword1 = g_dev_ctxs[slot_id].slot.dword1;

    g_input_ctx.eps[dci - 1].dword0 = ((uint32_t)interval << 16);
    g_input_ctx.eps[dci - 1].dword1 =
        ((uint32_t)mps << 16) | (3 << 1) | (EP_TYPE_INTERRUPT_IN << 3);
    g_input_ctx.eps[dci - 1].dword2 =
        (uint32_t)(uintptr_t)g_kbd_ring.ring | 1;
    g_input_ctx.eps[dci - 1].dword3 =
        (uint32_t)((uint64_t)(uintptr_t)g_kbd_ring.ring >> 32);
    g_input_ctx.eps[dci - 1].dword4 = 8;

    xhci_trb_t trb = {0};
    trb.param_lo = (uint32_t)(uintptr_t)&g_input_ctx;
    trb.param_hi = (uint32_t)((uint64_t)(uintptr_t)&g_input_ctx >> 32);
    trb.control  = (TRB_TYPE_CONFIGURE_EP << 10) | TRB_IOC | (slot_id << 24);

    int got = 0;
    return send_cmd_ex(&trb, &got);
}

/* ================================================================
 *  Полная инициализация HID-клавиатуры
 * ================================================================ */
static void setup_keyboard(int slot_id, const usb_interface_desc_t *iface,
                           const usb_endpoint_desc_t *ep)
{
    int ep_num   = ep->bEndpointAddress & 0x0F;
    int ep_dci   = XHCI_EP_DCI(ep_num, 1);
    int mps      = ep->wMaxPacketSize & 0x7FF;
    int interval = ep->bInterval ? ep->bInterval : 6;

    ring_init(&g_kbd_ring, g_kbd_ring_mem, 32);

    if (!xhci_configure_keyboard_ep(slot_id, ep_num, mps, interval)) {
        logi(S_DRV, "[USB] Configure Endpoint failed");
        return;
    }

    hid_set_protocol(slot_id, iface->bInterfaceNumber);
    hid_set_idle(slot_id, iface->bInterfaceNumber);

    g_kbd_slot = slot_id;
    g_kbd_dci  = ep_dci;
    for (int i = 0; i < 6; i++) g_prev_keys[i] = 0;

    xhci_ring_t *r = &g_kbd_ring;
    xhci_trb_t *t = &r->ring[r->enqueue];
    t->param_lo = (uint32_t)(uintptr_t)g_kbd_buf;
    t->param_hi = (uint32_t)((uint64_t)(uintptr_t)g_kbd_buf >> 32);
    t->status   = (8 << 16);
    t->control  = (TRB_TYPE_NORMAL << 10) | TRB_IOC | (r->ccs & 1);
    r->enqueue++;
    doorbell(slot_id, ep_dci);

    logi(S_DRV, "[USB] Keyboard ready (slot=%d, dci=%d, mps=%d, int=%d)",
         slot_id, ep_dci, mps, interval);
}

/* ================================================================
 *  Разбор конфигурации и поиск HID-клавиатуры
 * ================================================================ */
static int try_enable_hid_keyboard(int slot_id, uint16_t total_len) {
    if (total_len == 0 || total_len > 512) return 0;
    uint8_t *buf = (uint8_t *)kmalloc(total_len);
    if (!buf) return 0;

    if (!xhci_get_descriptor(slot_id, USB_DESC_CONFIG, 0, 0, buf, total_len)) {
        kfree(buf);
        return 0;
    }

    usb_interface_desc_t *cur_iface = 0;
    usb_endpoint_desc_t  *kbd_ep   = 0;
    usb_interface_desc_t *kbd_if   = 0;

    int off = 0;
    while (off + 2 <= (int)total_len) {
        uint8_t dlen  = buf[off];
        uint8_t dtype = buf[off + 1];
        if (dlen < 2) break;

        if (dtype == USB_DESC_INTERFACE && dlen >= 9) {
            usb_interface_desc_t *ifc = (usb_interface_desc_t *)(buf + off);
            cur_iface = ifc;
            if (ifc->bInterfaceClass == 0x03 &&
                ifc->bInterfaceSubClass == 1 &&
                ifc->bInterfaceProtocol == 1)
            {
                kbd_if = ifc;
                kbd_ep = 0;
            }
        } else if (dtype == USB_DESC_ENDPOINT && dlen >= 7 && cur_iface) {
            usb_endpoint_desc_t *epd = (usb_endpoint_desc_t *)(buf + off);
            if (cur_iface->bInterfaceClass == 0x03 &&
                (epd->bEndpointAddress & 0x80) &&
                (epd->bmAttributes & 0x03) == 3)
            {
                kbd_ep = epd;
                kbd_if = cur_iface;
            }
        }
        off += dlen;
    }

    int ok = 0;
    if (kbd_if && kbd_ep) {
        setup_keyboard(slot_id, kbd_if, kbd_ep);
        ok = 1;
    } else {
        logi(S_DRV, "[USB] HID keyboard interface/endpoint not found");
    }

    kfree(buf);
    return ok;
}

/* ================================================================
 *  Отложенный Address Device (>=10 мс после сброса)
 * ================================================================ */
static int      g_pending_addr_port[256]  = {0};
static uint64_t g_pending_addr_tick[256]  = {0};
static uint8_t  g_pending_addr_speed[256] = {0};

/* ================================================================
 *  Poll
 * ================================================================ */
void xhci_poll(void) {
    if (!g_op_base) return;

    /* Сохранить IF и запретить прерывания на время работы */
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    __asm__ volatile("cli");

    /* 1) Drain event ring */
    int guard = 0;
    while (guard++ < 64) {
        if ((g_event_ring.ring[g_event_ring.dequeue].control & 1) != g_event_ring.ccs_event)
            break;
        consume_non_target_events(0xFFFFFFFFu, 0, 0);
    }

    /* 2) Отложенные Address Device (прошло >=1 тик = 10 мс) */
    for (int i = 1; i <= g_max_ports; i++) {
        if (!g_pending_addr_port[i]) continue;
        if (pit_ticks() - g_pending_addr_tick[i] < 1) continue;

        g_pending_addr_port[i] = 0;

        uint32_t portsc = g_op_base[XHCI_PORTSC(i) / 4];
        if (!(portsc & XHCI_PORTSC_CCS)) {
            logi(S_DRV, "[xHCI] Port %d: device disappeared before address", i);
            continue;
        }

        int slot = xhci_address_device(i, g_pending_addr_speed[i]);
        if (slot > 0) {
            usb_device_desc_t dev;
            if (xhci_get_descriptor(slot, USB_DESC_DEVICE, 0, 0, &dev, sizeof(dev))) {
                logi(S_DRV, "[USB] Device: VID=0x%x PID=0x%x Class=0x%x",
                     dev.idVendor, dev.idProduct, dev.bDeviceClass);

                usb_config_desc_t cfg;
                if (xhci_get_descriptor(slot, USB_DESC_CONFIG, 0, 0, &cfg, sizeof(cfg))) {
                    logi(S_DRV, "[USB] Config: %d interfaces", cfg.bNumInterfaces);
                    xhci_set_configuration(slot, cfg.bConfigurationValue);
                    try_enable_hid_keyboard(slot, cfg.wTotalLength);
                }
            }
        }
    }

    /* 3) Скан портов */
    for (int i = 1; i <= g_max_ports; i++) {
        uint32_t portsc = g_op_base[XHCI_PORTSC(i) / 4];

        if (g_pending_addr_port[i]) continue;

        if ((portsc & XHCI_PORTSC_CCS) && !(portsc & XHCI_PORTSC_PED)) {
            logi(S_DRV, "[xHCI] Device on port %d, reset", i);
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PR;
            for (volatile int t = 0; t < 1000000; t++) {
                portsc = g_op_base[XHCI_PORTSC(i) / 4];
                if ((portsc & XHCI_PORTSC_PRC) || !(portsc & XHCI_PORTSC_PR)) break;
            }
            portsc = g_op_base[XHCI_PORTSC(i) / 4];
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PRC;

            g_pending_addr_port[i]  = 1;
            g_pending_addr_tick[i]  = pit_ticks();
            g_pending_addr_speed[i] = (portsc >> 10) & 0xF;
        }

        if (portsc & XHCI_PORTSC_CSC)
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_CSC;
    }

    if (flags & 0x200) __asm__ volatile("sti");
}