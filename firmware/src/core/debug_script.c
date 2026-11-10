#include "core/debug_script.h"

#include "core/key_script.h"

enum {
    DECIMAL_BASE = 10,
    HEX_BASE = 16,
    HEX_LETTER_VALUE = 10,
    WORD_HEX_DIGITS = 8,
    CASE_BIT = 0x20
};

typedef struct Word {
    const char *text;
    DebugCommandKind kind;
} Word;

static const Word WORDS[] = {
    {"build", DEBUG_COMMAND_BUILD},
    {"wait", DEBUG_COMMAND_WAIT},
    {"measure", DEBUG_COMMAND_MEASURE},
    {"profile", DEBUG_COMMAND_PROFILE},
    {"set", DEBUG_COMMAND_SET},
    {"screenshot", DEBUG_COMMAND_SCREENSHOT},
    {"reconnect", DEBUG_COMMAND_RECONNECT},
    {"usb_sense", DEBUG_COMMAND_USB_SENSE},
    {"peek", DEBUG_COMMAND_PEEK},
    {"poke", DEBUG_COMMAND_POKE},
    {"gpio", DEBUG_COMMAND_GPIO},
};

static const char *const PROFILE_ACTIONS[] = {
    [DEBUG_PROFILE_OFF] = "off", [DEBUG_PROFILE_ON] = "on", [DEBUG_PROFILE_RESET] = "reset"};
static const char *const SENSE_MODES[] = {"normal", "kept_on"};
static const char *const PIN_ACTIONS[] = {[DEBUG_PIN_READ] = "read",
                                          [DEBUG_PIN_LOW] = "low",
                                          [DEBUG_PIN_HIGH] = "high",
                                          [DEBUG_PIN_INPUT] = "input",
                                          [DEBUG_PIN_FUNCTION] = "function"};

static bool is_blank(char character)
{
    return character == ' ' || character == '\t' || character == '\r';
}

static DebugSpan trimmed(DebugSpan span)
{
    while (span.length > 0 && is_blank(span.text[0])) {
        span.text++;
        span.length--;
    }
    while (span.length > 0 && is_blank(span.text[span.length - 1])) {
        span.length--;
    }
    return span;
}

static char lowered(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character | CASE_BIT) : character;
}

bool debug_span_matches(DebugSpan span, const char *text)
{
    size_t index = 0;
    while (index < span.length && text[index] != '\0' &&
           lowered(span.text[index]) == lowered(text[index])) {
        index++;
    }
    return index == span.length && text[index] == '\0';
}

DebugSpan debug_script_next_line(const char *text, size_t length, size_t *offset)
{
    size_t start = *offset;
    size_t end = start;
    while (end < length && text[end] != '\n') {
        end++;
    }
    *offset = end < length ? end + 1 : end;
    return trimmed((DebugSpan){text + start, end - start});
}

uint32_t debug_script_line_count(const char *text, size_t length)
{
    uint32_t lines = 0;
    size_t offset = 0;
    while (offset < length) {
        debug_script_next_line(text, length, &offset);
        lines++;
    }
    return lines;
}

static DebugSpan first_word(DebugSpan line)
{
    size_t length = 0;
    while (length < line.length && !is_blank(line.text[length])) {
        length++;
    }
    return (DebugSpan){line.text, length};
}

static DebugSpan after(DebugSpan line, size_t consumed)
{
    return trimmed((DebugSpan){line.text + consumed, line.length - consumed});
}

static bool parse_number(DebugSpan span, uint32_t *number)
{
    uint32_t value = 0;
    for (size_t index = 0; index < span.length; index++) {
        char digit = span.text[index];
        if (digit < '0' || digit > '9' || value > DEBUG_SCRIPT_MAX_MS) {
            return false;
        }
        value = value * DECIMAL_BASE + (uint32_t)(digit - '0');
    }
    *number = value;
    return span.length > 0 && value >= 1 && value <= DEBUG_SCRIPT_MAX_MS;
}

static bool parse_choice(DebugSpan span, const char *const *choices, uint32_t count,
                         uint32_t *number)
{
    for (uint32_t choice = 0; choice < count; choice++) {
        if (debug_span_matches(span, choices[choice])) {
            *number = choice;
            return true;
        }
    }
    return false;
}

static int hex_value(char character)
{
    char letter = lowered(character);
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    return letter >= 'a' && letter <= 'f' ? letter - 'a' + HEX_LETTER_VALUE : -1;
}

