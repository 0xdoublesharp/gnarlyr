#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Standby {
    bool active;
    bool bluetooth_enabled;
    bool waking;
    uint32_t entries;
    uint32_t wakes;
    uint32_t entry_ms;
    uint32_t wake_ms;
    uint32_t latency_ms;
    uint16_t entry_mv;
    uint16_t wake_mv;
} Standby;
