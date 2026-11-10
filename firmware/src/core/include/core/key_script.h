#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/key.h"

enum {
    KEY_SCRIPT_TAP_MS = 32,
    KEY_SCRIPT_LONG_MS = 1120,
    KEY_SCRIPT_HELD_MS = 4800,
    KEY_SCRIPT_GAP_MS = 128
};

typedef enum KeyScriptHold {
    KEY_SCRIPT_TAP,
    KEY_SCRIPT_LONG,
    KEY_SCRIPT_HELD
} KeyScriptHold;

typedef struct KeyScriptStep {
    Key key;
    KeyScriptHold hold;
} KeyScriptStep;

const char *key_script_key_name(Key key);
uint32_t key_script_hold_ms(KeyScriptHold hold);
size_t key_script_step_length(const char *text, size_t length);
bool key_script_parse_step(const char *text, size_t length, KeyScriptStep *step);
int key_script_parse(const char *text, size_t length, KeyScriptStep *steps, int capacity);
