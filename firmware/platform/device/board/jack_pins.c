#include "gpio.h"
#include "hal/audio.h"
#include "jack.h"

HalJackPins hal_audio_jack_pins(void)
{
    HalJackPins pins = board_jack_boot_pins();
    pins.single_ended.high = gpio_read(BOARD_JACK_SINGLE_ENDED);
    pins.balanced.high = gpio_read(BOARD_JACK_BALANCED);
    return pins;
}
