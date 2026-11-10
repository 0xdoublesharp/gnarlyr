#include "core/key_script.h"

enum {
    SUFFIX_LENGTH = 5
};

static const char *const KEY_NAMES[KEY_COUNT] = {
    [KEY_VOLUME_UP] = "down", [KEY_VOLUME_DOWN] = "up", [KEY_PLAY] = "play",
    [KEY_MENU] = "menu",      [KEY_BACK] = "back",      [KEY_POWER] = "power",
};

static const char LONG_SUFFIX[SUFFIX_LENGTH + 1] = ":long";
static const char HELD_SUFFIX[SUFFIX_LENGTH + 1] = ":held";

static const uint32_t HOLD_MS[] = {
    [KEY_SCRIPT_TAP] = KEY_SCRIPT_TAP_MS,
    [KEY_SCRIPT_LONG] = KEY_SCRIPT_LONG_MS,
    [KEY_SCRIPT_HELD] = KEY_SCRIPT_HELD_MS,
};

const char *key_script_key_name(Key key)
{
    return (unsigned)key < KEY_COUNT ? KEY_NAMES[key] : "?";
}

uint32_t key_script_hold_ms(KeyScriptHold hold)
{
    return HOLD_MS[hold];
}

static bool matches(const char *text, size_t length, const char *word)
{
    size_t index = 0;
    while (index < length && word[index] != '\0' && text[index] == word[index]) {
        index++;
    }
    return index == length && word[index] == '\0';
}

static bool has_suffix(const char *text, size_t length, const char *suffix)
{
    return length > SUFFIX_LENGTH && matches(text + length - SUFFIX_LENGTH, SUFFIX_LENGTH, suffix);
}

static KeyScriptHold hold_of(const char *text, size_t length)
{
    if (has_suffix(text, length, LONG_SUFFIX)) {
        return KEY_SCRIPT_LONG;
    }
    return has_suffix(text, length, HELD_SUFFIX) ? KEY_SCRIPT_HELD : KEY_SCRIPT_TAP;
}

size_t key_script_step_length(const char *text, size_t length)
{
    size_t index = 0;
    while (index < length && text[index] != ',') {
        index++;
    }
    return index;
}

bool key_script_parse_step(const char *text, size_t length, KeyScriptStep *step)
{
    KeyScriptHold hold = hold_of(text, length);
    size_t name_length = hold == KEY_SCRIPT_TAP ? length : length - SUFFIX_LENGTH;
    for (int key = 0; key < KEY_COUNT; key++) {
        if (matches(text, name_length, KEY_NAMES[key])) {
            *step = (KeyScriptStep){.key = (Key)key, .hold = hold};
            return true;
        }
    }
    return false;
}

int key_script_parse(const char *text, size_t length, KeyScriptStep *steps, int capacity)
{
    int count = 0;
    size_t start = 0;
    for (;;) {
        size_t step_length = key_script_step_length(text + start, length - start);
        if (count == capacity || !key_script_parse_step(text + start, step_length, &steps[count])) {
            return 0;
        }
        count++;
        start += step_length;
        if (start == length) {
            return count;
        }
        start++;
    }
}
