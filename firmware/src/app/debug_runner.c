#include "debug_runner.h"

#include <stddef.h>

#include "core/deadline.h"
#include "debug_runner_state.h"
#include "hal/clock.h"
#include "hal/system.h"

DebugRunnerState debug_runner;

DebugScriptRun *debug_runner_script(void)
{
    return &debug_runner.run;
}

bool debug_runner_active(void)
{
    return debug_runner.run.state == DEBUG_SCRIPT_PENDING ||
           debug_runner.run.state == DEBUG_SCRIPT_RUNNING;
}

void debug_runner_note_state(DebugScriptState state)
{
    debug_runner.run.state = (uint8_t)state;
    debug_loop_event(DEBUG_EVENT_SCRIPT, DEBUG_SCRIPT_EVENT_STATE + (uint32_t)state);
}

bool debug_runner_next_key(HalKeyTransition *transition)
{
    if (!debug_runner.transition_ready) {
        return false;
    }
    *transition = debug_runner.transition;
    debug_runner.transition_ready = false;
    return true;
}

void debug_runner_key_pressed(void)
{
    debug_runner.key_pressed = true;
}

void debug_runner_finish(uint32_t now_ms, DebugScriptState state)
{
    hal_system_hold_usb_supply_off(false);
    debug_runner.run.finished_ms = now_ms;
    hal_system_script_mark(0);
    debug_runner_note_state(state);
}

static const DebugCodeEntry *debug_code(void)
{
    return (const DebugCodeEntry *)hal_system_load_debug_code();
}

static const DebugCodeEntry *running_code(void)
{
    return debug_runner.run.state == DEBUG_SCRIPT_RUNNING ? debug_code() : NULL;
}

void debug_runner_abort(void)
{
    debug_runner.reconnect_due = false;
    if (!debug_runner_active()) {
        return;
    }
    uint32_t now_ms = hal_clock_milliseconds();
    const DebugCodeEntry *code = running_code();
    if (code != NULL) {
        code->stop(now_ms, DEBUG_SCRIPT_ABORTED);
    } else {
        debug_runner_finish(now_ms, DEBUG_SCRIPT_ABORTED);
    }
}

static bool reached(uint32_t now_ms)
{
    return deadline_reached(now_ms, debug_runner.until_ms);
}

static void reconnect_when_due(uint32_t now_ms)
{
    if (debug_runner.reconnect_due && reached(now_ms)) {
        debug_runner.reconnect_due = false;
        hal_system_usb_reconnect();
    }
}

void debug_runner_step(uint32_t now_ms, DebugRunnerView view, const DebugAppHooks *app)
{
    if (!debug_runner_active()) {
        if (!view.power_down) {
            reconnect_when_due(now_ms);
        }
        return;
    }
    const DebugCodeEntry *code = debug_code();
    if (code == NULL) {
        debug_runner_finish(now_ms, DEBUG_SCRIPT_ABORTED);
        debug_runner.reconnect_due = true;
        debug_runner.until_ms = now_ms + DEBUG_SCRIPT_END_MS;
        return;
    }
    code->step(now_ms, view, app);
}

void debug_runner_frame(DebugFrameSources sources)
{
    if (debug_runner.phase != DEBUG_PHASE_MEASURING) {
        return;
    }
    const DebugCodeEntry *code = running_code();
    if (code != NULL) {
        code->frame(sources);
    }
}

bool debug_runner_screenshot(const DebugScreenshot *shot)
{
    const DebugCodeEntry *code = debug_code();
    return code != NULL && code->screenshot(shot);
}
