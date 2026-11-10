#include "debug_runner.h"

#include <stddef.h>

#include "core/key_script.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/system.h"

typedef enum Phase {
    PHASE_NEXT,
    PHASE_KEY_HELD,
    PHASE_KEY_GAP,
    PHASE_WAITING,
    PHASE_MEASURING,
    PHASE_REQUESTED
} Phase;

typedef struct Runner {
    DebugScriptRun run;
    uint8_t next_op;
    uint8_t line;
    uint8_t phase;
    uint8_t requested;
    Key key;
    bool key_down;
    bool reconnect_due;
    bool transition_ready;
    HalKeyTransition transition;
    uint32_t until_ms;
    uint32_t underruns_before;
} Runner;

static Runner runner;

DebugScriptRun *debug_runner_script(void)
{
    return &runner.run;
}

bool debug_runner_active(void)
{
    return runner.run.state == DEBUG_SCRIPT_PENDING || runner.run.state == DEBUG_SCRIPT_RUNNING;
}

static void note_state(DebugScriptState state)
{
    runner.run.state = (uint8_t)state;
    debug_loop_event(DEBUG_EVENT_SCRIPT, DEBUG_SCRIPT_EVENT_STATE + (uint32_t)state);
}

void debug_runner_begin(void)
{
    runner.next_op = 0;
    runner.line = 0;
    runner.phase = PHASE_NEXT;
    runner.reconnect_due = runner.run.state != DEBUG_SCRIPT_PENDING;
    runner.until_ms = hal_clock_milliseconds() + DEBUG_SCRIPT_END_MS;
    note_state((DebugScriptState)runner.run.state);
}

static void send_key(bool pressed, uint32_t now_ms)
{
    runner.key_down = pressed;
    runner.transition = (HalKeyTransition){runner.key, pressed, now_ms};
    runner.transition_ready = true;
}

bool debug_runner_next_key(HalKeyTransition *transition)
{
    if (!runner.transition_ready) {
        return false;
    }
    *transition = runner.transition;
    runner.transition_ready = false;
    return true;
}

static void stop(uint32_t now_ms, DebugScriptState state)
{
    if (runner.key_down) {
        send_key(false, now_ms);
    }
    runner.run.finished_ms = now_ms;
    hal_system_script_mark(0);
    note_state(state);
}

void debug_runner_abort(void)
{
    if (debug_runner_active()) {
        stop(hal_clock_milliseconds(), DEBUG_SCRIPT_ABORTED);
    }
}

static void set_status(DebugLineStatus status)
{
    runner.run.status[runner.line - 1] = (uint8_t)status;
    runner.phase = PHASE_NEXT;
}

static void begin_measure(uint32_t now_ms, uint32_t duration_ms)
{
    if (runner.run.measure_count == DEBUG_SCRIPT_MEASURES) {
        set_status(DEBUG_LINE_FAILED);
        return;
    }
    DebugMeasure *measure = &runner.run.measures[runner.run.measure_count];
    *measure = (DebugMeasure){.line = runner.line, .duration_ms = duration_ms};
    hal_audio_reset_stats();
    runner.underruns_before = hal_audio_stats().underruns;
    measure->clock_us = hal_clock_microseconds();
    measure->idle_us = hal_system_idle_microseconds();
    runner.until_ms = now_ms + duration_ms;
    runner.phase = PHASE_MEASURING;
}

static void take_audio(DebugMeasure *measure)
{
    HalAudioStats stats = hal_audio_stats();
    bool counted = stats.underruns >= runner.underruns_before;
    measure->audio_running = stats.elapsed_us != 0;
    measure->buffered_ms = stats.buffered_ms;
    measure->underruns = counted ? stats.underruns - runner.underruns_before : stats.underruns;
    measure->output_underruns = stats.output_underruns;
    measure->blocks = stats.blocks;
    measure->handoff_us = stats.decode_total_us;
    measure->handoff_max_us = stats.decode_max_us;
    measure->read_us = stats.read_total_us;
    measure->read_max_us = stats.read_max_us;
    measure->audio_busy_us = stats.busy_us;
    measure->audio_elapsed_us = stats.elapsed_us;
}

static void end_measure(void)
{
    DebugMeasure *measure = &runner.run.measures[runner.run.measure_count];
    take_audio(measure);
    measure->clock_us = hal_clock_microseconds() - measure->clock_us;
    measure->idle_us = hal_system_idle_microseconds() - measure->idle_us;
    runner.run.measure_count++;
    set_status(DEBUG_LINE_OK);
}

static uint32_t larger(uint32_t first, uint32_t second)
{
    return first > second ? first : second;
}

void debug_runner_frame(DebugFrameSources sources)
{
    if (runner.run.state != DEBUG_SCRIPT_RUNNING || runner.phase != PHASE_MEASURING) {
        return;
    }
    DebugMeasure *measure = &runner.run.measures[runner.run.measure_count];
    uint32_t frame_us = frame_stats_latest_us(sources.total);
    measure->frames++;
    measure->frame_us += frame_us;
    measure->frame_worst_us = larger(measure->frame_worst_us, frame_us);
    measure->update_us += frame_stats_latest_us(sources.update);
    measure->draw_us += frame_stats_latest_us(sources.draw);
    measure->send_us += frame_stats_latest_us(sources.send);
}

