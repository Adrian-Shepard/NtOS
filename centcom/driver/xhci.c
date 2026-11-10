#include "xhci.h"
#include "usb.h"
#include "../log/log.h"

static volatile uint8_t *g_cap_base = 0;
static volatile uint32_t *g_op_base = 0;
static volatile uint32_t *g_ir_base = 0;

static xhci_trb_t g_cmd_ring_mem[256] __attribute__((aligned(64)));
static xhci_trb_t g_event_ring_mem[256] __attribute__((aligned(64)));
static uint64_t g_dcbaa[256] __attribute__((aligned(64)));
static xhci_erst_entry_t g_erst[1] __attribute__((aligned(64)));

static xhci_ring_t g_cmd_ring;
static xhci_ring_t g_event_ring;
static uint8_t g_max_slots = 0;
static int g_max_ports = 0;

static xhci_device_ctx_t g_dev_ctxs[8] __attribute__((aligned(64)));
static xhci_input_ctx_t  g_input_ctx __attribute__((aligned(64)));

static xhci_trb_t g_ep0_rings[8][64] __attribute__((aligned(64)));
static xhci_ring_t g_ep0_ring_structs[8];

static int g_debug_poll_count = 0;

static int wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected, int ms) {
    for (volatile uint64_t i = 0; i < (uint64_t)ms * 1000000; i++) {
        if ((*reg & mask) == expected) return 0;
    }
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
        ring->ring[i].status = 0;
        ring->ring[i].control = 0;
    }
    ring->ring[size - 1].param_lo = (uint32_t)(uintptr_t)ring->ring;
    ring->ring[size - 1].param_hi = (uint32_t)((uint64_t)(uintptr_t)ring->ring >> 32);
    ring->ring[size - 1].control = (TRB_TYPE_LINK << 10) | (1 << 1) | 1;
}

int xhci_init(uint64_t mmio_base) {
    if (mmio_base == 0) return 0;
    g_cap_base = (volatile uint8_t *)mmio_base;
    uint8_t caplength = *(volatile uint8_t *)(g_cap_base + XHCI_CAPLENGTH);
    g_op_base = (volatile uint32_t *)(g_cap_base + caplength);
    
    uint32_t rtsoff = *(volatile uint32_t *)(g_cap_base + XHCI_RTSOFF);
    g_ir_base = (volatile uint32_t *)(g_cap_base + rtsoff + 0x20);

    uint32_t hcsparams1 = *(volatile uint32_t *)(g_cap_base + XHCI_HCSPARAMS1);
    g_max_slots = hcsparams1 & 0xFF;
    g_max_ports = (hcsparams1 >> 24) & 0xFF;
    logi(S_DRV, "[xHCI] Slots=%d, Ports=%d", g_max_slots, g_max_ports);
    if (g_max_slots == 0 || g_max_ports == 0) return 0;

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

static int send_cmd(xhci_trb_t *trb) {
    xhci_ring_t *r = &g_cmd_ring;
    r->ring[r->enqueue] = *trb;
    r->ring[r->enqueue].control |= (r->ccs & 1);
    r->enqueue++;
    if (r->enqueue >= r->size - 1) {
        r->ring[r->enqueue].control = (TRB_TYPE_LINK << 10) | (1 << 1) | (r->ccs & 1);
        r->enqueue = 0;
        r->ccs ^= 1;
    }
    volatile uint32_t *db = (volatile uint32_t *)(g_cap_base + *(volatile uint32_t *)(g_cap_base + XHCI_DBOFF));
    db[0] = 0;

    for (int timeout = 0; timeout < 5000; timeout++) {
        xhci_trb_t *ev = &g_event_ring.ring[g_event_ring.dequeue];
        if ((ev->control & 1) == g_event_ring.ccs_event) {
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == TRB_TYPE_CMD_COMPLETE) {
                int code = (ev->status >> 24) & 0xFF;
                uint32_t slot_id = (ev->control >> 24) & 0xFF;
                g_event_ring.dequeue++;
                if (g_event_ring.dequeue >= g_event_ring.size) {
                    g_event_ring.dequeue = 0;
                    g_event_ring.ccs_event ^= 1;
                }
                g_ir_base[6] = (uint32_t)(uintptr_t)&g_event_ring.ring[g_event_ring.dequeue] | (1 << 3);
                g_ir_base[7] = (uint32_t)((uint64_t)(uintptr_t)&g_event_ring.ring[g_event_ring.dequeue] >> 32);
                if (code == 1) return slot_id;
                else { logi(S_DRV, "[xHCI] Cmd failed, code=%d", code); return -1; }
            }
        }
        for (volatile int j = 0; j < 1000; j++);
    }
    return -1;
}

