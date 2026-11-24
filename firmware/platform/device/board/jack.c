#include "jack.h"
#include "board.h"
#include "core/seam.h"
#include "dac.h"
#include "device/jack_sense.h"
#include "gpio.h"
#include "hal/audio.h"
#include "registers.h"

#define GRF_PORT0_PULL (GRF_BASE + 0x14u)
#define GRF_PORT2_PULL (GRF_BASE + 0x54u)
#define PORT0_JACK_PULL 0x00200020u
#define PORT2_JACK_PULL 0x00010001u

const GpioPin BOARD_JACK_SINGLE_ENDED = {0, 13};
const GpioPin BOARD_JACK_BALANCED = {2, 8};

static bool pins_ready;
static HalJackPins boot_state;
static JackSense sense;

static HalJackPin boot_pin(GpioPin pin)
{
    return (HalJackPin){.high = false,
                        .boot_function = (uint8_t)gpio_function(pin),
                        .boot_output = gpio_is_output(pin),
                        .pull = gpio_pull_enabled(pin)};
}

CORE_COLD static void prepare_pins(void)
{
    if (pins_ready) {
        return;
    }
    boot_state = (HalJackPins){.single_ended = boot_pin(BOARD_JACK_SINGLE_ENDED),
                               .balanced = boot_pin(BOARD_JACK_BALANCED)};
    register_write(GRF_PORT0_PULL, PORT0_JACK_PULL);
    register_write(GRF_PORT2_PULL, PORT2_JACK_PULL);
    gpio_mux(BOARD_JACK_SINGLE_ENDED, GPIO_FUNCTION_GPIO);
    gpio_input(BOARD_JACK_SINGLE_ENDED);
    gpio_mux(BOARD_JACK_BALANCED, GPIO_FUNCTION_GPIO);
    gpio_input(BOARD_JACK_BALANCED);
    pins_ready = true;
}

CORE_COLD HalJackPins board_jack_boot_pins(void)
{
    prepare_pins();
    return boot_state;
}

CORE_SEAM CORE_COLD bool board_jack_poll(void)
{
    prepare_pins();
    if (board_bluetooth_routed() || board_bluetooth_settling()) {
        return false;
    }
    JackKind reading =
        board_jack_kind(gpio_read(BOARD_JACK_SINGLE_ENDED), gpio_read(BOARD_JACK_BALANCED));
    if (sense.started && reading != sense.stable) {
        dac_path_guard(true);
    }
    return jack_sense_sample(&sense, reading);
}

bool board_jack_headset_ready(void)
{
    return sense.started && sense.stable == JACK_SINGLE_ENDED && !board_bluetooth_routed() &&
           !board_bluetooth_settling();
}

HalAudioJack hal_audio_jack(void)
{
    if (!sense.started) {
        board_jack_poll();
    }
    switch (sense.stable) {
    case JACK_SINGLE_ENDED:
        return HAL_AUDIO_JACK_SINGLE_ENDED;
    case JACK_BALANCED:
        return HAL_AUDIO_JACK_BALANCED;
    case JACK_NONE:
        break;
    }
    return HAL_AUDIO_JACK_NONE;
}

CORE_SEAM bool board_jack_settling(void)
{
    return sense.matches != 0;
}
