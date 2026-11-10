#pragma once
#include <stdint.h>

int  e1000_init(void);
void e1000_send_packet(const uint8_t *data, uint16_t len);
int  e1000_receive_packet(uint8_t *buf, uint16_t max_len);
void e1000_get_mac(uint8_t *mac_out);