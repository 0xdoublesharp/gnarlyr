#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static inline bool deadline_elapsed(uint32_t now, uint32_t start, uint32_t span)
{
    return now - start >= span;
}
