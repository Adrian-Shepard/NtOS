/* doom_platform.c — платформенный слой Doom для NtOS userspace.
   Рисует кадр через syscall gui_win_blit, ввод — через key_poll (сырые
   скан-коды), тайминг — через get_ticks()/sleep(). */

#include "doomgeneric.h"
#include "doomkeys.h"
#include <gui.h>
#include <unistd.h>
#include <stdint.h>

static int g_win = -1;

void doom_platform_set_window(int id) { g_win = id; }

void DG_Init(void) {}
void DG_SetWindowTitle(const char *t) { if (g_win >= 0) gui_win_set_title(g_win, t); }

void DG_DrawFrame(void) {
    if (g_win < 0) return;
    gui_win_blit(g_win, DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

uint32_t DG_GetTicksMs(void) {
    /* PIT = 100 Гц, значит 1 тик = 10 мс */
    return (uint32_t)(get_ticks() * 10);
}

void DG_SleepMs(uint32_t ms) {
    sleep((unsigned long)ms);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    long sc = key_poll();
    if (sc < 0) return 0;
    int rel  = (sc & 0x80) != 0;
    int code = (int)(sc & 0x7F);
    *pressed = rel ? 0 : 1;
    switch (code) {
        case 0x48:*doomKey=KEY_UPARROW;break;
        case 0x50:*doomKey=KEY_DOWNARROW;break;
        case 0x4B:*doomKey=KEY_LEFTARROW;break;
        case 0x4D:*doomKey=KEY_RIGHTARROW;break;
        case 0x1D:*doomKey=KEY_FIRE;break;
        case 0x39:*doomKey=KEY_USE;break;
        case 0x01:*doomKey=KEY_ESCAPE;break;
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
