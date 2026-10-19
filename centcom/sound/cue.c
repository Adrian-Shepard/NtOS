#include "cue.h"
#include "speaker.h"
#include "../ntsad/ntsad.h"
#include "../log/log.h"

typedef struct {
    uint16_t freq;
    uint16_t ticks;
} cue_note_t;

typedef struct {
    const cue_note_t *notes;
    uint8_t           len;
} cue_track_t;

static const cue_note_t n_notify[]   = { { 523, 6 }, { 659, 8 } };
static const cue_note_t n_click[]    = { { 440, 2 } };
static const cue_note_t n_warn[]     = { { 330, 5 }, { 0, 3 }, { 330, 5 } };
static const cue_note_t n_error[]    = { { 220, 8 }, { 165, 15 } };
static const cue_note_t n_boot[]     = { { 262, 8 }, { 330, 8 }, { 392, 8 }, { 523, 15 } };
static const cue_note_t n_shutdown[] = { { 523, 8 }, { 392, 8 }, { 330, 8 }, { 262, 15 } };
static const cue_note_t n_panic[]    = {
    { 440, 6 }, { 0, 2 }, { 392, 6 }, { 0, 2 },
    { 440, 6 }, { 0, 2 }, { 392, 6 }, { 0, 2 },
    { 440, 6 }, { 0, 2 }, { 392, 6 }, { 0, 2 },
};

static const cue_note_t n_ui_soft[]   = { { 330, 6 } };
static const cue_note_t n_ui_phase[]  = { { 262, 5 }, { 0, 1 }, { 330, 6 } };
static const cue_note_t n_ui_tick[]   = { { 220, 2 } };
static const cue_note_t n_ui_reveal[] = { { 262, 5 }, { 330, 5 }, { 392, 8 } };
static const cue_note_t n_ui_final[]  = { { 196, 18 } };
static const cue_note_t n_ui_trust[]  = { { 392, 5 }, { 330, 6 }, { 262, 14 } };

static const cue_track_t g_tracks[] = {
    [CUE_BOOT]      = { n_boot,     4 },
    [CUE_SHUTDOWN]  = { n_shutdown, 4 },
    [CUE_NOTIFY]    = { n_notify,   2 },
    [CUE_WARN]      = { n_warn,     3 },
    [CUE_ERROR]     = { n_error,    2 },
    [CUE_PANIC]     = { n_panic,    12 },
    [CUE_CLICK]     = { n_click,    1 },
    [CUE_UI_SOFT]   = { n_ui_soft,   1 },
    [CUE_UI_PHASE]  = { n_ui_phase,  3 },
    [CUE_UI_TICK]   = { n_ui_tick,   1 },
    [CUE_UI_REVEAL] = { n_ui_reveal, 3 },
    [CUE_UI_FINAL]  = { n_ui_final,  1 },
    [CUE_UI_TRUST]  = { n_ui_trust,  3 },
};

static const cue_track_t *g_track   = 0;
static uint8_t            g_idx     = 0;
static uint32_t           g_remain  = 0;
static uint8_t            g_playing = 0;

#if CUE_SOUND_ENABLED
static const cue_track_t *lookup(uint8_t code) {
    if (code >= sizeof(g_tracks) / sizeof(g_tracks[0])) return 0;
    if (!g_tracks[code].notes) return 0;
    return &g_tracks[code];
}
#endif

void cue_play(uint8_t code) {
#if !CUE_SOUND_ENABLED
    (void)code;
    return;
#else
    const cue_track_t *t = lookup(code);
    if (!t) {
        logw(S_DRV, "unknown cue code %u", code);
        return;
    }
    speaker_off();
    g_track   = t;
    g_idx     = 0;
    g_remain  = 0;
    g_playing = 1;
#endif
}

void cue_stop(void) {
    speaker_off();
    g_playing = 0;
    g_track   = 0;
    g_idx     = 0;
    g_remain  = 0;
}

int cue_is_playing(void) { return g_playing; }

void cue_tick(void) {
    if (!g_playing || !g_track) return;
    if (g_remain > 0) { g_remain--; return; }

    if (g_idx >= g_track->len) {
        speaker_off();
        ntsad_emit(NTSAD_CH_CORE, NTSAD_EV_NOTE_OFF, NTSAD_SEV_INFO, 0, 0);
        g_playing = 0;
        g_track   = 0;
        return;
    }

    const cue_note_t *n = &g_track->notes[g_idx];
    g_idx++;

    if (n->freq == 0) speaker_off();
    else              speaker_on(n->freq);

    g_remain = n->ticks;
}

static void on_cue(const ntsad_msg_t *m, void *user) {
    (void)user;
    uint64_t v = m->arg1;
    if (v > 255) {
        logw(S_DRV, "cue arg1 out of range: %x", v);
        return;
    }
    cue_play((uint8_t)v);
}

void cue_init(void) {
    ntsad_subscribe(0xFFFF, NTSAD_EV_CUE, on_cue, 0);
    logi(S_DRV, "cue subsystem ready");
}