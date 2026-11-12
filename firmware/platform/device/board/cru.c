#include "cru.h"

#include "registers.h"

#define CRU_GATE_BASE 0x80u
#define CRU_RESET_BASE 0xC0u

enum {
    BITS_PER_REGISTER = 16
};

static void write_masked_bit(uint32_t base, uint32_t id, bool set)
{
    uint32_t bit = id % BITS_PER_REGISTER;
    uint32_t value = (1u << (bit + BITS_PER_REGISTER)) | (set ? 1u << bit : 0u);
    register_write(CRU_BASE + base + 4u * (id / BITS_PER_REGISTER), value);
}

void cru_gate(uint32_t id, bool clock_on)
{
    write_masked_bit(CRU_GATE_BASE, id, !clock_on);
}

void cru_reset(uint32_t id, bool held)
{
    write_masked_bit(CRU_RESET_BASE, id, held);
}

void cru_write(uint32_t offset, uint32_t value)
{
    register_write(CRU_BASE + offset, value);
}

void cru_stop_unused_clocks(void)
{
    static const struct {
        uint8_t offset;
        uint32_t value;
    } WORDS[] = {{0x88, 0x00070007u},
                 {0x84, 0xDCD0DCD0u},
                 {0xA0, 0x7DF27DF2u},
                 {0xA4, 0x00020002u},
                 {0x80, 0x1B201B20u}};
    for (uint32_t index = 0; index < sizeof WORDS / sizeof WORDS[0]; index++) {
        register_write(CRU_BASE + WORDS[index].offset, WORDS[index].value);
    }
}
