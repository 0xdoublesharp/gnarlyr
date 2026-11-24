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

enum {
    SECTOR_BYTES = 512,
    SECTOR_WORDS = SECTOR_BYTES / 4,
    TABLE_HEADER_BYTES = 8,
    TABLE_ROW_BYTES = 8,
    GLYPH_TABLE_ROWS = (SECTOR_BYTES / 2 - TABLE_HEADER_BYTES) / TABLE_ROW_BYTES,
    CACHE_WORDS = (GLYPH_WINDOW_BYTES - 2 * SECTOR_BYTES) / 4
};

static uint32_t table[SECTOR_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t sector[SECTOR_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t cache[CACHE_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t sector_lba;
static uint32_t glyph_slot;

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

static bool read_face(void *context, uint32_t face, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (hal_system_restart_pending() || face >= table[1]) {
        return false;
    }
    uint32_t start = table[2 + 2 * face];
    uint32_t size = table[3 + 2 * face];
    if (offset > size || length > size - offset) {
        return false;
    }
    return board_slot_read(glyph_slot, start + offset, buffer, length);
}

static bool load_table(uint32_t slot)
{
    if (!emmc_read(NULL, slot + GLYPH_TABLE_OFFSET / SECTOR_BYTES, 1, table) ||
        table[0] != GLYPH_TABLE_MAGIC || table[1] - 1 >= GLYPH_TABLE_ROWS) {
        return false;
    }
    glyph_slot = slot;
    return true;
}

__attribute__((noinline)) void board_glyphs_start(void)
{
    EmmcInfo info;
    board_slot_cache_invalidate();
    if (emmc_start(&info) && (load_table(info.layout.slot_1) || load_table(info.layout.slot_2))) {
        font_pack_attach((FontPackReader){read_face, NULL}, cache, sizeof cache);
    } else {
        font_pack_detach();
    }
}
