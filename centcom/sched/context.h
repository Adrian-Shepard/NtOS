#pragma once
#include <stdint.h>

void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

/* Переключение с одновременной сменой адресного пространства */
void context_switch_full(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3);