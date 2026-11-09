#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/input.h"

enum {
    HEADSET_REMOTE_QUEUE_CAPACITY = 8,
    HEADSET_REMOTE_SETTLE_MS = 500,
    HEADSET_REMOTE_PLUG_CHANGE_MS = 1000,
    HEADSET_REMOTE_LONG_PRESS_MS = 400,
    HEADSET_REMOTE_CLICK_GAP_MS = 300,
    HEADSET_REMOTE_MAX_CLICKS = 3
};

typedef struct HeadsetRemote {
    bool plugged;
    bool decided;
    bool no_mic;
    bool run_play;
    uint32_t run_ms;
    HalHeadsetButton candidate;
    HalHeadsetButton held;
    uint32_t held_ms;
    bool long_sent;
    uint8_t clicks;
    uint32_t click_ms;
    uint8_t head;
    uint8_t count;
    HalHeadsetCommand queue[HEADSET_REMOTE_QUEUE_CAPACITY];
} HeadsetRemote;

HalHeadsetButton headset_remote_button(uint16_t level);
void headset_remote_init(HeadsetRemote *remote);
void headset_remote_sample(HeadsetRemote *remote, uint16_t level, bool plugged, uint32_t time_ms);
bool headset_remote_next(HeadsetRemote *remote, HalHeadsetCommand *command);
bool headset_remote_no_mic(const HeadsetRemote *remote);
bool headset_remote_listening(const HeadsetRemote *remote);
