#pragma once

#include "device/audio_effects.h"
#include "device/audio_fade.h"
#include "device/power_history.h"

#define AUDIO_EFFECTS_BASE 0x03092000u
#define AUDIO_EFFECTS_BYTES 0x2000u
#define AUDIO_EFFECTS_NUMBER 12u
#define AUDIO_EFFECTS_MAGIC 0x0C4D5344u

typedef struct DebugBatteryTrace {
    uint32_t count;
    uint32_t idle_us;
} DebugBatteryTrace;

typedef struct AudioEffectsEntry {
    uint32_t magic;
    void (*fade_in)(AudioFadeBlock block, AudioFadeRamp *ramp);
    void (*fade_out)(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining);
    void (*effects_design)(AudioEffects *effects, uint32_t rate);
    uint32_t (*effects_run)(AudioEffects *effects, const AudioEffectsBlock *block);
    void (*effects_settle)(AudioEffects *effects, AudioSegment *segments, int count,
                           uint32_t consumed);
#ifdef ECHO_DEBUG_LOOP
    const void *debug;
    void (*battery_trace)(DebugBatteryTrace *trace, uint32_t now_ms, PowerSample sample);
#endif
} AudioEffectsEntry;

void debug_battery_trace_write(DebugBatteryTrace *trace, uint32_t now_ms, PowerSample sample);
