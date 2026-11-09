#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/headset_dips.h"
#include "device/headset_remote.h"
#include "hal/input.h"

typedef struct HeadsetMonitor {
    HeadsetRemote remote;
    HeadsetDips dips;
    bool known;
    uint16_t level;
} HeadsetMonitor;

void headset_monitor_init(HeadsetMonitor *monitor);
void headset_monitor_sample(HeadsetMonitor *monitor, uint16_t level, bool plugged,
                            uint32_t time_ms);
HalHeadsetReading headset_monitor_reading(const HeadsetMonitor *monitor, uint32_t time_ms);
