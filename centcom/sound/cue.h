#pragma once
#include <stdint.h>
#define CUE_SOUND_ENABLED 0
/* встроенные сигналы */
#define CUE_BOOT        0x01
#define CUE_SHUTDOWN    0x02
#define CUE_NOTIFY      0x03
#define CUE_WARN        0x04
#define CUE_ERROR       0x05
#define CUE_PANIC       0x06
#define CUE_CLICK       0x07

/* UI-звуки boot screen — мягкие, низкие */
#define CUE_UI_SOFT     0x08   /* одиночный мягкий тон */
#define CUE_UI_PHASE    0x09   /* смена фазы: C4→E4 */
#define CUE_UI_TICK     0x0A   /* микро-тик для строк данных */
#define CUE_UI_REVEAL   0x0B   /* аккорд C4-E4-G4 */
#define CUE_UI_FINAL    0x0C   /* одна низкая нота (G3) */
#define CUE_UI_TRUST    0x0D   /* нисходящий G4-E4-C4 */

void cue_init(void);
void cue_tick(void);
void cue_play(uint8_t code);
void cue_stop(void);
int  cue_is_playing(void);