int xhci_address_device(int port, uint8_t speed) {
    xhci_trb_t trb = {0};
    trb.control = (TRB_TYPE_ENABLE_SLOT << 10) | TRB_IOC;
    int slot_id = send_cmd(&trb);
    if (slot_id < 0) return 0;
    logi(S_DRV, "[xHCI] Slot ID = %d", slot_id);

    for (uint32_t i = 0; i < sizeof(xhci_input_ctx_t)/4; i++) ((uint32_t*)&g_input_ctx)[i] = 0;
    g_input_ctx.add_flags = (1 << 0) | (1 << 1); 
    g_input_ctx.slot.dword0 = (speed << 20) | (1 << 27);
    g_input_ctx.slot.dword1 = (port << 16);

    uint32_t max_packet = (speed == 3) ? 64 : 8;
    g_input_ctx.eps[0].dword1 = (max_packet << 16) | (3 << 1);
    
    ring_init(&g_ep0_ring_structs[slot_id], g_ep0_rings[slot_id], 64);
    g_input_ctx.eps[0].dword2 = (uint32_t)(uintptr_t)g_ep0_ring_structs[slot_id].ring | 1;
    g_input_ctx.eps[0].dword3 = (uint32_t)((uint64_t)(uintptr_t)g_ep0_ring_structs[slot_id].ring >> 32);

    g_dcbaa[slot_id] = (uint64_t)(uintptr_t)&g_dev_ctxs[slot_id];

    xhci_trb_t addr_trb = {0};
    addr_trb.param_lo = (uint32_t)(uintptr_t)&g_input_ctx;
    addr_trb.param_hi = (uint32_t)((uint64_t)(uintptr_t)&g_input_ctx >> 32);
    addr_trb.control = (TRB_TYPE_ADDRESS_DEV << 10) | TRB_IOC | (slot_id << 24);
    if (send_cmd(&addr_trb) < 0) return 0;

    logi(S_DRV, "[xHCI] Device addressed on slot %d!", slot_id);
    return slot_id;
}

int xhci_control_transfer(int slot_id, uint8_t *setup, uint8_t *data, uint32_t len, int direction_in) {
    xhci_ring_t *r = &g_ep0_ring_structs[slot_id];
    
    xhci_trb_t *setup_trb = &r->ring[r->enqueue];
    setup_trb->param_lo = *(uint32_t*)&setup[0];
    setup_trb->param_hi = *(uint32_t*)&setup[4];
    setup_trb->status = (8 << 16); 
    setup_trb->control = (TRB_TYPE_SETUP << 10) | TRB_IOC | TRB_IDT | (r->ccs & 1);
    r->enqueue = (r->enqueue + 1) % (r->size - 1);

    if (len > 0 && data != 0) {
        xhci_trb_t *data_trb = &r->ring[r->enqueue];
        data_trb->param_lo = (uint32_t)(uintptr_t)data;
        data_trb->param_hi = (uint32_t)((uint64_t)(uintptr_t)data >> 32);
        data_trb->status = (len << 16);
        uint32_t dir_bit = direction_in ? (1 << 16) : 0;
        data_trb->control = (TRB_TYPE_DATA << 10) | TRB_IOC | dir_bit | (r->ccs & 1);
        r->enqueue = (r->enqueue + 1) % (r->size - 1);
    }

    xhci_trb_t *status_trb = &r->ring[r->enqueue];
    status_trb->param_lo = 0;
    status_trb->param_hi = 0;
    status_trb->status = 0;
    uint32_t status_dir = direction_in ? 0 : (1 << 16);
    status_trb->control = (TRB_TYPE_STATUS << 10) | TRB_IOC | status_dir | (r->ccs & 1);
    r->enqueue = (r->enqueue + 1) % (r->size - 1);

    volatile uint32_t *db = (volatile uint32_t *)(g_cap_base + *(volatile uint32_t *)(g_cap_base + XHCI_DBOFF));
    db[slot_id] = 1;

    for (int timeout = 0; timeout < 5000; timeout++) {
        xhci_trb_t *ev = &g_event_ring.ring[g_event_ring.dequeue];
        if ((ev->control & 1) == g_event_ring.ccs_event) {
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == TRB_TYPE_TRANSFER_EVENT) {
                int code = (ev->status >> 24) & 0xFF;
                g_event_ring.dequeue++;
                if (g_event_ring.dequeue >= g_event_ring.size) {
                    g_event_ring.dequeue = 0;
                    g_event_ring.ccs_event ^= 1;
                }
                g_ir_base[6] = (uint32_t)(uintptr_t)&g_event_ring.ring[g_event_ring.dequeue] | (1 << 3);
                g_ir_base[7] = (uint32_t)((uint64_t)(uintptr_t)&g_event_ring.ring[g_event_ring.dequeue] >> 32);
                if (code == 1 || code == 13) return 1;
                else { logi(S_DRV, "[xHCI] Transfer failed, code=%d", code); return 0; }
            }
        }
        for (volatile int j = 0; j < 1000; j++);
    }
    logi(S_DRV, "[xHCI] Transfer timed out!");
    return 0;
}

