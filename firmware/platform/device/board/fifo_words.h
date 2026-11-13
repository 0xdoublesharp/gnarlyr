#pragma once

#include <stdint.h>

void fifo_words_read(uint32_t *to, uint32_t fifo, uint32_t count);
void fifo_words_write(uint32_t fifo, const uint32_t *from, uint32_t count);