static void apply_profile(uint32_t action)
{
    if (action == DEBUG_PROFILE_RESET) {
        hal_system_reset_profile();
    } else {
        hal_system_set_profiling(action == DEBUG_PROFILE_ON);
    }
    set_status(DEBUG_LINE_OK);
}

static DebugRequest request(DebugRequestKind kind, const DebugOp *op)
{
    const char *text = runner.run.text;
    runner.phase = PHASE_REQUESTED;
    runner.requested = (uint8_t)kind;
    return (DebugRequest){.kind = kind,
                          .argument = {text + op->first, op->first_length},
                          .value = {text + op->second, op->second_length},
                          .slot = runner.run.screenshot_count};
}

static void press_key(const DebugOp *op, uint32_t now_ms)
{
    runner.key = (Key)op->first_length;
    runner.until_ms = now_ms + op->first;
    runner.phase = PHASE_KEY_HELD;
    send_key(true, now_ms);
}

static void reconnect_now(uint32_t now_ms)
{
    set_status(DEBUG_LINE_OK);
    stop(now_ms, DEBUG_SCRIPT_FINISHED);
    hal_system_usb_reconnect();
}

static DebugRequest run_op(const DebugOp *op, uint32_t now_ms)
{
    switch (op->kind) {
    case DEBUG_OP_KEY:
        press_key(op, now_ms);
        break;
    case DEBUG_OP_WAIT:
        runner.until_ms = now_ms + op->first;
        runner.phase = PHASE_WAITING;
        break;
    case DEBUG_OP_MEASURE:
        begin_measure(now_ms, op->first);
        break;
    case DEBUG_OP_PROFILE:
        apply_profile(op->first);
        break;
    case DEBUG_OP_USB_SENSE:
        hal_system_keep_usb_on(op->first != 0);
        set_status(DEBUG_LINE_OK);
        break;
    case DEBUG_OP_SET:
        return request(DEBUG_REQUEST_SET, op);
    case DEBUG_OP_SCREENSHOT:
        if (runner.run.screenshot_count < DEBUG_SCRIPT_SCREENSHOTS) {
            return request(DEBUG_REQUEST_SCREENSHOT, op);
        }
        set_status(DEBUG_LINE_FAILED);
        break;
    default:
        reconnect_now(now_ms);
        break;
    }
    return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
}

static void enter_line(uint32_t line)
{
    if (line != runner.line) {
        runner.line = (uint8_t)line;
        hal_system_script_mark(line);
        debug_loop_event(DEBUG_EVENT_SCRIPT, line);
    }
}

static DebugRequest next_op(uint32_t now_ms)
{
    if (runner.next_op >= runner.run.op_count) {
        stop(now_ms, DEBUG_SCRIPT_FINISHED);
        runner.reconnect_due = true;
        runner.until_ms = now_ms + DEBUG_SCRIPT_END_MS;
        return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
    }
    const DebugOp *op = &runner.run.ops[runner.next_op++];
    enter_line(op->line);
    return run_op(op, now_ms);
}

void debug_runner_done(bool succeeded)
{
    if (runner.phase != PHASE_REQUESTED) {
        return;
    }
    if (succeeded && runner.requested == DEBUG_REQUEST_SCREENSHOT) {
        runner.run.screenshot_lines[runner.run.screenshot_count++] = runner.line;
    }
    set_status(succeeded ? DEBUG_LINE_OK : DEBUG_LINE_FAILED);
}

static bool reached(uint32_t now_ms)
{
    return (int32_t)(now_ms - runner.until_ms) >= 0;
}

static DebugRequest advance(uint32_t now_ms)
{
    if (runner.phase == PHASE_NEXT) {
        return next_op(now_ms);
    }
    if (!reached(now_ms)) {
        return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
    }
    if (runner.phase == PHASE_KEY_HELD) {
        send_key(false, now_ms);
        runner.until_ms = now_ms + KEY_SCRIPT_GAP_MS;
        runner.phase = PHASE_KEY_GAP;
    } else if (runner.phase == PHASE_MEASURING) {
        end_measure();
    } else if (runner.phase != PHASE_REQUESTED) {
        set_status(DEBUG_LINE_OK);
    }
    return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
}

static DebugRequest start(uint32_t now_ms)
{
    runner.run.started_ms = now_ms;
    note_state(DEBUG_SCRIPT_RUNNING);
    return (DebugRequest){.kind = DEBUG_REQUEST_START};
}

static void reconnect_when_due(uint32_t now_ms)
{
    if (runner.reconnect_due && reached(now_ms)) {
        runner.reconnect_due = false;
        hal_system_usb_reconnect();
    }
}

DebugRequest debug_runner_step(uint32_t now_ms, bool held)
{
    DebugRequest none = {.kind = DEBUG_REQUEST_NONE};
    if (runner.run.state == DEBUG_SCRIPT_PENDING) {
        return held ? none : start(now_ms);
    }
    if (runner.run.state != DEBUG_SCRIPT_RUNNING) {
        reconnect_when_due(now_ms);
        return none;
    }
    if (now_ms - runner.run.started_ms >= DEBUG_SCRIPT_LIMIT_MS) {
        stop(now_ms, DEBUG_SCRIPT_TIMED_OUT);
        hal_system_usb_reconnect();
        return none;
    }
    return held ? none : advance(now_ms);
}
