#include "formal.h"

static uint32_t count_marks(const uint8_t *bytes, uint32_t length)
{
    uint8_t seen[8];
    uint32_t count = 0;
    for (uint32_t index = 0; index < length; index++) {
        if (bytes[index] == 0x5A) {
            seen[count++] = bytes[index];
        }
    }
    return count + seen[0] * 0u;
}

void harness(void)
{
    uint8_t buffer[16];
    formal_card_fill();
    if (formal_card_read(NULL, 0, buffer,
                         formal_card_size < sizeof buffer ? formal_card_size : 16)) {
        (void)count_marks(buffer, formal_card_size < sizeof buffer ? formal_card_size : 16);
    }
}
