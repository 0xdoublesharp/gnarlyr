#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui/theme.h"

enum {
    THEME_PALETTE_BYTES = 52
};

typedef struct ThemePalette {
    ThemeColors colors;
    bool horizon;
    bool header_rule;
} ThemePalette;

bool theme_palette_decode(const uint8_t *bytes, uint32_t length, ThemePalette *palette);
void theme_palette_encode(const ThemePalette *palette, uint8_t *bytes);
