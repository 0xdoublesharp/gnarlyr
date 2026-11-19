#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/key_tracker.h"
#include "core/string_builder.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    KEY_TEST_HISTORY = 6,
    KEY_TEST_SCALE = 2
};

typedef struct KeyTestEntry {
    uint32_t time_ms;
    uint16_t reading;
} KeyTestEntry;

typedef struct KeyTestScreen {
    Screen screen;
    const Theme *theme;
    KeyEvent last;
    bool has_last;
    uint16_t reading;
    uint8_t count;
    KeyTestEntry history[KEY_TEST_HISTORY];
} KeyTestScreen;

void key_test_screen_init(KeyTestScreen *test, const Theme *theme);
Key key_test_band(uint16_t reading);
void key_test_event_text(KeyEvent event, StringBuilder *builder);
