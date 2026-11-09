#pragma once
#include <stdint.h>

// --- Базовые структуры xHCI ---

// TRB (Transfer Request Block) - 16 байт, должен быть выровнен на 16
typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} __attribute__((aligned(16))) xhci_trb_t;

// Event Ring Segment Table Entry (ERST)
typedef struct {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

// Структура для Command Ring и Event Ring
typedef struct {
    xhci_trb_t *ring;
    uint32_t size;
    uint32_t enqueue;   // для Command Ring
    uint32_t dequeue;   // для Event Ring
    uint8_t ccs;        // Cycle State (Command Ring)
    uint8_t ccs_event;  // Cycle State (Event Ring)
} xhci_ring_t;

// Контекст слота (Slot Context) - 32 байта
typedef struct {
    uint32_t route_string;
    uint32_t speed;
    uint32_t reserved;
    uint32_t port_dev_info;
    uint32_t tt_info;
    uint32_t state;
    uint32_t reserved2[2];
} __attribute__((packed)) xhci_slot_ctx_t;

// Контекст эндпоинта (Endpoint Context) - 32 байта
typedef struct {
    uint32_t ep_state;
    uint32_t ep_caps;
    uint64_t ep_ring_ptr;
    uint32_t ep_avg_len;
    uint32_t ep_max_packet;
    uint32_t ep_xfer_delay;
    uint32_t reserved[3];
} __attribute__((packed)) xhci_ep_ctx_t;

// Device Context - 2048 байт (33 x 32-байтных записи + padding)
typedef struct {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t eps[31];
} __attribute__((packed, aligned(64))) xhci_device_ctx_t;

// Input Context - для команды Address Device
typedef struct {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t eps[31];
} __attribute__((packed, aligned(64))) xhci_input_ctx_t;

// --- Регистры xHCI ---
#define XHCI_CAPLENGTH      0x00
#define XHCI_HCSPARAMS1     0x04
#define XHCI_HCSPARAMS2     0x08
#define XHCI_HCCPARAMS1     0x10
#define XHCI_DBOFF          0x14
#define XHCI_RTSOFF         0x18

#define XHCI_USBCMD         0x00
#define XHCI_USBSTS         0x04
#define XHCI_PAGESIZE       0x08
#define XHCI_DNCTRL         0x14
#define XHCI_CRCR_LO        0x18
#define XHCI_CRCR_HI        0x1C
#define XHCI_DCBAAP_LO      0x30
#define XHCI_DCBAAP_HI      0x34
#define XHCI_CONFIG         0x38

#define XHCI_PORTSC(n)      (0x400 + (n) * 0x10)
#define XHCI_PORTPMSC(n)    (0x404 + (n) * 0x10)

// Биты USBCMD
#define XHCI_CMD_RUN        (1 << 0)
#define XHCI_CMD_HCRST      (1 << 1)
#define XHCI_CMD_INTE       (1 << 2)

// Биты USBSTS
#define XHCI_STS_HCH        (1 << 0) // Controller Halted
#define XHCI_STS_CNR        (1 << 11) // Controller Not Ready

// Биты PORTSC
#define XHCI_PORTSC_CCS     (1 << 0)  // Current Connect Status
#define XHCI_PORTSC_PED     (1 << 1)  // Port Enabled/Disabled
#define XHCI_PORTSC_PR      (1 << 4)  // Port Reset
#define XHCI_PORTSC_PLS_MASK (0xF << 5) // Port Link State
#define XHCI_PORTSC_PP      (1 << 9)  // Port Power
#define XHCI_PORTSC_CSC     (1 << 17) // Connect Status Change
#define XHCI_PORTSC_PRC     (1 << 21) // Port Reset Change

// Типы TRB (биты 15-10 в поле control)
#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP          2
#define TRB_TYPE_DATA           3
#define TRB_TYPE_STATUS         4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_DISABLE_SLOT   10
#define TRB_TYPE_ADDRESS_DEV    11
#define TRB_TYPE_CONFIG_EP      12
#define TRB_TYPE_EVENT          32
#define TRB_TYPE_CMD_COMPLETE   33
#define TRB_TYPE_PORT_STATUS_CHG 34

// Флаги TRB
#define TRB_CYCLE_STATE (1 << 0)
#define TRB_IOC         (1 << 5) // Interrupt On Completion
#define TRB_IDT         (1 << 6) // Immediate Data

// --- Публичные функции ---
int xhci_init(uint64_t mmio_base);
void xhci_poll(void); // Вызывать в цикле, чтобы читать события и данные от мыши/клавы