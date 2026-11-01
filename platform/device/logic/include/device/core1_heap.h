#pragma once

#include <stddef.h>
#include <stdint.h>

void core1_heap_init(void *base, size_t bytes);
void core1_heap_reset(void);
void *core1_heap_allocate(size_t bytes);
