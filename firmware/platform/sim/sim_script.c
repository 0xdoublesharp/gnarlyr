#include "sim_script.h"

#include <string.h>

static const char PLAY_PREFIX[] = "play+";
static const char MENU_PREFIX[] = "menu+";

static bool prefixed(const char *text, size_t length, const char *prefix)
{
    size_t size = strlen(prefix);
    return length > size && strncmp(text, prefix, size) == 0;
}

static bool parse_step(const char *text, size_t length, SimScript *script)
{
    uint8_t seed = prefixed(text, length, PLAY_PREFIX)   ? KEY_CHORD_PLAY
                   : prefixed(text, length, MENU_PREFIX) ? KEY_CHORD_MENU
                                                         : KEY_CHORD_NONE;
    size_t skip = seed == KEY_CHORD_NONE ? 0 : sizeof PLAY_PREFIX - 1;
    if (!key_script_parse_step(text + skip, length - skip, &script->steps[script->count])) {
        return false;
    }
    script->seed[script->count++] = seed;
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
        bool chord = script->seed[index] != KEY_CHORD_NONE;
        Key seed = script->seed[index] == KEY_CHORD_MENU ? KEY_MENU : KEY_PLAY;
        if (chord) {
            events[count++] = (SimScriptEvent){.frame = frame, .key = seed, .pressed = true};
            frame += SIM_SCRIPT_GAP_FRAMES;
        }
        events[count++] = (SimScriptEvent){.frame = frame, .key = step->key, .pressed = true};
        events[count++] =
            (SimScriptEvent){.frame = frame + hold, .key = step->key, .pressed = false};
        frame += hold + SIM_SCRIPT_GAP_FRAMES;
        if (chord) {
            events[count++] = (SimScriptEvent){.frame = frame, .key = seed, .pressed = false};
            frame += SIM_SCRIPT_GAP_FRAMES;
        }
    }
    return count;
}