int xhci_get_descriptor(int slot_id, uint8_t type, uint8_t index, uint16_t lang, void *buffer, uint32_t len) {
    (void)index;
    uint8_t setup[8];
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0; setup[3] = type;
    setup[4] = (uint8_t)(lang & 0xFF); setup[5] = (uint8_t)(lang >> 8);
    setup[6] = (uint8_t)(len & 0xFF); setup[7] = (uint8_t)(len >> 8);
    return xhci_control_transfer(slot_id, setup, (uint8_t*)buffer, len, 1);
}

int xhci_set_configuration(int slot_id, uint8_t config_value) {
    uint8_t setup[8];
    setup[0] = 0x00;
    setup[1] = USB_REQ_SET_CONFIGURATION;
    setup[2] = config_value; setup[3] = 0;
    setup[4] = 0; setup[5] = 0;
    setup[6] = 0; setup[7] = 0;
    return xhci_control_transfer(slot_id, setup, 0, 0, 0);
}

// --- Poll ports с отладкой ---
void xhci_poll(void) {
    if (!g_op_base) {
        logi(S_DRV, "[xHCI] poll: g_op_base is NULL!");
        return;
    }

    // Отладка: показываем только первые 3 вызова
    if (g_debug_poll_count < 3) {
        logi(S_DRV, "[xHCI] poll #%d called, ports=%d", g_debug_poll_count + 1, g_max_ports);
        g_debug_poll_count++;
    }

    for (int i = 1; i <= g_max_ports; i++) {
        uint32_t portsc = g_op_base[XHCI_PORTSC(i) / 4];
        
        // Отладка: выводим PORTSC для всех портов при первых вызовах
        if (g_debug_poll_count <= 3) {
            logi(S_DRV, "[xHCI] port %d: PORTSC=0x%x", i, portsc);
        }
        
        if ((portsc & XHCI_PORTSC_CCS) && !(portsc & XHCI_PORTSC_PED)) {
            logi(S_DRV, "[xHCI] Device connected on port %d. Resetting...", i);
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PR;
            for (volatile int t = 0; t < 1000000; t++) {
                portsc = g_op_base[XHCI_PORTSC(i) / 4];
                if ((portsc & XHCI_PORTSC_PRC) || !(portsc & XHCI_PORTSC_PR)) break;
            }
            portsc = g_op_base[XHCI_PORTSC(i) / 4];
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PRC;
            uint8_t speed = (portsc >> 10) & 0xF;
            logi(S_DRV, "[xHCI] Port %d reset complete. Speed=%d", i, speed);
            
            int slot_id = xhci_address_device(i, speed);
            if (slot_id > 0) {
                usb_device_desc_t dev_desc;
                if (xhci_get_descriptor(slot_id, USB_DESC_DEVICE, 0, 0, &dev_desc, sizeof(dev_desc))) {
                    logi(S_DRV, "[USB] Device: VID=0x%x PID=0x%x Class=0x%x", 
                         dev_desc.idVendor, dev_desc.idProduct, dev_desc.bDeviceClass);
                    
                    usb_config_desc_t cfg_desc;
                    if (xhci_get_descriptor(slot_id, USB_DESC_CONFIG, 0, 0, &cfg_desc, sizeof(cfg_desc))) {
                        logi(S_DRV, "[USB] Config: %d interfaces", cfg_desc.bNumInterfaces);
                        xhci_set_configuration(slot_id, cfg_desc.bConfigurationValue);
                        logi(S_DRV, "[USB] Configuration set!");
                    }
                }
            }
        }
        if (portsc & XHCI_PORTSC_CSC) {
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_CSC;
        }
    }
}