#pragma once

#include "core/key_tracker.h"
#include "gfx/font.h"

enum {
    DISCOVERY_TEXT_BYTES = 64
};

typedef struct DiscoveryText {
    char text[DISCOVERY_TEXT_BYTES];
    uint8_t character;
    bool numeric;
} DiscoveryText;

void discovery_text_begin(DiscoveryText *editor, const char *text, bool numeric);
void discovery_text_key(DiscoveryText *editor, KeyEvent event);
char discovery_text_character(const DiscoveryText *editor);
const char *discovery_text_tail(const char *text, const Font *font, int width);
bool discovery_text_number(const DiscoveryText *editor, uint32_t limit, uint32_t *value);
