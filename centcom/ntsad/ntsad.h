#pragma once
#include <stdint.h>

/* 16 каналов */
#define NTSAD_CH_CORE   0
#define NTSAD_CH_MEM    1
#define NTSAD_CH_IRQ    2
#define NTSAD_CH_TIME   3
#define NTSAD_CH_SMP    4
#define NTSAD_CH_SCH    5
#define NTSAD_CH_PROC   6
#define NTSAD_CH_SYSC   7
#define NTSAD_CH_DRV    8
#define NTSAD_CH_BUS    9
#define NTSAD_CH_STOR   10
#define NTSAD_CH_FS     11
#define NTSAD_CH_NET    12
#define NTSAD_CH_HID    13
#define NTSAD_CH_TTY    14
#define NTSAD_CH_SEC    15
#define NTSAD_CH_COUNT  16

/* типы событий */
#define NTSAD_EV_STATE     0x01
#define NTSAD_EV_METRIC    0x02
#define NTSAD_EV_CUE       0x03
#define NTSAD_EV_NOTE_ON   0x04
#define NTSAD_EV_NOTE_OFF  0x05
#define NTSAD_EV_HEARTBEAT 0x06
#define NTSAD_EV_MARK      0x07
#define NTSAD_EV_ERROR     0x08
#define NTSAD_EV_CUSTOM    0x09
#define NTSAD_EV_ANY       0xFF

/* severity */
#define NTSAD_SEV_DEBUG    0x00
#define NTSAD_SEV_INFO     0x20
#define NTSAD_SEV_NOTICE   0x60
#define NTSAD_SEV_WARN     0xA0
#define NTSAD_SEV_ERROR    0xD0
#define NTSAD_SEV_CRIT     0xF0

/* состояния подсистем */
#define NTSAD_STATE_OFFLINE   0x00
#define NTSAD_STATE_INIT      0x01
#define NTSAD_STATE_READY     0x02
#define NTSAD_STATE_BUSY      0x03
#define NTSAD_STATE_DEGRADED  0x04
#define NTSAD_STATE_FAILED    0x05
#define NTSAD_STATE_SHUTDOWN  0x06

/* встроенные звуковые сигналы */
#define NTSAD_CUE_BOOT      0x01
#define NTSAD_CUE_SHUTDOWN  0x02
#define NTSAD_CUE_NOTIFY    0x03
#define NTSAD_CUE_WARN      0x04
#define NTSAD_CUE_ERROR     0x05
#define NTSAD_CUE_PANIC     0x06
#define NTSAD_CUE_CLICK     0x07

#ifdef NTSAD_WITH_PAYLOAD
  #define NTSAD_PAYLOAD_MAX 64
#else
  #define NTSAD_PAYLOAD_MAX 0
#endif

#define NTSAD_QUEUE_SIZE 128

typedef struct {
    uint64_t timestamp;
    uint64_t seq;
    uint64_t arg1;
    uint64_t arg2;
    uint16_t payload_len;
    uint8_t  channel;
    uint8_t  event;
    uint8_t  severity;
    uint8_t  flags;
#ifdef NTSAD_WITH_PAYLOAD
    uint8_t  payload[NTSAD_PAYLOAD_MAX];
#endif
} ntsad_msg_t;

typedef void (*ntsad_cb_t)(const ntsad_msg_t *msg, void *user);

void ntsad_init(void);
void ntsad_tick(void);

void ntsad_emit(uint8_t ch, uint8_t ev, uint8_t sev,
                uint64_t arg1, uint64_t arg2);

int      ntsad_subscribe(uint16_t ch_mask, uint8_t ev,
                         ntsad_cb_t cb, void *user);
void     ntsad_unsubscribe(int handle);
uint16_t ntsad_active_channels(void);

int                ntsad_length(void);
const ntsad_msg_t *ntsad_peek(int index);
void               ntsad_copy(int index, ntsad_msg_t *out);

void ntsad_state (uint8_t ch, uint8_t state);
void ntsad_metric(uint8_t ch, uint64_t key, int64_t value);
void ntsad_cue   (uint8_t cue_code);
void ntsad_error (uint8_t ch, uint64_t err_code);