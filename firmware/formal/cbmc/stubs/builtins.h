#pragma once

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t length);

#define __builtin_memcpy memcpy
