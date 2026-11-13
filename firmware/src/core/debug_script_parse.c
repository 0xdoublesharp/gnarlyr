#include "core/debug_script.h"

#include "core/key_script.h"

enum {
    DECIMAL_BASE = 10,
    HEX_BASE = 16,
    HEX_LETTER_VALUE = 10,
    WORD_HEX_DIGITS = 8,
    MILLISECONDS_PER_MINUTE = 60000,
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
    {"dac", DEBUG_COMMAND_DAC},
    {"supply", DEBUG_COMMAND_SUPPLY},
    {"screen", DEBUG_COMMAND_SCREEN},
    {"soak", DEBUG_COMMAND_SOAK},
    {"rtc", DEBUG_COMMAND_RTC},
    {"standby", DEBUG_COMMAND_STANDBY},
    {"rescan", DEBUG_COMMAND_RESCAN},
    {"open", DEBUG_COMMAND_OPEN},
};

static const char *const PROFILE_ACTIONS[] = {
    [DEBUG_PROFILE_OFF] = "off", [DEBUG_PROFILE_ON] = "on", [DEBUG_PROFILE_RESET] = "reset"};
static const char *const SENSE_MODES[] = {"normal", "kept_on"};
static const char *const SWITCH_STATES[] = {"off", "on"};
static const char *const RTC_ACTIONS[] = {[DEBUG_RTC_READ] = "read", [DEBUG_RTC_SET] = "set"};
static const char *const RESCAN_ACTIONS[] = {"check", "full"};
static const char *const OPEN_TARGETS[] = {"home", "coverflow", "nowplaying"};
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
    case DEBUG_COMMAND_DAC:
        return parse_words(command, 1) && command->number <= DEBUG_DAC_MAP_MAX;
    case DEBUG_COMMAND_SUPPLY:
    case DEBUG_COMMAND_SCREEN:
    case DEBUG_COMMAND_STANDBY:
        return parse_choice(command->argument, SWITCH_STATES,
                            sizeof SWITCH_STATES / sizeof SWITCH_STATES[0], &command->number);
    case DEBUG_COMMAND_RTC:
        return parse_choice(command->argument, RTC_ACTIONS,
                            sizeof RTC_ACTIONS / sizeof RTC_ACTIONS[0], &command->number);
    case DEBUG_COMMAND_RESCAN:
        return parse_choice(command->argument, RESCAN_ACTIONS,
                            sizeof RESCAN_ACTIONS / sizeof RESCAN_ACTIONS[0], &command->number);
    case DEBUG_COMMAND_OPEN:
        return parse_choice(command->argument, OPEN_TARGETS,
                            sizeof OPEN_TARGETS / sizeof OPEN_TARGETS[0], &command->number);
    case DEBUG_COMMAND_SOAK:
        return parse_word(command->argument, &command->number) && command->number >= 1 &&
               command->number <= DEBUG_SOAK_MAX_MINUTES;
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
