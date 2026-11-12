#include "device/headset_monitor.h"

HalHeadsetReading headset_monitor_reading(const HeadsetMonitor *monitor, uint32_t time_ms)
{
    HeadsetDipView dips = headset_dips_view(&monitor->dips, time_ms);
    return (HalHeadsetReading){.known = monitor->known,
                               .level = monitor->level,
                               .button = headset_remote_button(monitor->level),
                               .no_mic = headset_remote_no_mic(&monitor->remote),
                               .dipped = dips.recent,
                               .lowest = dips.lowest,
                               .dip_ms = dips.longest_ms,
                               .dips = dips.count};
}
