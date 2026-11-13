#include "core/debug_script.h"

#include "core/key_script.h"

enum {
    MILLISECONDS_PER_MINUTE = 60000
};

static bool build_matches(DebugSpan tag, const char *version)
{
    while (*version != '\0' && *version != '-') {
        version++;
    }
    return debug_span_matches(tag, "any") ||
           (*version == '-' && debug_span_matches(tag, version + 1));
}

static DebugOp *new_op(DebugScriptRun *run, DebugOpKind kind, uint32_t line)
{
    if (run->op_count == DEBUG_SCRIPT_OPS) {
        run->state = DEBUG_SCRIPT_REFUSED_TOO_LONG;
        return &run->ops[DEBUG_SCRIPT_OPS - 1];
    }
    DebugOp *op = &run->ops[run->op_count++];
    *op = (DebugOp){.kind = (uint8_t)kind, .line = (uint8_t)line};
    return op;
}

static void add_keys(DebugScriptRun *run, DebugSpan keys, uint32_t line)
{
    size_t start = 0;
    while (start < keys.length) {
        KeyScriptStep step;
        size_t length = key_script_step_length(keys.text + start, keys.length - start);
        key_script_parse_step(keys.text + start, length, &step);
        DebugOp *op = new_op(run, DEBUG_OP_KEY, line);
        op->first = (uint16_t)key_script_hold_ms(step.hold);
        op->first_length = (uint8_t)step.key;
        start += length + 1;
    }
}

static void add_spans(DebugScriptRun *run, DebugOpKind kind, const DebugCommand *command,
                      uint32_t line)
{
    DebugOp *op = new_op(run, kind, line);
    op->first = (uint16_t)(command->argument.text - run->text);
    op->first_length = (uint8_t)command->argument.length;
    op->second = (uint16_t)(command->value.text != NULL ? command->value.text - run->text : 0);
    op->second_length = (uint8_t)command->value.length;
}

static void add_number(DebugScriptRun *run, DebugOpKind kind, uint32_t number, uint32_t line)
{
    new_op(run, kind, line)->first = (uint16_t)number;
}

static uint16_t add_word(DebugScriptRun *run, uint32_t word)
{
    if (run->word_count == DEBUG_SCRIPT_WORDS) {
        run->state = DEBUG_SCRIPT_REFUSED_TOO_LONG;
        return 0;
    }
    run->words[run->word_count] = word;
    return run->word_count++;
}

static bool add_probe(DebugScriptRun *run, const DebugCommand *command, uint32_t line)
{
    DebugOp *op;
    switch (command->kind) {
    case DEBUG_COMMAND_PEEK:
        new_op(run, DEBUG_OP_PEEK, line)->first = add_word(run, command->number);
        return true;
    case DEBUG_COMMAND_POKE:
        op = new_op(run, DEBUG_OP_POKE, line);
        op->first = add_word(run, command->number);
        op->second = add_word(run, command->second);
        return true;
    case DEBUG_COMMAND_DAC:
        new_op(run, DEBUG_OP_DAC, line)->first = add_word(run, command->number);
        return true;
    case DEBUG_COMMAND_GPIO:
        op = new_op(run, DEBUG_OP_PIN, line);
        op->first = (uint16_t)command->number;
        op->second = (uint16_t)command->second;
        return true;
    default:
        return false;
    }
}

static bool add_numbered(DebugScriptRun *run, const DebugCommand *command, uint32_t line)
{
    static const uint8_t OPS[DEBUG_COMMAND_UNKNOWN] = {
        [DEBUG_COMMAND_WAIT] = 1 + DEBUG_OP_WAIT,
        [DEBUG_COMMAND_MEASURE] = 1 + DEBUG_OP_MEASURE,
        [DEBUG_COMMAND_PROFILE] = 1 + DEBUG_OP_PROFILE,
        [DEBUG_COMMAND_USB_SENSE] = 1 + DEBUG_OP_USB_SENSE,
        [DEBUG_COMMAND_RECONNECT] = 1 + DEBUG_OP_RECONNECT,
        [DEBUG_COMMAND_SUPPLY] = 1 + DEBUG_OP_SUPPLY,
        [DEBUG_COMMAND_SCREEN] = 1 + DEBUG_OP_SCREEN,
        [DEBUG_COMMAND_SOAK] = 1 + DEBUG_OP_SOAK,
        [DEBUG_COMMAND_RTC] = 1 + DEBUG_OP_RTC,
        [DEBUG_COMMAND_STANDBY] = 1 + DEBUG_OP_STANDBY,
        [DEBUG_COMMAND_RESCAN] = 1 + DEBUG_OP_RESCAN,
        [DEBUG_COMMAND_OPEN] = 1 + DEBUG_OP_OPEN};
    if (command->kind >= DEBUG_COMMAND_UNKNOWN || OPS[command->kind] == 0) {
        return false;
    }
    add_number(run, (DebugOpKind)(OPS[command->kind] - 1), command->number, line);
    if (command->kind == DEBUG_COMMAND_SOAK) {
        run->soak_ms += command->number * MILLISECONDS_PER_MINUTE;
    }
    return true;
}

