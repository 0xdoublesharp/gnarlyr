#include "sim_script.h"

#include <string.h>

bool sim_script_parse(const char *text, SimScript *script)
{
    script->count = key_script_parse(text, strlen(text), script->steps, SIM_SCRIPT_MAX_STEPS);
    return script->count > 0;
}

int sim_script_schedule(const SimScript *script, uint32_t first_frame, SimScriptEvent *events)
{
    int count = 0;
    uint32_t frame = first_frame;
    for (int index = 0; index < script->count; index++) {
        const KeyScriptStep *step = &script->steps[index];
        uint32_t hold = key_script_hold_ms(step->hold) / SIM_SCRIPT_FRAME_MS;
        events[count++] = (SimScriptEvent){.frame = frame, .key = step->key, .pressed = true};
        events[count++] =
            (SimScriptEvent){.frame = frame + hold, .key = step->key, .pressed = false};
        frame += hold + SIM_SCRIPT_GAP_FRAMES;
    }
    return count;
}
