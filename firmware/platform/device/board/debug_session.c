#include <stdbool.h>
#include <stdint.h>

#include "app/debug_loop.h"
#include "debug_probe.h"
#include "hal/audio.h"
#include "hal/system.h"

static DebugReportAudio audio;

void board_debug_usb_starting(bool dac)
{
    audio.state = hal_audio_status().state;
    audio.stats = hal_audio_stats();
    audio.running = audio.stats.elapsed_us != 0;
    debug_loop_event(DEBUG_EVENT_USB_START, dac);
}

void board_debug_usb_ended(void)
{
    debug_loop_event(DEBUG_EVENT_USB_END, (uint32_t)hal_system_usb_last().end);
}

const DebugReportAudio *board_debug_audio(void)
{
    return &audio;
}
