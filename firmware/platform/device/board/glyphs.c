#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "emmc.h"
#include "gfx/font_pack.h"
#include "hal/system.h"

#define GLYPH_TABLE_OFFSET 0x2C0000u
#define GLYPH_TABLE_MAGIC 0x54504745u
#define NO_SECTOR 0xFFFFFFFFu

enum {
    SECTOR_BYTES = 512,
    SECTOR_WORDS = SECTOR_BYTES / 4,
    TABLE_HEADER_BYTES = 8,
    TABLE_ROW_BYTES = 8,
    GLYPH_RAM_BYTES = 0x9000,
    CACHE_WORDS = (GLYPH_RAM_BYTES - 2 * SECTOR_BYTES) / 4
};

static uint32_t table[SECTOR_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t sector[SECTOR_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t cache[CACHE_WORDS] __attribute__((section(".glyph_ram")));
static uint32_t sector_lba;
static uint32_t slot_1;
static uint32_t face_count;

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
        const uint8_t *bytes = (const uint8_t *)sector + within;
        for (uint32_t index = 0; index < chunk; index++) {
            out[index] = bytes[index];
        }
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
    if (hal_system_restart_pending() || face >= face_count) {
        return false;
    }
    uint32_t start = table[2 + 2 * face];
    uint32_t size = table[3 + 2 * face];
    if (offset > size || length > size - offset) {
        return false;
    }
    return board_slot_read(slot_1, start + offset, buffer, length);
}

void board_glyphs_start(void)
{
    EmmcInfo info;
    font_pack_detach();
    face_count = 0;
    sector_lba = NO_SECTOR;
    if (!emmc_start(&info) ||
        !emmc_read(NULL, info.layout.slot_1 + GLYPH_TABLE_OFFSET / SECTOR_BYTES, 1, table) ||
        table[0] != GLYPH_TABLE_MAGIC) {
        return;
    }
    slot_1 = info.layout.slot_1;
    face_count = table[1] < FONT_PACK_FACES ? table[1] : FONT_PACK_FACES;
    font_pack_attach((FontPackReader){read_face, NULL}, cache, sizeof cache);
}
