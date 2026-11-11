#include "debug_steps.h"

#include <stddef.h>

#include "core/key_script.h"
#include "debug_runner_state.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"

enum {
    SOAK_CHECK_MS = 10000,
    MILLISECONDS_PER_MINUTE = 60000,
    MICROSECONDS_PER_MILLISECOND = 1000
};

static void send_key(bool pressed, uint32_t now_ms)
{
    debug_runner.key_down = pressed;
    debug_runner.transition = (HalKeyTransition){debug_runner.key, pressed, now_ms};
    debug_runner.transition_ready = true;
}

static DebugSoakEdge soak_edge(void)
{
    uint32_t levels = hal_system_power_levels() |
                      (hal_audio_status().idle ? 0u : DEBUG_SOAK_AUDIO) |
                      (debug_runner.screen_on ? DEBUG_SOAK_SCREEN : 0u);
    return (DebugSoakEdge){.sample = hal_system_power_samples(),
                           .levels = (uint8_t)levels,
                           .clock_mhz = (uint8_t)hal_system_clock_mhz()};
}

static void measure_soak(DebugSoak *soak)
{
    uint32_t idle_us = hal_system_idle_microseconds();
    soak->idle_ms += (idle_us - debug_runner.idle_us) / MICROSECONDS_PER_MILLISECOND;
    debug_runner.idle_us = idle_us;
}

static void begin_soak(uint32_t now_ms, uint32_t minutes)
{
    if (debug_runner.run.soak_count == DEBUG_SCRIPT_SOAKS) {
        debug_runner_set_status(DEBUG_LINE_FAILED);
        return;
    }
    debug_runner.run.soaks[debug_runner.run.soak_count] =
        (DebugSoak){.line = debug_runner.line, .started_ms = now_ms, .start = soak_edge()};
    debug_runner.until_ms = now_ms + minutes * MILLISECONDS_PER_MINUTE;
    debug_runner.checked_ms = now_ms;
    debug_runner.low_checks = 0;
    debug_runner.key_pressed = false;
    debug_runner.idle_us = hal_system_idle_microseconds();
    debug_runner.phase = DEBUG_PHASE_SOAKING;
}

static void end_soak(uint32_t now_ms, DebugSoakEnd end)
{
    DebugSoak *soak = &debug_runner.run.soaks[debug_runner.run.soak_count++];
    measure_soak(soak);
    soak->finish = soak_edge();
    soak->ended_ms = now_ms;
    soak->end = (uint8_t)end;
    if (end != DEBUG_SOAK_DONE) {
        hal_system_hold_usb_supply_off(false);
    }
    debug_runner_set_status(end == DEBUG_SOAK_DONE ? DEBUG_LINE_OK : DEBUG_LINE_CUT_SHORT);
}

static bool battery_low_for_a_minute(void)
{
    HalBattery battery = hal_power_battery();
    bool low = battery.known && battery.millivolts < DEBUG_SOAK_LOW_MILLIVOLTS;
    debug_runner.low_checks = low ? (uint8_t)(debug_runner.low_checks + 1) : 0;
    return debug_runner.low_checks > DEBUG_SOAK_LOW_MS / SOAK_CHECK_MS;
}

static void continue_soak(uint32_t now_ms)
{
    if (debug_runner.key_pressed) {
        end_soak(now_ms, DEBUG_SOAK_KEY);
    } else if ((int32_t)(now_ms - debug_runner.until_ms) >= 0) {
        end_soak(now_ms, DEBUG_SOAK_DONE);
    } else if (now_ms - debug_runner.checked_ms >= SOAK_CHECK_MS) {
        debug_runner.checked_ms = now_ms;
        measure_soak(&debug_runner.run.soaks[debug_runner.run.soak_count]);
        if (battery_low_for_a_minute()) {
            end_soak(now_ms, DEBUG_SOAK_LOW_BATTERY);
        }
    }
}

void debug_steps_stop(uint32_t now_ms, DebugScriptState state)
{
    if (debug_runner.phase == DEBUG_PHASE_SOAKING) {
        end_soak(now_ms, DEBUG_SOAK_STOPPED);
    }
    if (debug_runner.key_down) {
        send_key(false, now_ms);
    }
    debug_runner_finish(now_ms, state);
}

static void begin_measure(uint32_t now_ms, uint32_t duration_ms)
{
    if (debug_runner.run.measure_count == DEBUG_SCRIPT_MEASURES) {
        debug_runner_set_status(DEBUG_LINE_FAILED);
        return;
    }
    DebugMeasure *measure = &debug_runner.run.measures[debug_runner.run.measure_count];
    *measure = (DebugMeasure){.line = debug_runner.line, .duration_ms = duration_ms};
    hal_audio_reset_stats();
    debug_runner.underruns_before = hal_audio_stats().underruns;
    measure->clock_us = hal_clock_microseconds();
    measure->idle_us = hal_system_idle_microseconds();
    debug_runner.until_ms = now_ms + duration_ms;
    debug_runner.phase = DEBUG_PHASE_MEASURING;
}

