#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "emmc.h"
#include "gfx/font_pack.h"
#include "hal/system.h"
#include "overlay.h"

#define GLYPH_TABLE_OFFSET 0x2C0000u
#define GLYPH_TABLE_MAGIC 0x54504745u
#define NO_SECTOR 0xFFFFFFFFu

typedef struct GlyphFace {
    uint32_t lba;
    uint32_t size;
} GlyphFace;

enum {
    SECTOR_BYTES = 512,
    SECTOR_WORDS = SECTOR_BYTES / 4,
    TABLE_HEADER_BYTES = 8,
    TABLE_ROW_BYTES = 8,
    GLYPH_TABLE_ROWS = (SECTOR_BYTES / 2 - TABLE_HEADER_BYTES) / TABLE_ROW_BYTES,
    CACHE_WORDS = (GLYPH_WINDOW_BYTES - SECTOR_BYTES - FONT_PACK_IMAGE_FACES * sizeof(GlyphFace)) /
        4
};

static GlyphFace faces[FONT_PACK_IMAGE_FACES] __attribute__((section(".glyph_ram")));
static uint32_t sector[SECTOR_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t cache[CACHE_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t sector_lba;
static EmmcInfo glyph_image;

void board_slot_cache_invalidate(void);

void board_slot_cache_invalidate(void)
{
    sector_lba = NO_SECTOR;
}

static bool read_sector(uint32_t lba)
{
    if (lba == sector_lba) {
        return true;
    }
    sector_lba = NO_SECTOR;
    if (!emmc_read(NULL, lba, 1, sector)) {
        return false;
    }
    sector_lba = lba;
    return true;
}

bool board_slot_read(uint32_t slot, uint32_t at, void *buffer, uint32_t length)
{
    uint8_t *out = buffer;
    while (length > 0) {
        uint32_t within = at % SECTOR_BYTES;
        uint32_t chunk = SECTOR_BYTES - within < length ? SECTOR_BYTES - within : length;
        if (!read_sector(slot + at / SECTOR_BYTES)) {
            return false;
        }
        memcpy(out, (const uint8_t *)sector + within, chunk);
        out += chunk;
        at += chunk;
        length -= chunk;
    }
    return true;
}

bool board_slot_table_row(uint32_t slot, const BoardImageTable *listing, uint32_t index,
                          uint32_t *row)
{
    uint32_t header[2];
    return board_slot_read(slot, listing->offset, header, sizeof header) &&
           header[0] == listing->magic && index < header[1] && index < listing->rows &&
           board_slot_read(slot, listing->offset + TABLE_HEADER_BYTES + TABLE_ROW_BYTES * index,
                           row, TABLE_ROW_BYTES);
}

static bool glyph_table_read(uint32_t slot)
{
    return read_sector(slot + GLYPH_TABLE_OFFSET / SECTOR_BYTES) &&
           sector[0] == GLYPH_TABLE_MAGIC && sector[1] - 1 < GLYPH_TABLE_ROWS;
}

static bool open_face(uint32_t slot, uint32_t face, void *buffer)
{
    if (!glyph_table_read(slot) || face >= sector[1]) {
        return false;
    }
    uint32_t start = sector[2 + 2 * face];
    uint32_t size = sector[3 + 2 * face];
    uint32_t slot_bytes = glyph_image.layout.slot_sectors * SECTOR_BYTES;
    if (start % SECTOR_BYTES != 0 || start >= slot_bytes || size > slot_bytes - start) {
        return false;
    }
    uint32_t lba = slot + start / SECTOR_BYTES;
    if (!read_sector(lba) || sector[3] > size || !font_pack_header_valid((const uint8_t *)sector)) {
        return false;
    }
    faces[face] = (GlyphFace){lba, sector[3]};
    memcpy(buffer, sector, FONT_PACK_HEADER_BYTES);
    return true;
}

static bool read_face(void *context, uint32_t face, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (hal_system_restart_pending()) {
        return false;
    }
    if (offset == 0) {
        return length == FONT_PACK_HEADER_BYTES &&
               (open_face(glyph_image.layout.slot_1, face, buffer) ||
                open_face(glyph_image.layout.slot_2, face, buffer));
    }
    GlyphFace source = faces[face];
    return offset <= source.size && length <= source.size - offset &&
           board_slot_read(source.lba, offset, buffer, length);
}

__attribute__((noinline)) void board_glyphs_start(void)
{
    board_slot_cache_invalidate();
    if (emmc_start(&glyph_image)) {
        font_pack_attach((FontPackReader){read_face, NULL}, cache, sizeof cache);
    } else {
        font_pack_detach();
    }
}
