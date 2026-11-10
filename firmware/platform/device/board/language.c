#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "core/i18n.h"
#include "emmc.h"
#include "hal/language.h"
#include "hal/system.h"

#define LANGUAGE_TABLE_OFFSET 0x2C0100u
#define LANGUAGE_TABLE_MAGIC 0x544C5344u

enum {
    SECTOR_BYTES = 512,
    TABLE_HEADER_BYTES = 8,
    TABLE_ENTRY_BYTES = 8,
    TABLE_ENTRIES = 31
};

static uint32_t memory[I18N_PACK_CAPACITY / 4] __attribute__((section(".language_ram")));

uint8_t *hal_language_memory(uint32_t *capacity)
{
    *capacity = sizeof memory;
    return (uint8_t *)memory;
}

static bool entry_of(uint32_t slot, uint32_t index, uint32_t *entry)
{
    uint32_t header[2];
    return board_slot_read(slot, LANGUAGE_TABLE_OFFSET, header, sizeof header) &&
           header[0] == LANGUAGE_TABLE_MAGIC && index < header[1] && index < TABLE_ENTRIES &&
           board_slot_read(slot,
                           LANGUAGE_TABLE_OFFSET + TABLE_HEADER_BYTES + TABLE_ENTRY_BYTES * index,
                           entry, TABLE_ENTRY_BYTES);
}

bool hal_language_read(HalLanguagePack pack, uint32_t offset, void *buffer, uint32_t length)
{
    EmmcInfo info;
    uint32_t entry[2];
    if (hal_system_restart_pending() || pack.copy >= HAL_LANGUAGE_COPIES || !emmc_start(&info)) {
        return false;
    }
    uint32_t slot = pack.copy == 0 ? info.layout.slot_1 : info.layout.slot_2;
    uint32_t slot_bytes = info.layout.slot_sectors * SECTOR_BYTES;
    if (!entry_of(slot, pack.index, entry) || entry[0] > slot_bytes ||
        entry[1] > slot_bytes - entry[0] || offset > entry[1] || length > entry[1] - offset) {
        return false;
    }
    return board_slot_read(slot, entry[0] + offset, buffer, length);
}