static void take_audio(DebugMeasure *measure)
{
    HalAudioStats stats = hal_audio_stats();
    bool counted = stats.underruns >= debug_runner.underruns_before;
    measure->audio_running = stats.elapsed_us != 0;
    measure->buffered_ms = stats.buffered_ms;
    measure->underruns =
        counted ? stats.underruns - debug_runner.underruns_before : stats.underruns;
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
    DebugMeasure *measure = &debug_runner.run.measures[debug_runner.run.measure_count];
    take_audio(measure);
    measure->clock_us = hal_clock_microseconds() - measure->clock_us;
    measure->idle_us = hal_system_idle_microseconds() - measure->idle_us;
    debug_runner.run.measure_count++;
    debug_runner_set_status(DEBUG_LINE_OK);
}

static uint32_t larger(uint32_t first, uint32_t second)
{
    return first > second ? first : second;
}

void debug_steps_frame(DebugFrameSources sources)
{
    DebugMeasure *measure = &debug_runner.run.measures[debug_runner.run.measure_count];
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
    debug_runner_set_status(DEBUG_LINE_OK);
}

static DebugRequest request(DebugRequestKind kind, const DebugOp *op)
{
    const char *text = debug_runner.run.text;
    debug_runner.phase = DEBUG_PHASE_REQUESTED;
    debug_runner.requested = (uint8_t)kind;
    return (DebugRequest){.kind = kind,
                          .argument = {text + op->first, op->first_length},
                          .value = {text + op->second, op->second_length},
                          .slot = debug_runner.run.screenshot_count};
}

static DebugRequest ask_app(DebugRequestKind kind, uint32_t slot)
{
    debug_runner.phase = DEBUG_PHASE_REQUESTED;
    debug_runner.requested = (uint8_t)kind;
    return (DebugRequest){.kind = kind, .slot = slot};
}

static void press_key(const DebugOp *op, uint32_t now_ms)
{
    debug_runner.key = (Key)op->first_length;
    debug_runner.until_ms = now_ms + op->first;
    debug_runner.phase = DEBUG_PHASE_KEY_HELD;
    send_key(true, now_ms);
}

static void reconnect_now(uint32_t now_ms)
{
    debug_runner_set_status(DEBUG_LINE_OK);
    debug_steps_stop(now_ms, DEBUG_SCRIPT_FINISHED);
    hal_system_usb_reconnect();
}

_Static_assert(DEBUG_OP_POKE - DEBUG_OP_PEEK == HAL_PROBE_POKE &&
                   DEBUG_OP_PIN - DEBUG_OP_PEEK == HAL_PROBE_PIN &&
                   DEBUG_OP_DAC - DEBUG_OP_PEEK == HAL_PROBE_DAC,
               "probe ops are numbered like the probes they ask for");

static void probe(const DebugOp *op, uint32_t now_ms)
{
    DebugScriptRun *run = &debug_runner.run;
    if (run->probe_count == DEBUG_SCRIPT_PROBES) {
        debug_runner_set_status(DEBUG_LINE_FAILED);
        return;
    }
    bool words = op->kind != DEBUG_OP_PIN;
    HalProbe result = hal_system_probe((HalProbeKind)(op->kind - DEBUG_OP_PEEK),
                                       words ? run->words[op->first] : op->first,
                                       words ? run->words[op->second] : op->second);
    if (result.allowed) {
        run->probes[run->probe_count++] = (DebugProbe){.line = op->line,
                                                       .levels = result.levels,
                                                       .time_ms = now_ms,
                                                       .before = result.before,
                                                       .after = result.after};
    }
    debug_runner_set_status(result.allowed ? DEBUG_LINE_OK : DEBUG_LINE_REFUSED);
}

_Static_assert((int)DEBUG_RTC_REGISTERS == (int)HAL_RTC_REGISTERS,
               "a read keeps every register the board gives");

static HalRtcRead rtc_action(DebugRtcAction action, uint32_t *written_s)
{
    HalWallTime wall = hal_clock_wall();
    *written_s = 0;
    if (action == DEBUG_RTC_READ) {
        return hal_system_rtc_read();
    }
    if (wall.source == HAL_WALL_UNKNOWN) {
        return (HalRtcRead){.status = HAL_RTC_NO_TIME};
    }
    *written_s = wall.utc_s;
    return hal_system_rtc_set(wall.utc_s);
}

static void use_rtc(const DebugOp *op, uint32_t now_ms)
{
    DebugScriptRun *run = &debug_runner.run;
    if (run->rtc_count == DEBUG_SCRIPT_RTC_READS) {
        debug_runner_set_status(DEBUG_LINE_FAILED);
        return;
    }
    uint32_t written_s = 0;
    HalRtcRead result = rtc_action((DebugRtcAction)op->first, &written_s);
    DebugRtcRead *read = &run->rtc[run->rtc_count++];
    *read = (DebugRtcRead){.line = debug_runner.line,
                           .status = (uint8_t)result.status,
                           .action = (uint8_t)op->first,
                           .time_ms = now_ms,
                           .written_s = written_s};
    for (uint32_t index = 0; index < DEBUG_RTC_REGISTERS; index++) {
        read->registers[index] = result.registers[index];
    }
    debug_runner_set_status(result.status == HAL_RTC_OK ? DEBUG_LINE_OK : DEBUG_LINE_FAILED);
}

