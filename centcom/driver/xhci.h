#pragma once
#include <stdint.h>

// ==========================================
// 1. ОСНОВНЫЕ СТРУКТУРЫ xHCI
// ==========================================

typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} __attribute__((aligned(16))) xhci_trb_t;

typedef struct {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

typedef struct {
    xhci_trb_t *ring;
    uint32_t size;
    uint32_t enqueue;
    uint32_t dequeue;
    uint8_t ccs;
    uint8_t ccs_event;
} xhci_ring_t;

typedef struct {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
    uint32_t dword4;
    uint32_t reserved[3];
} __attribute__((packed)) xhci_slot_ctx_t;

typedef struct {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
    uint32_t dword4;
    uint32_t reserved[3];
} __attribute__((packed)) xhci_ep_ctx_t;

typedef struct {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t eps[31];
} __attribute__((packed, aligned(64))) xhci_device_ctx_t;

typedef struct {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t eps[31];
} __attribute__((packed, aligned(64))) xhci_input_ctx_t;

// ==========================================
// 2. РЕГИСТРЫ
// ==========================================
#define XHCI_CAPLENGTH      0x00
#define XHCI_HCSPARAMS1     0x04
#define XHCI_DBOFF          0x14
#define XHCI_RTSOFF         0x18

#define XHCI_USBCMD         0x00
#define XHCI_USBSTS         0x04
#define XHCI_CRCR_LO        0x18
#define XHCI_CRCR_HI        0x1C
#define XHCI_DCBAAP_LO      0x30
#define XHCI_DCBAAP_HI      0x34
#define XHCI_CONFIG         0x38

#define XHCI_PORTSC(n)      (0x400 + (n - 1) * 0x10)

// БИТЫ USBCMD / USBSTS
#define XHCI_CMD_RUN        (1 << 0)
#define XHCI_CMD_HCRST      (1 << 1)
#define XHCI_CMD_INTE       (1 << 2)
#define XHCI_STS_HCH        (1 << 0)
#define XHCI_STS_CNR        (1 << 11)

// БИТЫ PORTSC
#define XHCI_PORTSC_CCS     (1 << 0)
#define XHCI_PORTSC_PED     (1 << 1)
#define XHCI_PORTSC_PR      (1 << 4)
#define XHCI_PORTSC_PLS_MASK (0xF << 5)
#define XHCI_PORTSC_PLS_U0  (0 << 5)
#define XHCI_PORTSC_PP      (1 << 9)
#define XHCI_PORTSC_CSC     (1 << 17)
#define XHCI_PORTSC_PRC     (1 << 21)

// ТИПЫ TRB
#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP          2
#define TRB_TYPE_DATA           3
#define TRB_TYPE_STATUS         4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_NO_OP          8
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_ADDRESS_DEV    11
#define TRB_TYPE_CONFIGURE_EP   12
#define TRB_TYPE_SET_TR_DEQUEUE 16
#define TRB_TYPE_TRANSFER_EVENT 32
#define TRB_TYPE_CMD_COMPLETE   33
#define TRB_TYPE_PORT_STATUS    34

#define TRB_IOC         (1 << 5)
#define TRB_IDT         (1 << 6)

// ТИПЫ ENDPOINT-КОНТЕКСТА
#define EP_TYPE_BULK_OUT        2
#define EP_TYPE_INTERRUPT_OUT   3
#define EP_TYPE_CONTROL         4
#define EP_TYPE_BULK_IN         6
#define EP_TYPE_INTERRUPT_IN    7

// DCI = 2*ep_num + (IN?1:0)
#define XHCI_EP_DCI(ep_num, is_in)  (2 * (ep_num) + ((is_in) ? 1 : 0))

// Context Entries = bits 27..31 в Slot Context dword0
#define XHCI_SLOT_CTX_SET_ENTRIES(dw0, n) \
    ((dw0) = ((dw0) & ~(0x1Fu << 27)) | (((n) & 0x1Fu) << 27))

// ==========================================
// 3. ФУНКЦИИ
// ==========================================
int  xhci_init(uint64_t mmio_base);
void xhci_poll(void);
int  xhci_address_device(int port, uint8_t speed);
int  xhci_control_transfer(int slot_id, uint8_t *setup, uint8_t *data, uint32_t len, int direction_in);
int  xhci_get_descriptor(int slot_id, uint8_t type, uint8_t index, uint16_t lang, void *buffer, uint32_t len);
int  xhci_set_configuration(int slot_id, uint8_t config_value);

// HID-клавиатура (реализация в xhci.c)
int  xhci_get_key(void);     // 0 = нет данных, иначе ASCII/скан
int  xhci_kbd_ready(void);   // 1 = клавиатура подключена