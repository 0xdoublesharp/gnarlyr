#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "gfx/font.h"
#include "ui/theme.h"

enum {
    PLACEHOLDER_ART_MAX_INITIALS = 2
};

typedef struct ArtInitials {
    uint32_t letters[PLACEHOLDER_ART_MAX_INITIALS];
    int count;
} ArtInitials;

typedef struct ArtInk {
    Color top;
    Color bottom;
    Color text;
} ArtInk;

typedef struct ArtFonts {
    const Font *large;
    const Font *small;
} ArtFonts;

typedef struct PlaceholderArt {
    ArtInitials initials;
    ArtInk ink;
    ArtFonts fonts;
} PlaceholderArt;

ArtInitials placeholder_art_initials(const Font *font, const char *text);
ArtInk placeholder_art_ink(const ThemeColors *colors, uint32_t seed);
void placeholder_art_draw(Canvas *canvas, const PlaceholderArt *art);
