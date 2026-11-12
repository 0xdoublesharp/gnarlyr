#pragma once
#include "platform.h"
static inline uint16_t swap16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t swap32(uint32_t x) { return __builtin_bswap32(x); }
