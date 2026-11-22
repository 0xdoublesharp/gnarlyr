#pragma once

#include "tags.h"

#define FORMAL_NUMBER_OK(number, length)                                                           \
    ((number)->start <= (number)->end && (number)->end <= (length) &&                              \
     (number)->digits <= (number)->end - (number)->start && (number)->kept <= KEPT_DIGITS &&       \
     (number)->mantissa < 1000000000000ull && (number)->exponent >= -200 &&                        \
     (number)->exponent <= 200 && (number)->point >= -200 && (number)->point <= 200)

#define FORMAL_CONTRACT_number_of                                                                  \
    __CPROVER_requires(length <= TEXT_LIMIT && __CPROVER_is_fresh(text, length))                   \
        __CPROVER_requires(__CPROVER_is_fresh(number, sizeof(Number))) __CPROVER_assigns(*number)  \
            __CPROVER_ensures(!__CPROVER_return_value || FORMAL_NUMBER_OK(number, length))

#define FORMAL_CONTRACT_gain_value                                                                 \
    __CPROVER_requires(__CPROVER_is_fresh(number, sizeof(Number)))                                 \
        __CPROVER_requires(FORMAL_NUMBER_OK(number, TEXT_LIMIT))                                   \
            __CPROVER_requires(__CPROVER_is_fresh(out, sizeof(uint32_t))) __CPROVER_assigns(*out)  \
                __CPROVER_ensures(!__CPROVER_return_value || *out <= GAIN_LIMIT)

#define FORMAL_CONTRACT_peak_value                                                                 \
    __CPROVER_requires(__CPROVER_is_fresh(number, sizeof(Number)))                                 \
        __CPROVER_requires(FORMAL_NUMBER_OK(number, TEXT_LIMIT))                                   \
            __CPROVER_requires(__CPROVER_is_fresh(text, number->end))                              \
                __CPROVER_requires(__CPROVER_is_fresh(out, sizeof(uint32_t)))                      \
                    __CPROVER_assigns(*out)
