#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/font.h"

enum {
#ifdef ECHO_BOARD_ECHO
    FONT_PACK_FACES = 5,
    FONT_PACK_MAX_BITMAP = 256,
#else
    FONT_PACK_FACES = 3,
    FONT_PACK_MAX_BITMAP = 96,
#endif
    FONT_PACK_HEADER_BYTES = 32,
    FONT_PACK_ENTRY_BYTES = 12,
    FONT_PACK_ENTRIES_PER_SECTOR = 42,
    FONT_PACK_SECTOR_BYTES = 512,
    FONT_PACK_MAX_DIRECTORY = 8,
    FONT_PACK_WAYS = 4,
    FONT_PACK_VERSION = 1
};

typedef struct FontPackReader {
    bool (*read)(void *context, uint32_t face, uint32_t offset, void *buffer, uint32_t length);
    void *context;
} FontPackReader;

uint32_t font_pack_attach(FontPackReader reader, void *memory, size_t bytes);
void font_pack_detach(void);
void font_pack_forget(void);
const Glyph *font_pack_find(uint32_t face, uint32_t codepoint);
const uint8_t *font_pack_bitmap(const Glyph *glyph);
