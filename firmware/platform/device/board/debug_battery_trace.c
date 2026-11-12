#include <stdint.h>

#include "board.h"
#include "device/audio_effects_module.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "registers.h"

enum {
    TRACE_RECORDS = 2048,
    TRACE_WORDS = 6,
    TRACE_MAGIC = 0x44534254u
};

void debug_battery_trace_write(DebugBatteryTrace *trace, uint32_t now_ms, PowerSample sample)
{
    if (trace->count >= TRACE_RECORDS) {
        return;
    }
    uint32_t idle_us = hal_system_idle_microseconds();
    uint32_t select = register_read(CRU_BASE + 0x14u);
    uint32_t mhz = hal_system_clock_mhz();
    uint32_t words[TRACE_WORDS] = {
        now_ms,
        idle_us - trace->idle_us,
        mhz / ((select & 7u) + 1u) |
            (board_core1_streaming() ? (mhz / ((select >> 12 & 7u) + 1u)) << 16 : 0u),
        sample.millivolts | (uint32_t)sample.percent << 16 | (uint32_t)sample.flags << 24,
        trace->count + 1u,
        TRACE_MAGIC};
    for (uint32_t index = 0; index < TRACE_WORDS - 1; index++) {
        words[TRACE_WORDS - 1] ^= words[index];
    }
    if (hal_internal_file_write_at("/" FIRMWARE_DATA_DIRECTORY "/debug/battery.bin",
                                   trace->count * sizeof words, words, sizeof words)) {
        trace->count++;
        trace->idle_us = idle_us;
    }
}
