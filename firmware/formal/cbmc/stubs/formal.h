#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library/byte_source.h"

#ifndef FORMAL_CARD_FRESH
#define FORMAL_CARD_FRESH 0
#endif

#ifndef FORMAL_CARD_BYTES
#define FORMAL_CARD_BYTES 64
#endif

extern uint8_t formal_card[FORMAL_CARD_BYTES];
extern uint32_t formal_card_size;

uint8_t nondet_uint8(void);
uint16_t nondet_uint16(void);
uint32_t nondet_uint32(void);
bool nondet_bool(void);

void formal_card_fill(void);
bool formal_card_read(void *context, uint32_t offset, void *buffer, uint32_t length);
ByteSource formal_card_source(void);
uint32_t formal_below(uint32_t limit);
void formal_check_text(const char *text, uint32_t capacity);