static bool parse_hex(DebugSpan digits, uint32_t *word)
{
    uint32_t value = 0;
    for (size_t index = 0; index < digits.length; index++) {
        int digit = hex_value(digits.text[index]);
        if (digit < 0) {
            return false;
        }
        value = value * HEX_BASE + (uint32_t)digit;
    }
    *word = value;
    return digits.length >= 1 && digits.length <= WORD_HEX_DIGITS;
}

static bool parse_word(DebugSpan span, uint32_t *word)
{
    if (span.length > 2 && span.text[0] == '0' && lowered(span.text[1]) == 'x') {
        return parse_hex((DebugSpan){span.text + 2, span.length - 2}, word);
    }
    uint32_t value = 0;
    for (size_t index = 0; index < span.length; index++) {
        char digit = span.text[index];
        if (digit < '0' || digit > '9' ||
            value > (UINT32_MAX - (uint32_t)(digit - '0')) / DECIMAL_BASE) {
            return false;
        }
        value = value * DECIMAL_BASE + (uint32_t)(digit - '0');
    }
    *word = value;
    return span.length > 0;
}

static bool parse_words(DebugCommand *command, uint32_t count)
{
    DebugSpan first = first_word(command->argument);
    DebugSpan rest = after(command->argument, first.length);
    if (!parse_word(first, &command->number)) {
        return false;
    }
    return count == 1 ? rest.length == 0 : parse_word(rest, &command->second);
}

static bool parse_pin(DebugSpan span, uint32_t *pin)
{
    uint32_t number = 0;
    bool port_given =
        span.length > 2 && span.text[0] >= '0' && span.text[0] <= '9' && span.text[1] == '.';
    if (!port_given || !parse_word((DebugSpan){span.text + 2, span.length - 2}, &number) ||
        number >= DEBUG_PIN_COUNT) {
        return false;
    }
    *pin = (uint32_t)(span.text[0] - '0') << DEBUG_PIN_PORT_SHIFT | number;
    return true;
}

static bool parse_pin_action(DebugSpan span, uint32_t *action)
{
    DebugSpan word = first_word(span);
    DebugSpan rest = after(span, word.length);
    uint32_t function = 0;
    if (!parse_choice(word, PIN_ACTIONS, sizeof PIN_ACTIONS / sizeof PIN_ACTIONS[0], action)) {
        return false;
    }
    if (*action != DEBUG_PIN_FUNCTION) {
        return rest.length == 0;
    }
    if (!parse_word(rest, &function) || function >= DEBUG_PIN_FUNCTIONS) {
        return false;
    }
    *action |= function << DEBUG_PIN_FUNCTION_SHIFT;
    return true;
}

static bool parse_gpio(DebugCommand *command)
{
    DebugSpan pin = first_word(command->argument);
    return parse_pin(pin, &command->number) &&
           parse_pin_action(after(command->argument, pin.length), &command->second);
}

