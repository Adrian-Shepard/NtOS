#pragma once
#include <stdint.h>

/* Вызывается для каждого обычного файла в архиве */
typedef void (*tar_callback_t)(const char *name,
                                const uint8_t *data,
                                uint64_t size);

/* Разбирает tar-архив. Для каждого файла вызывает cb. */
void tar_parse(const uint8_t *data, uint64_t size, tar_callback_t cb);