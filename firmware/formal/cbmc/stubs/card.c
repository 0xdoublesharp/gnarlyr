#include "formal.h"

void *memcpy(void *destination, const void *source, size_t length);

uint8_t formal_card[FORMAL_CARD_BYTES];
uint32_t formal_card_size;

void formal_card_fill(void)
{
    __CPROVER_havoc_object(formal_card);
    formal_card_size = nondet_uint32();
    __CPROVER_assume(formal_card_size <= FORMAL_CARD_BYTES);
}

bool formal_card_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    __CPROVER_assert(__CPROVER_w_ok(buffer, length), "a read fits the reader's buffer");
    if (nondet_bool()) {
        __CPROVER_havoc_slice(buffer, length);
        return false;
    }
    if (offset > formal_card_size || length > formal_card_size - offset) {
        return false;
    }
#if FORMAL_CARD_FRESH
    __CPROVER_havoc_slice(buffer, length);
#else
    memcpy(buffer, formal_card + offset, length);
#endif
    return true;
}

ByteSource formal_card_source(void)
{
    return (ByteSource){.read = formal_card_read, .context = NULL};
}

uint32_t formal_below(uint32_t limit)
{
    uint32_t value = nondet_uint32();
    __CPROVER_assume(value < limit);
    return value;
}

void formal_check_text(const char *text, uint32_t capacity)
{
    bool ended = false;
    for (uint32_t index = 0; index < capacity && !ended; index++) {
        ended = text[index] == '\0';
    }
    __CPROVER_assert(ended, "the text ends inside its buffer");
}
