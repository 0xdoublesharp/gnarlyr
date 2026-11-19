#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/color.h"

typedef Color (*ThemeFixturePixel)(int x, int y, int index);

typedef struct ThemeFixtureArt {
    uint16_t slot;
    uint8_t orientation;
    uint8_t text_size;
    uint16_t width;
    uint16_t height;
    uint8_t codec;
    uint8_t flags;
} ThemeFixtureArt;

typedef struct ThemeFixture {
    uint8_t id;
    uint8_t base;
    uint8_t count;
    const ThemeFixtureArt *art;
    ThemeFixturePixel pixel;
    const uint8_t *style;
    uint16_t style_bytes;
} ThemeFixture;

uint32_t theme_fixture_build(uint8_t *bytes, uint32_t capacity, const ThemeFixture *fixture);
void theme_fixture_seal(uint8_t *bytes, uint32_t length);
