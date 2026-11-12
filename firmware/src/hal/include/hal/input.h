#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key.h"

typedef struct HalKeyTransition {
    Key key;
    bool pressed;
    uint32_t time_ms;
} HalKeyTransition;

typedef enum HalHeadsetButton {
    HAL_HEADSET_BUTTON_NONE,
    HAL_HEADSET_BUTTON_PLAY,
    HAL_HEADSET_BUTTON_VOLUME_UP,
    HAL_HEADSET_BUTTON_VOLUME_DOWN
} HalHeadsetButton;

typedef enum HalHeadsetCommand {
    HAL_HEADSET_TOGGLE_PAUSE,
    HAL_HEADSET_NEXT,
    HAL_HEADSET_PREVIOUS,
    HAL_HEADSET_VOLUME_UP,
    HAL_HEADSET_VOLUME_DOWN
} HalHeadsetCommand;

typedef struct HalHeadsetReading {
    bool known;
    uint16_t level;
    HalHeadsetButton button;
    bool no_mic;
    bool dipped;
    uint16_t lowest;
    uint16_t dip_ms;
    uint8_t dips;
} HalHeadsetReading;

bool hal_input_next_transition(HalKeyTransition *transition);
bool hal_input_waiting(void);
bool hal_input_next_headset_command(HalHeadsetCommand *command);
HalHeadsetReading hal_input_headset_reading(void);

uint16_t hal_input_key_adc(void);
