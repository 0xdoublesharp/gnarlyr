#include "board.h"
#include "device/headset_monitor.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "irq.h"

HalHeadsetReading hal_input_headset_reading(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    uint32_t primask = irq_save();
    HalHeadsetReading reading = headset_monitor_reading(board_input_watch_headset(), now_ms);
    irq_restore(primask);
    return reading;
}
