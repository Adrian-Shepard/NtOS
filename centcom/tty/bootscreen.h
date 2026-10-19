#pragma once
#include "../../bootinfo.h"

void bootscreen_phase1(const bootinfo_t *bi);   /* handoff  */
void bootscreen_phase2(void);                   /* CPU      */
void bootscreen_phase3(void);                   /* memory   */
void bootscreen_phase4(void);                   /* periph   */
void bootscreen_phase5(const bootinfo_t *bi);   /* binary   */