static bool is_name(DebugSpan span)
{
    for (size_t index = 0; index < span.length; index++) {
        char character = span.text[index];
        bool allowed = (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') || character == '_' ||
                       character == '-';
        if (!allowed) {
            return false;
        }
    }
    return span.length >= 1 && span.length <= DEBUG_SCRIPT_NAME_BYTES;
}

static bool is_tag(DebugSpan span)
{
    return span.length > 0 && first_word(span).length == span.length;
}

static bool split_setting(DebugCommand *command)
{
    DebugSpan whole = command->argument;
    size_t equals = 0;
    while (equals < whole.length && whole.text[equals] != '=') {
        equals++;
    }
    if (equals == whole.length) {
        return false;
    }
    command->argument = trimmed((DebugSpan){whole.text, equals});
    command->value = after(whole, equals + 1);
    return command->argument.length > 0 && command->value.length > 0;
}

static bool arguments_fit(DebugCommand *command)
{
    switch (command->kind) {
    case DEBUG_COMMAND_BUILD:
        return is_tag(command->argument);
    case DEBUG_COMMAND_WAIT:
    case DEBUG_COMMAND_MEASURE:
        return parse_number(command->argument, &command->number);
    case DEBUG_COMMAND_PROFILE:
        return parse_choice(command->argument, PROFILE_ACTIONS,
                            sizeof PROFILE_ACTIONS / sizeof PROFILE_ACTIONS[0], &command->number);
    case DEBUG_COMMAND_USB_SENSE:
        return parse_choice(command->argument, SENSE_MODES,
                            sizeof SENSE_MODES / sizeof SENSE_MODES[0], &command->number);
    case DEBUG_COMMAND_SET:
        return split_setting(command);
    case DEBUG_COMMAND_SCREENSHOT:
        return is_name(command->argument);
    case DEBUG_COMMAND_PEEK:
        return parse_words(command, 1);
    case DEBUG_COMMAND_POKE:
        return parse_words(command, 2);
    case DEBUG_COMMAND_GPIO:
        return parse_gpio(command);
    default:
        return command->argument.length == 0;
    }
}

static bool is_key_list(DebugSpan line)
{
    size_t start = 0;
    for (;;) {
        KeyScriptStep step;
        size_t length = key_script_step_length(line.text + start, line.length - start);
        if (!key_script_parse_step(line.text + start, length, &step)) {
            return false;
        }
        start += length;
        if (start == line.length) {
            return true;
        }
        start++;
    }
}

static DebugCommand worded(DebugSpan line, DebugSpan word, DebugCommandKind kind)
{
    DebugCommand command = {.kind = kind, .argument = after(line, word.length)};
    if (!arguments_fit(&command)) {
        command.kind = DEBUG_COMMAND_BAD_ARGUMENT;
    }
    return command;
}

DebugCommand debug_script_parse(DebugSpan line)
{
    if (line.length == 0 || line.text[0] == '#') {
        return (DebugCommand){.kind = DEBUG_COMMAND_BLANK};
    }
    DebugSpan word = first_word(line);
    for (size_t index = 0; index < sizeof WORDS / sizeof WORDS[0]; index++) {
        if (debug_span_matches(word, WORDS[index].text)) {
            return worded(line, word, WORDS[index].kind);
        }
    }
    bool keys = is_key_list(line);
    return (DebugCommand){.kind = keys ? DEBUG_COMMAND_KEYS : DEBUG_COMMAND_UNKNOWN,
                          .argument = line};
}

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
    case DEBUG_COMMAND_GPIO:
        op = new_op(run, DEBUG_OP_PIN, line);
        op->first = (uint16_t)command->number;
        op->second = (uint16_t)command->second;
        return true;
    default:
        return false;
    }
}

static void add_command(DebugScriptRun *run, const DebugCommand *command, uint32_t line)
{
    if (add_probe(run, command, line)) {
        return;
    }
    switch (command->kind) {
    case DEBUG_COMMAND_KEYS:
        add_keys(run, command->argument, line);
        break;
    case DEBUG_COMMAND_WAIT:
        add_number(run, DEBUG_OP_WAIT, command->number, line);
        break;
    case DEBUG_COMMAND_MEASURE:
        add_number(run, DEBUG_OP_MEASURE, command->number, line);
        break;
    case DEBUG_COMMAND_PROFILE:
        add_number(run, DEBUG_OP_PROFILE, command->number, line);
        break;
    case DEBUG_COMMAND_USB_SENSE:
        add_number(run, DEBUG_OP_USB_SENSE, command->number, line);
        break;
    case DEBUG_COMMAND_SET:
        add_spans(run, DEBUG_OP_SET, command, line);
        break;
    case DEBUG_COMMAND_SCREENSHOT:
        add_spans(run, DEBUG_OP_SCREENSHOT, command, line);
        break;
    case DEBUG_COMMAND_RECONNECT:
        add_number(run, DEBUG_OP_RECONNECT, 0, line);
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
    run->measure_count = 0;
    run->screenshot_count = 0;
    run->started_ms = 0;
    run->finished_ms = 0;
    for (int line = 0; line < DEBUG_SCRIPT_LINES; line++) {
        run->status[line] = DEBUG_LINE_NOT_RUN;
    }
}

void debug_script_load(DebugScriptRun *run, uint32_t length, DebugScriptHost host)
{
    clear_results(run);
    run->length = (uint16_t)(length < DEBUG_SCRIPT_BYTES ? length : DEBUG_SCRIPT_BYTES);
    run->state = DEBUG_SCRIPT_PENDING;
    if (length > DEBUG_SCRIPT_BYTES ||
        debug_script_line_count(run->text, run->length) > DEBUG_SCRIPT_LINES) {
        run->state = DEBUG_SCRIPT_REFUSED_TOO_LONG;
    } else if (host.restart_pending) {
        run->state = DEBUG_SCRIPT_REFUSED_RESTART_PENDING;
    } else {
        compile(run, host);
    }
    if (run->state != DEBUG_SCRIPT_PENDING) {
        clear_results(run);
    }
}
