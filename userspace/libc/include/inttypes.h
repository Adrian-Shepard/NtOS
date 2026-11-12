#pragma once
#include <stdint.h>

/* Минимальный freestanding inttypes.h: C99-типы уже в stdint.h,
   здесь только imaxdiv_t и часто используемые форматные макросы. */

typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
