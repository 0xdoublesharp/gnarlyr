#pragma once

#include "gpio.h"
#include "hal/audio.h"

extern const GpioPin BOARD_JACK_SINGLE_ENDED;
extern const GpioPin BOARD_JACK_BALANCED;

HalJackPins board_jack_boot_pins(void);
