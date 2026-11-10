#pragma once
#include <stdint.h>

void dma_mask_channel(int ch);
void dma_unmask_channel(int ch);
void dma_setup_8bit(int channel, uint32_t addr, uint16_t len, uint8_t mode);