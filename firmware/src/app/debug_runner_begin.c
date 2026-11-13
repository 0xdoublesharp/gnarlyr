#include "app/debug_loop.h"
#include "debug_runner_state.h"
#include "hal/clock.h"

void debug_runner_begin(void)
{
    debug_runner.next_op = 0;
    debug_runner.line = 0;
    debug_runner.phase = DEBUG_PHASE_NEXT;
    debug_runner.reconnect_due = debug_runner.run.state != DEBUG_SCRIPT_PENDING;
    debug_runner.until_ms = hal_clock_milliseconds() + DEBUG_SCRIPT_END_MS;
    debug_runner_note_state((DebugScriptState)debug_runner.run.state);
}
