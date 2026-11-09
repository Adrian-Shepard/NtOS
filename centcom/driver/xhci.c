#include "xhci.h"
#include "usb.h"
#include "../core/io.h"
#include "../log/log.h"
#include "../mem/heap.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"
#include "../driver/mouse.h"
#include "../driver/keyboard.h"

static volatile uint8_t *g_cap_base = 0;
static volatile uint32_t *g_op_base = 0;
static uint64_t g_mmio_base = 0;

// Статические буферы для колец и структур (выровнены на 64 байта)
static xhci_trb_t g_cmd_ring_mem[256] __attribute__((aligned(64)));
static xhci_trb_t g_event_ring_mem[256] __attribute__((aligned(64)));
static uint64_t g_dcbaa[256] __attribute__((aligned(64)));
static xhci_erst_entry_t g_erst[1] __attribute__((aligned(64)));

static xhci_ring_t g_cmd_ring;
static xhci_ring_t g_event_ring;
static uint8_t g_max_slots = 0;
static int g_max_ports = 0;

// Ожидание битов в регистре
static int wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t expected, int ms) {
    for (volatile uint64_t i = 0; i < (uint64_t)ms * 1000000; i++) {
        if ((*reg & mask) == expected) return 0;
    }
    return -1;
}

// Инициализация кольца с использованием готового буфера
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
    
    // Link TRB (замыкаем кольцо)
    ring->ring[size - 1].param_lo = (uint32_t)(uintptr_t)ring->ring;
    ring->ring[size - 1].param_hi = (uint32_t)((uint64_t)(uintptr_t)ring->ring >> 32);
    ring->ring[size - 1].control = (TRB_TYPE_LINK << 10) | (1 << 1); // Link, Toggle Cycle
}