static void add_command(DebugScriptRun *run, const DebugCommand *command, uint32_t line)
{
    if (add_probe(run, command, line) || add_numbered(run, command, line)) {
        return;
    }
    switch (command->kind) {
    case DEBUG_COMMAND_KEYS:
        add_keys(run, command->argument, line);
        break;
    case DEBUG_COMMAND_SET:
        add_spans(run, DEBUG_OP_SET, command, line);
        break;
    case DEBUG_COMMAND_SCREENSHOT:
        add_spans(run, DEBUG_OP_SCREENSHOT, command, line);
        break;
    case DEBUG_COMMAND_BUILD:
        run->status[line - 1] = DEBUG_LINE_OK;
        break;
    case DEBUG_COMMAND_BAD_ARGUMENT:
        run->status[line - 1] = DEBUG_LINE_BAD_ARGUMENT;
        break;
    default:
        run->status[line - 1] = DEBUG_LINE_UNKNOWN;
        break;
    }
}

static void compile(DebugScriptRun *run, DebugScriptHost host)
{
    size_t offset = 0;
    bool build_seen = false;
    for (uint32_t line = 1; offset < run->length && run->state == DEBUG_SCRIPT_PENDING; line++) {
        DebugCommand command =
            debug_script_parse(debug_script_next_line(run->text, run->length, &offset));
        if (command.kind == DEBUG_COMMAND_BLANK) {
            continue;
        }
        if (!build_seen && command.kind != DEBUG_COMMAND_BUILD) {
            run->state = DEBUG_SCRIPT_REFUSED_NO_BUILD;
        } else if (!build_seen && !build_matches(command.argument, host.version)) {
            run->state = DEBUG_SCRIPT_REFUSED_BUILD;
        }
        build_seen = true;
        add_command(run, &command, line);
    }
    if (!build_seen) {
        run->state = DEBUG_SCRIPT_REFUSED_NO_BUILD;
    }
}

static void clear_results(DebugScriptRun *run)
{
    run->op_count = 0;
    run->word_count = 0;
    run->probe_count = 0;
    run->soak_count = 0;
    run->rtc_count = 0;
    run->soak_ms = 0;
    run->measure_count = 0;
    run->screenshot_count = 0;
    run->started_ms = 0;
    run->finished_ms = 0;
    for (int line = 0; line < DEBUG_SCRIPT_LINES; line++) {
        run->status[line] = DEBUG_LINE_NOT_RUN;
    }
}

void debug_script_stage(DebugScriptRun *run, uint32_t length, DebugScriptHost host)
{
    clear_results(run);
    run->length = (uint16_t)(length < DEBUG_SCRIPT_BYTES ? length : DEBUG_SCRIPT_BYTES);
    run->state = DEBUG_SCRIPT_PENDING;
    if (length > DEBUG_SCRIPT_BYTES ||
        debug_script_line_count(run->text, run->length) > DEBUG_SCRIPT_LINES) {
        run->state = DEBUG_SCRIPT_REFUSED_TOO_LONG;
    } else if (host.restart_pending) {
        run->state = DEBUG_SCRIPT_REFUSED_RESTART_PENDING;
    }
}

void debug_script_compile(DebugScriptRun *run, DebugScriptHost host)
{
    if (run->state == DEBUG_SCRIPT_PENDING) {
        clear_results(run);
        compile(run, host);
    }
    if (run->state != DEBUG_SCRIPT_PENDING) {
        clear_results(run);
    }
}

void debug_script_load(DebugScriptRun *run, uint32_t length, DebugScriptHost host)
{
    debug_script_stage(run, length, host);
    debug_script_compile(run, host);
}
