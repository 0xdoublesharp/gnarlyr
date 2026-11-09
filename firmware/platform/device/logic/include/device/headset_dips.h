#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    HEADSET_DIPS_MARGIN = 24,
    HEADSET_DIPS_HOLD_MS = 3000,
    HEADSET_DIPS_LONGEST_MS = 1000
};

typedef struct HeadsetDips {
    bool started;
    bool dipping;
    uint16_t idle;
    uint16_t lowest;
    uint16_t longest_ms;
    uint8_t count;
    uint32_t dip_start_ms;
    uint32_t last_dip_ms;
} HeadsetDips;

typedef struct HeadsetDipView {
    bool recent;
    uint16_t lowest;
    uint16_t longest_ms;
    uint8_t count;
} HeadsetDipView;

void headset_dips_sample(HeadsetDips *dips, uint16_t level, bool active, uint32_t time_ms);
HeadsetDipView headset_dips_view(const HeadsetDips *dips, uint32_t time_ms);
