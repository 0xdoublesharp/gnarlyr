#pragma once

#include <stdint.h>

#include "core/i18n.h"
#include "gfx/canvas.h"
#include "ui/theme.h"

typedef enum FooterKey {
    FOOTER_KEY_VOL,
    FOOTER_KEY_PLAY,
    FOOTER_KEY_MENU,
    FOOTER_KEY_BACK,
    FOOTER_KEY_HOLD_PLAY,
    FOOTER_KEY_HOLD_MENU,
    FOOTER_KEY_HOLD_BACK
} FooterKey;

typedef enum FooterAlign {
    FOOTER_ALIGN_START,
    FOOTER_ALIGN_END
} FooterAlign;

enum {
    FOOTER_CHIP_PADDING = 4,
    FOOTER_CHIP_GAP = 6,
    FOOTER_HINT_GAP = 4,
    FOOTER_LINE_SPACE = 4,
    FOOTER_VERB_ALPHA = 10
};

typedef struct FooterHint {
    uint8_t key;
    uint16_t verb;
} FooterHint;

typedef struct FooterHints {
    const FooterHint *items;
    uint8_t count;
    uint8_t align;
    void (*service)(void);
} FooterHints;

const char *footer_hints_key_name(FooterKey key);
int footer_hints_lines(const Theme *theme, FooterHints hints, int width);
int footer_hints_height(const Theme *theme, FooterHints hints);
Rect footer_hints_area(const Theme *theme, Rect content, FooterHints hints);
void footer_hints_draw(Canvas *canvas, const Theme *theme, Rect area, FooterHints hints);
void footer_hints_draw_inline(Canvas *canvas, const Theme *theme, Rect area, FooterHints hints);