int xhci_init(uint64_t mmio_base) {
    // ЗАЩИТА: если адрес 0, даже не пытаемся читать память
    if (mmio_base == 0) {
        logi(S_DRV, "[xHCI] ERROR: mmio_base is 0, aborting.");
        return 0;
    }

    g_mmio_base = mmio_base;
    g_cap_base = (volatile uint8_t *)mmio_base;
    
    if (!g_cap_base) {
        logi(S_DRV, "[xHCI] ERROR: g_cap_base is NULL");
        return 0;
    }

    logi(S_DRV, "[xHCI] Step 1: Reading CAPLENGTH");
    uint8_t caplength = *(volatile uint8_t *)(g_cap_base + XHCI_CAPLENGTH);
    g_op_base = (volatile uint32_t *)(g_cap_base + caplength);

    logi(S_DRV, "[xHCI] Step 2: Reading HCSPARAMS1");
    uint32_t hcsparams1 = *(volatile uint32_t *)(g_cap_base + XHCI_HCSPARAMS1);

    g_max_slots = hcsparams1 & 0xFF;
    g_max_ports = (hcsparams1 >> 24) & 0xFF;

    logi(S_DRV, "[xHCI] CAPLENGTH=%d HCSPARAMS1=0x%x", caplength, hcsparams1);
    logi(S_DRV, "[xHCI] Slots=%d, Ports=%d", g_max_slots, g_max_ports);

    if (g_max_slots == 0 || g_max_ports == 0) {
        logi(S_DRV, "[xHCI] ERROR: Invalid controller parameters");
        return 0;
    }

    logi(S_DRV, "[xHCI] Step 3: Resetting controller");
    g_op_base[XHCI_USBCMD / 4] |= XHCI_CMD_HCRST;
    if (wait_bits(&g_op_base[XHCI_USBCMD / 4], XHCI_CMD_HCRST, 0, 100) != 0) {
        logi(S_DRV, "[xHCI] ERROR: Reset timeout (USBCMD)");
        return 0;
    }
    if (wait_bits(&g_op_base[XHCI_USBSTS / 4], XHCI_STS_CNR, 0, 100) != 0) {
        logi(S_DRV, "[xHCI] ERROR: Reset timeout (USBSTS)");
        return 0;
    }

    logi(S_DRV, "[xHCI] Step 4: Setting up DCBAAP");
    for (int i = 0; i < 256; i++) g_dcbaa[i] = 0;
    g_op_base[XHCI_DCBAAP_LO / 4] = (uint32_t)(uintptr_t)g_dcbaa;
    g_op_base[XHCI_DCBAAP_HI / 4] = (uint32_t)((uint64_t)(uintptr_t)g_dcbaa >> 32);

    logi(S_DRV, "[xHCI] Step 5: Setting up Command Ring");
    ring_init(&g_cmd_ring, g_cmd_ring_mem, 256);
    g_op_base[XHCI_CRCR_LO / 4] = (uint32_t)(uintptr_t)g_cmd_ring.ring | 1;
    g_op_base[XHCI_CRCR_HI / 4] = (uint32_t)((uint64_t)(uintptr_t)g_cmd_ring.ring >> 32);

    logi(S_DRV, "[xHCI] Step 6: Setting up Event Ring");
    ring_init(&g_event_ring, g_event_ring_mem, 256);
    g_erst[0].ring_segment_base = (uint64_t)(uintptr_t)g_event_ring.ring;
    g_erst[0].ring_segment_size = g_event_ring.size;
    g_erst[0].reserved = 0;

    volatile uint32_t *ir_base = (volatile uint32_t *)(g_cap_base + *(volatile uint32_t *)(g_cap_base + XHCI_RTSOFF));
    ir_base[1] = 1;
    ir_base[2] = (uint32_t)(uintptr_t)g_erst;
    ir_base[3] = (uint32_t)((uint64_t)(uintptr_t)g_erst >> 32);
    ir_base[4] = (uint32_t)(uintptr_t)g_event_ring.ring;
    ir_base[5] = (uint32_t)((uint64_t)(uintptr_t)g_event_ring.ring >> 32);

    logi(S_DRV, "[xHCI] Step 7: Configuring Max Slots");
    g_op_base[XHCI_CONFIG / 4] = g_max_slots;

    logi(S_DRV, "[xHCI] Step 8: Running controller");
    g_op_base[XHCI_USBCMD / 4] |= XHCI_CMD_RUN | XHCI_CMD_INTE;
    if (wait_bits(&g_op_base[XHCI_USBSTS / 4], XHCI_STS_HCH, 0, 100) != 0) {
        logi(S_DRV, "[xHCI] ERROR: Controller failed to start!");
        return 0;
    }

    logi(S_DRV, "[xHCI] Controller running. Polling ports...");

    // Включаем питание на всех портах
    for (int i = 1; i <= g_max_ports; i++) {
        uint32_t portsc = g_op_base[XHCI_PORTSC(i) / 4];
        g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PP;
    }

    return 1;
}

void xhci_poll(void) {
    if (!g_op_base) return;
    
    for (int i = 1; i <= g_max_ports; i++) {
        uint32_t portsc = g_op_base[XHCI_PORTSC(i) / 4];
        
        // Если устройство подключено, но порт не включен (не сброшен)
        if ((portsc & XHCI_PORTSC_CCS) && !(portsc & XHCI_PORTSC_PED)) {
            logi(S_DRV, "[xHCI] Device connected on port %d. Resetting...", i);
            
            // Запускаем сброс порта
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PR;
            
            // Ждем завершения сброса
            for (volatile int t = 0; t < 1000000; t++) {
                portsc = g_op_base[XHCI_PORTSC(i) / 4];
                if ((portsc & XHCI_PORTSC_PRC) || !(portsc & XHCI_PORTSC_PR)) break;
            }
            
            // Очищаем флаг завершения сброса (PRC)
            portsc = g_op_base[XHCI_PORTSC(i) / 4];
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_PRC;
            
            logi(S_DRV, "[xHCI] Port %d reset complete. Status: 0x%x", i, portsc);
            logi(S_DRV, "[xHCI] WARNING: Address Device not implemented yet.");
        }
        
        // Очищаем флаг изменения подключения (CSC)
        if (portsc & XHCI_PORTSC_CSC) {
            g_op_base[XHCI_PORTSC(i) / 4] = portsc | XHCI_PORTSC_CSC;
        }
    }
}