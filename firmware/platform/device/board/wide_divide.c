#include "device/wide_divide.h"

uint64_t __udivmoddi4(uint64_t numerator, uint64_t denominator, uint64_t *remainder);

uint64_t __udivmoddi4(uint64_t numerator, uint64_t denominator, uint64_t *remainder)
{
    return wide_divide(numerator, denominator, remainder);
}
