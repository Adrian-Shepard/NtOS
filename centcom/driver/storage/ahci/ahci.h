#pragma once
#include <stdint.h>

int  ahci_init(void);
int  ahci_read_sector(uint32_t lba, uint8_t *buf);
int  ahci_write_sector(uint32_t lba, const uint8_t *buf);