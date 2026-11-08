#include "e1000.h"
#include "../bus/pci.h"
#include "../log/log.h"
#include "../mem/pmm.h"
#include "../mem/vmm.h"

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

#define REG_CTRL  0x0000
#define REG_STATUS 0x0008
#define REG_ICR   0x00C0
#define REG_IMASK 0x00D0
#define REG_RCTL  0x0100
#define REG_TCTL  0x0400
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH   0x2810
#define REG_RDT   0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH   0x3810
#define REG_TDT   0x3818
#define REG_MTA   0x5200
#define REG_RAL   0x5400
#define REG_RAH   0x5404

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 32
#define BUF_SIZE      2048

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

static volatile uint32_t *g_mmio = 0;
static uint8_t g_mac[6];
static uint64_t g_rx_phys = 0;
static uint64_t g_tx_phys = 0;
static uint64_t g_rx_buf_phys[RX_DESC_COUNT];
static uint64_t g_tx_buf_phys[TX_DESC_COUNT];
static e1000_rx_desc_t *g_rx_desc = 0;
static e1000_tx_desc_t *g_tx_desc = 0;
static uint32_t g_rx_cur = 0;
static uint32_t g_tx_cur = 0;

static inline void outl_mmio(uint32_t reg, uint32_t val) { g_mmio[reg/4] = val; }
static inline uint32_t inl_mmio(uint32_t reg) { return g_mmio[reg/4]; }

int e1000_init(void) {
    pci_device_t pci;
    if (!pci_find_device(0x02, 0x00, &pci)) { // Class 02 = Network, Subclass 00 = Ethernet
        loge(S_DRV, "e1000 not found");
        return 0;
    }
    if (pci.vendor_id != E1000_VENDOR || (pci.device_id != E1000_DEVICE && pci.device_id != 0x10D3)) {
        loge(S_DRV, "PCI device is not e1000/e1000e (%x:%x)", pci.vendor_id, pci.device_id);
        return 0;
    }

    uint64_t bar0 = (uint64_t)(pci.bar[0] & 0xFFFFFFF0);
    g_mmio = (volatile uint32_t*)bar0;
    logi(S_DRV, "e1000 found, MMIO at %x", bar0);

    // Читаем MAC
    uint32_t ral = inl_mmio(REG_RAL);
    uint32_t rah = inl_mmio(REG_RAH);
    g_mac[0] = ral & 0xFF;
    g_mac[1] = (ral >> 8) & 0xFF;
    g_mac[2] = (ral >> 16) & 0xFF;
    g_mac[3] = (ral >> 24) & 0xFF;
    g_mac[4] = rah & 0xFF;
    g_mac[5] = (rah >> 8) & 0xFF;
    logi(S_DRV, "e1000 MAC: %x:%x:%x:%x:%x:%x",
         (uint64_t)g_mac[0], (uint64_t)g_mac[1], (uint64_t)g_mac[2],
         (uint64_t)g_mac[3], (uint64_t)g_mac[4], (uint64_t)g_mac[5]);

    // Сброс
    outl_mmio(REG_CTRL, inl_mmio(REG_CTRL) | (1 << 26));
    for (volatile int i = 0; i < 100000; i++) __asm__ volatile("pause");

    // Выделяем память под дескрипторы и буферы
    g_rx_phys = pmm_alloc();
    g_tx_phys = pmm_alloc();
    for (int i = 0; i < RX_DESC_COUNT; i++) g_rx_buf_phys[i] = pmm_alloc();
    for (int i = 0; i < TX_DESC_COUNT; i++) g_tx_buf_phys[i] = pmm_alloc();

    g_rx_desc = (e1000_rx_desc_t*)g_rx_phys;
    g_tx_desc = (e1000_tx_desc_t*)g_tx_phys;

    for (int i = 0; i < RX_DESC_COUNT; i++) {
        g_rx_desc[i].addr = g_rx_buf_phys[i];
        g_rx_desc[i].status = 0;
    }
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        g_tx_desc[i].addr = g_tx_buf_phys[i];
        g_tx_desc[i].status = 1; // DD (Descriptor Done)
        g_tx_desc[i].cmd = 0;
    }

    // Настройка RX
    outl_mmio(REG_RDBAL, (uint32_t)g_rx_phys);
    outl_mmio(REG_RDBAH, 0);
    outl_mmio(REG_RDLEN, RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    outl_mmio(REG_RDH, 0);
    outl_mmio(REG_RDT, RX_DESC_COUNT - 1);
    outl_mmio(REG_RCTL, (1 << 1) | (1 << 15) | (1 << 26)); // EN, BAM, SECRC

    // Настройка TX
    outl_mmio(REG_TDBAL, (uint32_t)g_tx_phys);
    outl_mmio(REG_TDBAH, 0);
    outl_mmio(REG_TDLEN, TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    outl_mmio(REG_TDH, 0);
    outl_mmio(REG_TDT, 0);
    outl_mmio(REG_TCTL, (1 << 1) | (1 << 3) | (0x10 << 4) | (0x40 << 12)); // EN, PSP, CT, COLD

    // Сброс MTA (Multicast Table Array)
    for (int i = 0; i < 128; i++) outl_mmio(REG_MTA + i*4, 0);

    logi(S_DRV, "e1000 initialized");
    return 1;
}

void e1000_get_mac(uint8_t *mac_out) {
    for (int i = 0; i < 6; i++) mac_out[i] = g_mac[i];
}

void e1000_send_packet(const uint8_t *data, uint16_t len) {
    if (!g_mmio) return;
    if (len > BUF_SIZE) len = BUF_SIZE;

    uint8_t *buf = (uint8_t*)g_tx_buf_phys[g_tx_cur];
    for (uint16_t i = 0; i < len; i++) buf[i] = data[i];

    g_tx_desc[g_tx_cur].length = len;
    g_tx_desc[g_tx_cur].cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP, IFCS, RS
    g_tx_desc[g_tx_cur].status = 0;

    g_tx_cur = (g_tx_cur + 1) % TX_DESC_COUNT;
    outl_mmio(REG_TDT, g_tx_cur);

    // Ждем завершения отправки
    for (volatile int i = 0; i < 100000; i++) {
        if (g_tx_desc[(g_tx_cur - 1 + TX_DESC_COUNT) % TX_DESC_COUNT].status & 0x0F) break;
        __asm__ volatile("pause");
    }
}

int e1000_receive_packet(uint8_t *buf, uint16_t max_len) {
    if (!g_mmio) return 0;
    if (!(g_rx_desc[g_rx_cur].status & 0x01)) return 0; // DD не установлен

    uint16_t len = g_rx_desc[g_rx_cur].length;
    if (len > max_len) len = max_len;
    uint8_t *src = (uint8_t*)g_rx_buf_phys[g_rx_cur];
    for (uint16_t i = 0; i < len; i++) buf[i] = src[i];

    g_rx_desc[g_rx_cur].status = 0;
    outl_mmio(REG_RDT, g_rx_cur);
    g_rx_cur = (g_rx_cur + 1) % RX_DESC_COUNT;

    return len;
}