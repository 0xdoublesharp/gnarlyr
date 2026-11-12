#include "device/headset_dips.h"

HeadsetDipView headset_dips_view(const HeadsetDips *dips, uint32_t time_ms)
{
    bool recent =
        dips->count > 0 && (dips->dipping || time_ms - dips->last_dip_ms < HEADSET_DIPS_HOLD_MS);
    if (!recent) {
        return (HeadsetDipView){.recent = false};
    }
    uint16_t longest = dips->longest_ms;
    uint16_t current = dips->dipping ? headset_dips_clamp_ms(time_ms - dips->dip_start_ms) : 0;
    return (HeadsetDipView){.recent = true,
                            .lowest = dips->lowest,
                            .longest_ms = current > longest ? current : longest,
                            .count = dips->count};
}
