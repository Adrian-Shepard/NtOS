#include "ntsad.h"
#include "../log/log.h"
#include "../time/pit.h"

#define MAX_SUBS 16

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void irq_restore(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

typedef struct {
    uint16_t   ch_mask;
    uint8_t    ev;
    uint8_t    used;
    ntsad_cb_t cb;
    void      *user;
} sub_t;

static ntsad_msg_t g_queue[NTSAD_QUEUE_SIZE];
static int         g_qhead  = 0;
static int         g_qcount = 0;
static uint64_t    g_seq    = 0;

static sub_t       g_subs[MAX_SUBS];
static uint16_t    g_active = 0;
static uint32_t    g_hb_acc = 0;

void ntsad_init(void) {
    uint64_t f = irq_save();
    g_qhead  = 0;
    g_qcount = 0;
    g_seq    = 0;
    g_active = 0;
    g_hb_acc = 0;
    for (int i = 0; i < MAX_SUBS; i++) g_subs[i].used = 0;
    irq_restore(f);

    logi(S_LOG, "NTSAD bus online (queue=%d, subs=%d)",
         NTSAD_QUEUE_SIZE, MAX_SUBS);
}

static void emit_locked(uint8_t ch, uint8_t ev, uint8_t sev,
                        uint64_t arg1, uint64_t arg2) {
    if (ch >= NTSAD_CH_COUNT) return;
    if (!(g_active & (1u << ch))) return;

    int slot;
    if (g_qcount < NTSAD_QUEUE_SIZE) {
        slot = (g_qhead + g_qcount) % NTSAD_QUEUE_SIZE;
        g_qcount++;
    } else {
        slot = g_qhead;
        g_qhead = (g_qhead + 1) % NTSAD_QUEUE_SIZE;
    }

    ntsad_msg_t *m = &g_queue[slot];
    m->timestamp   = pit_ticks();
    m->seq         = g_seq++;
    m->arg1        = arg1;
    m->arg2        = arg2;
    m->payload_len = 0;
    m->channel     = ch;
    m->event       = ev;
    m->severity    = sev;
    m->flags       = 0;
#ifdef NTSAD_WITH_PAYLOAD
    m->payload[0]  = 0;
#endif

    for (int i = 0; i < MAX_SUBS; i++) {
        sub_t *s = &g_subs[i];
        if (!s->used) continue;
        if (!(s->ch_mask & (1u << ch))) continue;
        if (s->ev != NTSAD_EV_ANY && s->ev != ev) continue;
        s->cb(m, s->user);
    }
}

void ntsad_emit(uint8_t ch, uint8_t ev, uint8_t sev,
                uint64_t arg1, uint64_t arg2) {
    uint64_t f = irq_save();
    emit_locked(ch, ev, sev, arg1, arg2);
    irq_restore(f);
}

void ntsad_tick(void) {
    g_hb_acc++;
    if (g_hb_acc >= 1000) {
        g_hb_acc = 0;
        emit_locked(NTSAD_CH_CORE, NTSAD_EV_HEARTBEAT,
                    NTSAD_SEV_DEBUG, pit_ticks(), 0);
    }
}

int ntsad_subscribe(uint16_t ch_mask, uint8_t ev,
                    ntsad_cb_t cb, void *user) {
    if (!cb) return -1;

    uint64_t f = irq_save();
    int found = -1;
    for (int i = 0; i < MAX_SUBS; i++) {
        if (!g_subs[i].used) {
            g_subs[i].ch_mask = ch_mask;
            g_subs[i].ev      = ev;
            g_subs[i].cb      = cb;
            g_subs[i].user    = user;
            g_subs[i].used    = 1;
            g_active |= ch_mask;
            found = i;
            break;
        }
    }
    irq_restore(f);
    return found;
}

void ntsad_unsubscribe(int handle) {
    if (handle < 0 || handle >= MAX_SUBS) return;
    uint64_t f = irq_save();
    g_subs[handle].used = 0;
    g_active = 0;
    for (int i = 0; i < MAX_SUBS; i++)
        if (g_subs[i].used) g_active |= g_subs[i].ch_mask;
    irq_restore(f);
}

uint16_t ntsad_active_channels(void) { return g_active; }

int ntsad_length(void) {
    uint64_t f = irq_save();
    int n = g_qcount;
    irq_restore(f);
    return n;
}

const ntsad_msg_t *ntsad_peek(int index) {
    if (index < 0 || index >= g_qcount) return 0;
    int slot = (g_qhead + index) % NTSAD_QUEUE_SIZE;
    return &g_queue[slot];
}

void ntsad_copy(int index, ntsad_msg_t *out) {
    if (!out) return;
    if (index < 0 || index >= g_qcount) return;
    int slot = (g_qhead + index) % NTSAD_QUEUE_SIZE;
    uint64_t f = irq_save();
    *out = g_queue[slot];
    irq_restore(f);
}

void ntsad_state(uint8_t ch, uint8_t state) {
    ntsad_emit(ch, NTSAD_EV_STATE, NTSAD_SEV_INFO, state, 0);
}
void ntsad_metric(uint8_t ch, uint64_t key, int64_t value) {
    ntsad_emit(ch, NTSAD_EV_METRIC, NTSAD_SEV_DEBUG, key, (uint64_t)value);
}
void ntsad_cue(uint8_t cue_code) {
    ntsad_emit(NTSAD_CH_CORE, NTSAD_EV_CUE, NTSAD_SEV_INFO, cue_code, 0);
}
void ntsad_error(uint8_t ch, uint64_t err_code) {
    ntsad_emit(ch, NTSAD_EV_ERROR, NTSAD_SEV_ERROR, err_code, 0);
}