static DebugRequest run_op(const DebugOp *op, uint32_t now_ms)
{
    switch (op->kind) {
    case DEBUG_OP_KEY:
        press_key(op, now_ms);
        break;
    case DEBUG_OP_WAIT:
        debug_runner.until_ms = now_ms + op->first;
        debug_runner.phase = DEBUG_PHASE_WAITING;
        break;
    case DEBUG_OP_MEASURE:
        begin_measure(now_ms, op->first);
        break;
    case DEBUG_OP_PROFILE:
        apply_profile(op->first);
        break;
    case DEBUG_OP_USB_SENSE:
        hal_system_keep_usb_on(op->first != 0);
        debug_runner_set_status(DEBUG_LINE_OK);
        break;
    case DEBUG_OP_SET:
        return request(DEBUG_REQUEST_SET, op);
    case DEBUG_OP_SCREENSHOT:
        if (debug_runner.run.screenshot_count < DEBUG_SCRIPT_SCREENSHOTS) {
            return request(DEBUG_REQUEST_SCREENSHOT, op);
        }
        debug_runner_set_status(DEBUG_LINE_FAILED);
        break;
    case DEBUG_OP_RECONNECT:
        reconnect_now(now_ms);
        break;
    case DEBUG_OP_SUPPLY:
        hal_system_hold_usb_supply_off(op->first == 0);
        debug_runner_set_status(DEBUG_LINE_OK);
        break;
    case DEBUG_OP_SCREEN:
        return ask_app(DEBUG_REQUEST_SCREEN, op->first);
    case DEBUG_OP_STANDBY:
        return ask_app(DEBUG_REQUEST_STANDBY, op->first);
    case DEBUG_OP_SOAK:
        begin_soak(now_ms, op->first);
        break;
    case DEBUG_OP_RTC:
        use_rtc(op, now_ms);
        break;
    default:
        probe(op, now_ms);
        break;
    }
    return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
}

static void enter_line(uint32_t line)
{
    if (line != debug_runner.line) {
        debug_runner.line = (uint8_t)line;
        hal_system_script_mark(line);
        debug_loop_event(DEBUG_EVENT_SCRIPT, line);
    }
}

static DebugRequest next_op(uint32_t now_ms)
{
    if (debug_runner.next_op >= debug_runner.run.op_count) {
        debug_steps_stop(now_ms, DEBUG_SCRIPT_FINISHED);
        debug_runner.reconnect_due = true;
        debug_runner.until_ms = now_ms + DEBUG_SCRIPT_END_MS;
        return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
    }
    const DebugOp *op = &debug_runner.run.ops[debug_runner.next_op++];
    enter_line(op->line);
    return run_op(op, now_ms);
}

static bool reached(uint32_t now_ms)
{
    return (int32_t)(now_ms - debug_runner.until_ms) >= 0;
}

static DebugRequest advance(uint32_t now_ms)
{
    if (debug_runner.phase == DEBUG_PHASE_NEXT) {
        return next_op(now_ms);
    }
    if (debug_runner.phase == DEBUG_PHASE_SOAKING) {
        continue_soak(now_ms);
        return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
    }
    if (!reached(now_ms)) {
        return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
    }
    if (debug_runner.phase == DEBUG_PHASE_KEY_HELD) {
        send_key(false, now_ms);
        debug_runner.until_ms = now_ms + KEY_SCRIPT_GAP_MS;
        debug_runner.phase = DEBUG_PHASE_KEY_GAP;
    } else if (debug_runner.phase == DEBUG_PHASE_MEASURING) {
        end_measure();
    } else if (debug_runner.phase != DEBUG_PHASE_REQUESTED) {
        debug_runner_set_status(DEBUG_LINE_OK);
    }
    return (DebugRequest){.kind = DEBUG_REQUEST_NONE};
}

static DebugRequest start(uint32_t now_ms)
{
    debug_runner.run.started_ms = now_ms;
    debug_runner_note_state(DEBUG_SCRIPT_RUNNING);
    return (DebugRequest){.kind = DEBUG_REQUEST_START};
}

DebugRequest debug_steps_step(uint32_t now_ms, DebugRunnerView view)
{
    DebugRequest none = {.kind = DEBUG_REQUEST_NONE};
    bool held = view.held;
    debug_runner.screen_on = view.screen_on;
    if (debug_runner.run.state == DEBUG_SCRIPT_PENDING) {
        return held ? none : start(now_ms);
    }
    if (now_ms - debug_runner.run.started_ms >= DEBUG_SCRIPT_LIMIT_MS + debug_runner.run.soak_ms) {
        debug_steps_stop(now_ms, DEBUG_SCRIPT_TIMED_OUT);
        hal_system_usb_reconnect();
        return none;
    }
    return held ? none : advance(now_ms);
}
