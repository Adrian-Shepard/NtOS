#pragma once
#include <stdint.h>

void pic_init(uint8_t master_mask, uint8_t slave_mask);
void pic_send_eoi(unsigned char irq);