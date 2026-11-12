#include "device/headset_monitor.h"

void headset_monitor_init(HeadsetMonitor *monitor)
{
    *monitor = (HeadsetMonitor){.known = false};
    headset_remote_init(&monitor->remote);
}

void headset_monitor_sample(HeadsetMonitor *monitor, uint16_t level, bool plugged, uint32_t time_ms)
{
    monitor->known = true;
    monitor->level = level;
    headset_remote_sample(&monitor->remote, level, plugged, time_ms);
    headset_dips_sample(&monitor->dips, level, headset_remote_listening(&monitor->remote), time_ms);
}
