#include "ahci.h"
#include "../bus/pci.h"
#include "../mem/pmm.h"
#include "../log/log.h"
#include "../log/serial.h"
#include "../time/pit.h"

#define AHCI_CLASS    0x01
#define AHCI_SUBCLASS 0x06

typedef struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} __attribute__((packed)) hba_port_t;

typedef struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  rsv[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} __attribute__((packed)) hba_mem_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport:4;
    uint8_t  rsv0:3;
    uint8_t  c:1;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    uint16_t cfl:5;
    uint16_t a:1;
    uint16_t w:1;
    uint16_t p:1;
    uint16_t r:1;
    uint16_t b:1;
    uint16_t c:1;
    uint16_t rsv0:1;
    uint16_t pmp:4;
    uint16_t prdtl:16;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} __attribute__((packed)) hba_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} __attribute__((packed)) hba_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt[1];
} __attribute__((packed)) hba_cmd_tbl_t;

static hba_mem_t *g_abar = 0;
static hba_port_t *g_port = 0;
static hba_cmd_header_t *g_cmd_hdr = 0;
static hba_cmd_tbl_t *g_cmd_tbl = 0;
static uint64_t g_clb_phys = 0;
static uint64_t g_fis_phys = 0;
static uint64_t g_ctba_phys = 0;
static uint64_t g_buf_phys = 0;

#define TIMEOUT_MS 100

static int port_stop(hba_port_t *p) {
    p->cmd &= ~1U;                 /* clear ST */
    uint64_t start = pit_ticks();
    while ((p->cmd & 0x8000) && (pit_ticks() - start) < TIMEOUT_MS/10);
    return (p->cmd & 0x8000) == 0;
}

static int port_start(hba_port_t *p) {
    uint64_t start = pit_ticks();
    while ((p->cmd & 0x8000) && (pit_ticks() - start) < TIMEOUT_MS/10);
    if (p->cmd & 0x8000) return 0;

    p->cmd |= (1U << 4);           /* FRE */
    p->cmd |= (1U << 0);           /* ST */
    return 1;
}

int ahci_init(void) {
    pci_device_t pci;
    if (!pci_find_device(AHCI_CLASS, AHCI_SUBCLASS, &pci)) {
        loge(S_DRV, "AHCI controller not found");
        return 0;
    }

    uint64_t bar5 = (uint64_t)(pci.bar[5] & 0xFFFFFFF0);
    logi(S_DRV, "AHCI PCI: vendor=%x dev=%x bar5=%x",
         pci.vendor_id, pci.device_id, bar5);

    if (bar5 == 0 || bar5 == 0xFFFFFFF0) {
        loge(S_DRV, "AHCI BAR5 invalid (%x)", bar5);
        return 0;
    }

    g_abar = (hba_mem_t*)bar5;
    g_abar->ghc |= (1U << 31);  /* AE = 1 */

    uint32_t pi = g_abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1U << i)) {
            hba_port_t *p = &g_abar->ports[i];
            if ((p->ssts & 0x0F) == 3) {
                g_port = p;
                logi(S_DRV, "SATA drive on port %d (sig=%x)", i, p->sig);
                break;
            }
        }
    }

    if (!g_port) {
        loge(S_DRV, "No SATA drive connected");
        return 0;
    }

    g_clb_phys   = pmm_alloc();
    g_fis_phys   = pmm_alloc();
    g_ctba_phys  = pmm_alloc();
    g_buf_phys   = pmm_alloc();

    for (int i = 0; i < 4096; i++) {
        ((uint8_t*)g_clb_phys)[i]  = 0;
        ((uint8_t*)g_fis_phys)[i]  = 0;
        ((uint8_t*)g_ctba_phys)[i] = 0;
        ((uint8_t*)g_buf_phys)[i]  = 0;
    }

    if (!port_stop(g_port)) {
        loge(S_DRV, "port_stop failed (cmd=%x)", g_port->cmd);
        return 0;
    }

    g_port->clb  = (uint32_t)g_clb_phys;
    g_port->clbu = 0;
    g_port->fb   = (uint32_t)g_fis_phys;
    g_port->fbu  = 0;

    g_cmd_hdr = (hba_cmd_header_t*)g_clb_phys;
    g_cmd_hdr[0].prdtl = 1;
    g_cmd_hdr[0].ctba  = (uint32_t)g_ctba_phys;
    g_cmd_hdr[0].ctbau = 0;

    if (!port_start(g_port)) {
        loge(S_DRV, "port_start failed (cmd=%x)", g_port->cmd);
        return 0;
    }

    logi(S_DRV, "AHCI ready, buf=%x cmd=%x tfd=%x",
         g_buf_phys, g_port->cmd, g_port->tfd);
    return 1;
}

static int ahci_cmd(uint32_t lba, uint8_t *buf, int write) {
    if (!g_port) return 0;

    g_cmd_hdr[0].cfl   = 5;
    g_cmd_hdr[0].w     = write ? 1 : 0;
    g_cmd_hdr[0].prdtl = 1;

    g_cmd_tbl = (hba_cmd_tbl_t*)g_ctba_phys;
    g_cmd_tbl->prdt[0].dba  = (uint32_t)g_buf_phys;
    g_cmd_tbl->prdt[0].dbau = 0;
    g_cmd_tbl->prdt[0].dbc  = 512 - 1;
    g_cmd_tbl->prdt[0].i    = 1;

    for (int i = 0; i < 64; i++) g_cmd_tbl->cfis[i] = 0;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t*)g_cmd_tbl->cfis;
    fis->fis_type = 0x27;
    fis->c        = 1;
    fis->command  = write ? 0x35 : 0x25;
    fis->lba0     = (uint8_t)(lba & 0xFF);
    fis->lba1     = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2     = (uint8_t)((lba >> 16) & 0xFF);
    fis->device   = 1 << 6;
    fis->countl   = 1;

    uint64_t start = pit_ticks();
    while (g_port->tfd & (0x80 | 0x08)) {
        if ((pit_ticks() - start) > TIMEOUT_MS/10) return 0;
    }

    if (write) {
        uint8_t *dst = (uint8_t*)g_buf_phys;
        for (int i = 0; i < 512; i++) dst[i] = buf[i];
    }

    g_port->ci = 1;

    start = pit_ticks();
    while (g_port->ci & 1) {
        if ((pit_ticks() - start) > 100) return 0;
    }

    if (!write) {
        uint8_t *src = (uint8_t*)g_buf_phys;
        for (int i = 0; i < 512; i++) buf[i] = src[i];
    }
    return 1;
}

int ahci_read_sector(uint32_t lba, uint8_t *buf) {
    return ahci_cmd(lba, buf, 0);
}

int ahci_write_sector(uint32_t lba, const uint8_t *buf) {
    return ahci_cmd(lba, (uint8_t*)buf, 1);
}