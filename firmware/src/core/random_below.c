#include "core/random.h"

uint32_t random_below(Random *random, uint32_t bound)
{
    if (bound <= 1) {
        return 0;
    }
    uint32_t threshold = (uint32_t)(0u - bound) % bound;
    uint32_t value = random_next(random);
    while (value < threshold) {
        value = random_next(random);
    }
    return value % bound;
}
