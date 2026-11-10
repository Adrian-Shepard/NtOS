#include "doomgeneric.h"
#include "doomkeys.h"
#include <stdint.h>

extern void     fb_swap(void);
extern uint32_t fb_width(void);
extern uint32_t fb_height(void);
extern void*    fb_get_real_ptr(int x, int y);
extern int      keyboard_pop_scancode(void);
extern uint64_t pit_ticks(void);

extern void doomgeneric_Create(int, char**);
extern void doomgeneric_Tick(void);

volatile int doom_should_exit = 0;

void DG_Init(void) {}
void DG_SetWindowTitle(const char* t) { (void)t; }

// ============ ОТРИСОВКА ============
void DG_DrawFrame(void) {
    int W = (int)fb_width();
    int H = (int)fb_height();
    int xo = (W - 640) / 2; if (xo < 0) xo = 0;
    int yo = (H - 400) / 2; if (yo < 0) yo = 0;

    for (int y = 0; y < 400; y++) {
        uint32_t *row = (uint32_t *)fb_get_real_ptr(xo, yo + y);
        if (row) {
            for (int x = 0; x < 640; x++) {
                row[x] = DG_ScreenBuffer[y * 640 + x];
            }
        }
    }
    fb_swap(); // Используем полную замену кадра
}

// ============ ТАЙМИНГ ============
static uint64_t _t0 = 0;
uint32_t DG_GetTicksMs(void) {
    if (!_t0) _t0 = pit_ticks();
    return (uint32_t)((pit_ticks() - _t0) * 10);
}
void DG_SleepMs(uint32_t ms) { (void)ms; } // Без сна, чтобы избежать зависаний

// ============ КЛАВИАТУРА ============
int DG_GetKey(int* pressed, unsigned char* doomKey) {
    int sc = keyboard_pop_scancode();
    if (sc < 0) return 0;
    int rel  = (sc & 0x80) != 0;
    int code = sc & 0x7F;
    *pressed = rel ? 0 : 1;
    switch (code) {
        case 0x48:*doomKey=KEY_UPARROW;break;
        case 0x50:*doomKey=KEY_DOWNARROW;break;
        case 0x4B:*doomKey=KEY_LEFTARROW;break;
        case 0x4D:*doomKey=KEY_RIGHTARROW;break;
        case 0x1D:*doomKey=KEY_FIRE;break;
        case 0x39:*doomKey=KEY_USE;break;
        case 0x01: doom_should_exit = 1; *doomKey=KEY_ESCAPE; break;
        case 0x1C:*doomKey=KEY_ENTER;break;
        case 0x0F:*doomKey=KEY_TAB;break;
        case 0x2A: case 0x36:*doomKey=KEY_RSHIFT;break;
        case 0x1E:*doomKey='a';break; case 0x30:*doomKey='b';break;
        case 0x2E:*doomKey='c';break; case 0x20:*doomKey='d';break;
        case 0x12:*doomKey='e';break; case 0x21:*doomKey='f';break;
        case 0x22:*doomKey='g';break; case 0x23:*doomKey='h';break;
        case 0x17:*doomKey='i';break; case 0x24:*doomKey='j';break;
        case 0x25:*doomKey='k';break; case 0x26:*doomKey='l';break;
        case 0x32:*doomKey='m';break; case 0x31:*doomKey='n';break;
        case 0x18:*doomKey='o';break; case 0x19:*doomKey='p';break;
        case 0x10:*doomKey='q';break; case 0x13:*doomKey='r';break;
        case 0x1F:*doomKey='s';break; case 0x14:*doomKey='t';break;
        case 0x16:*doomKey='u';break; case 0x2F:*doomKey='v';break;
        case 0x11:*doomKey='w';break; case 0x2D:*doomKey='x';break;
        case 0x15:*doomKey='y';break; case 0x2C:*doomKey='z';break;
        case 0x02:*doomKey='1';break; case 0x03:*doomKey='2';break;
        case 0x04:*doomKey='3';break; case 0x05:*doomKey='4';break;
        case 0x06:*doomKey='5';break; case 0x0C:*doomKey='-';break;
        case 0x0D:*doomKey='=';break;
        default: return 0;
    }
    return 1;
}

// ============ ЗАПУСК ============
void doom_main(void) {
    doom_should_exit = 0;
    static char* argv[] = { "doom", "-iwad", "doom1.wad", 0 };
    doomgeneric_Create(3, argv);
    while (!doom_should_exit) doomgeneric_Tick();
}