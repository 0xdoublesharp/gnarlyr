#include "device/headset_dips.h"

static uint16_t clamp_ms(uint32_t milliseconds)
{
    return milliseconds > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)milliseconds;
}

static void start_dip(HeadsetDips *dips, uint16_t level, uint32_t time_ms)
{
    bool fresh = dips->count == 0 || time_ms - dips->last_dip_ms >= HEADSET_DIPS_HOLD_MS;
    if (fresh) {
        dips->count = 0;
        dips->lowest = level;
        dips->longest_ms = 0;
    }
    dips->dipping = true;
    dips->dip_start_ms = time_ms;
    if (dips->count < UINT8_MAX) {
        dips->count++;
    }
}

static void end_dip(HeadsetDips *dips, uint32_t time_ms)
{
    uint16_t length = clamp_ms(time_ms - dips->dip_start_ms);
    if (length > dips->longest_ms) {
        dips->longest_ms = length;
    }
    dips->dipping = false;
}

static bool rebaselined(HeadsetDips *dips, uint16_t level, uint32_t time_ms)
{
    if (!dips->dipping || time_ms - dips->dip_start_ms < HEADSET_DIPS_LONGEST_MS) {
        return false;
    }
    end_dip(dips, time_ms);
    dips->idle = level;
    dips->last_dip_ms = time_ms;
    return true;
}

void headset_dips_sample(HeadsetDips *dips, uint16_t level, bool active, uint32_t time_ms)
{
    if (!active) {
        *dips = (HeadsetDips){.started = false};
        return;
    }
    if (rebaselined(dips, level, time_ms)) {
        return;
    }
    if (!dips->started) {
        *dips = (HeadsetDips){.started = true, .idle = level};
        return;
    }
    bool below = level + HEADSET_DIPS_MARGIN < dips->idle;
    if (below && !dips->dipping) {
        start_dip(dips, level, time_ms);
    } else if (!below && dips->dipping) {
        end_dip(dips, time_ms);
    }
    if (dips->dipping) {
        dips->lowest = level < dips->lowest ? level : dips->lowest;
        dips->last_dip_ms = time_ms;
    } else {
        dips->idle = level;
    }
}

HeadsetDipView headset_dips_view(const HeadsetDips *dips, uint32_t time_ms)
{
    bool recent =
        dips->count > 0 && (dips->dipping || time_ms - dips->last_dip_ms < HEADSET_DIPS_HOLD_MS);
    if (!recent) {
        return (HeadsetDipView){.recent = false};
    }
    uint16_t longest = dips->longest_ms;
    uint16_t current = dips->dipping ? clamp_ms(time_ms - dips->dip_start_ms) : 0;
    return (HeadsetDipView){.recent = true,
                            .lowest = dips->lowest,
                            .longest_ms = current > longest ? current : longest,
                            .count = dips->count};
}
