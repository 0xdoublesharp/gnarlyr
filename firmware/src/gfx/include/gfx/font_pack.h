#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/font.h"

enum {
    FONT_PACK_IMAGE_FACES = 21,
    FONT_PACK_EXTRA_FACES = 7,
    FONT_PACK_FACES = FONT_PACK_IMAGE_FACES + FONT_PACK_EXTRA_FACES,
    FONT_PACK_EXTRA_LANGUAGES = 32,
    FONT_PACK_LANGUAGE_BYTES = 8,
    FONT_PACK_EXTRA_TABLE = 12,
    FONT_PACK_EXTRA_CODES = FONT_PACK_EXTRA_TABLE + 8 * FONT_PACK_EXTRA_FACES,
    FONT_PACK_MAX_BITMAP = 352,
    FONT_PACK_HEADER_BYTES = 48,
    FONT_PACK_HEADER_CHECK = 40,
    FONT_PACK_ROOT_OFFSET = 24,
    FONT_PACK_ENTRY_BYTES = 12,
    FONT_PACK_ENTRIES_PER_SECTOR = 42,
    FONT_PACK_SECTOR_BYTES = 512,
    FONT_PACK_SECTOR_CHECK = 508,
    FONT_PACK_DIRECTORY_ENTRIES = 254,
    FONT_PACK_MAX_ROOT = 8,
    FONT_PACK_MAX_GLYPHS = 0x10000,
    FONT_PACK_WAYS = 4,
    FONT_PACK_VERSION = 1
};

typedef enum FontPackHold {
    FONT_PACK_HOLD_NONE,
    FONT_PACK_HOLD_PIN,
    FONT_PACK_HOLD_FREEZE
} FontPackHold;

typedef struct FontPackReader {
    bool (*read)(void *context, uint32_t face, uint32_t offset, void *buffer, uint32_t length);
    void *context;
} FontPackReader;

uint32_t font_pack_attach(FontPackReader reader, void *memory, size_t bytes);
void font_pack_detach(void);
void font_pack_forget(void);
bool font_pack_attach_extra(FontPackReader reader, uint32_t size);
void font_pack_detach_extra(void);
bool font_pack_extra_attached(void);
const char *font_pack_extra_language(uint32_t index);
void font_pack_hold(FontPackHold hold);
uint32_t font_pack_held_bytes(void);
const Glyph *font_pack_find(uint32_t face, uint32_t codepoint);
const uint8_t *font_pack_bitmap(const Glyph *glyph);
