#include "sim_script.h"

#include <string.h>

static const char CHORD_PREFIX[] = "play+";

static bool parse_step(const char *text, size_t length, SimScript *script)
{
    size_t prefix = sizeof CHORD_PREFIX - 1;
    bool chord = length > prefix && strncmp(text, CHORD_PREFIX, prefix) == 0;
    size_t skip = chord ? prefix : 0;
    if (!key_script_parse_step(text + skip, length - skip, &script->steps[script->count])) {
        return false;
    }
    script->with_play[script->count++] = chord;
    return true;
}

bool sim_script_parse(const char *text, SimScript *script)
{
    size_t length = strlen(text);
    size_t start = 0;
    script->count = 0;
    while (length > 0 && script->count < SIM_SCRIPT_MAX_STEPS) {
        size_t step = key_script_step_length(text + start, length - start);
        if (!parse_step(text + start, step, script)) {
            script->count = 0;
            return false;
        }
        start += step;
        if (start == length) {
            return true;
        }
        start++;
    }
    script->count = 0;
    return false;
}

int sim_script_schedule(const SimScript *script, uint32_t first_frame, SimScriptEvent *events)
{
    int count = 0;
    uint32_t frame = first_frame;
    for (int index = 0; index < script->count; index++) {
        const KeyScriptStep *step = &script->steps[index];
        uint32_t hold = key_script_hold_ms(step->hold) / SIM_SCRIPT_FRAME_MS;
        bool chord = script->with_play[index];
        if (chord) {
            events[count++] = (SimScriptEvent){.frame = frame, .key = KEY_PLAY, .pressed = true};
            frame += SIM_SCRIPT_GAP_FRAMES;
        }
        events[count++] = (SimScriptEvent){.frame = frame, .key = step->key, .pressed = true};
        events[count++] =
            (SimScriptEvent){.frame = frame + hold, .key = step->key, .pressed = false};
        frame += hold + SIM_SCRIPT_GAP_FRAMES;
        if (chord) {
            events[count++] = (SimScriptEvent){.frame = frame, .key = KEY_PLAY, .pressed = false};
            frame += SIM_SCRIPT_GAP_FRAMES;
        }
    }
    return count;
}
