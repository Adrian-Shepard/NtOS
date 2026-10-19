#pragma once
#include <stdint.h>

typedef enum {
    S_BOO, S_CPU, S_LOG, S_PAN, S_DBG,
    S_PMM, S_VMM, S_HEP, S_CAC, S_MAP,
    S_IRQ, S_TMR, S_SMP,
    S_SCH, S_PRC, S_THR, S_SYC,
    S_DRV, S_PCI, S_ACP, S_ATA, S_KBD, S_TTY, S_NET, S_USB,
    S_VFS, S_FAT, S_EXT, S_NTM,
    S_ELF, S_LIB, S_USR, S_SHP,
    S_SEC, S_AUT,
    S_COUNT
} subsys_t;

void logi(subsys_t s, const char *fmt, ...);
void logw(subsys_t s, const char *fmt, ...);
void loge(subsys_t s, const char *fmt, ...);
void logd(subsys_t s, const char *fmt, ...);

/* Для паники */
void log_get_recent(char *out_buf, int max_lines);