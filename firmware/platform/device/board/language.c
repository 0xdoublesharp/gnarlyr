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
    TABLE_ENTRIES = 31
};

static const BoardImageTable LANGUAGE_TABLE = {LANGUAGE_TABLE_OFFSET, LANGUAGE_TABLE_MAGIC,
                                               TABLE_ENTRIES};

static uint32_t memory[I18N_PACK_CAPACITY / 4] __attribute__((section(".language_ram")));

uint8_t *hal_language_memory(uint32_t *capacity)
{
    *capacity = sizeof memory;
    return (uint8_t *)memory;
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
    if (!board_slot_table_row(slot, &LANGUAGE_TABLE, pack.index, entry) || entry[0] > slot_bytes ||
        entry[1] > slot_bytes - entry[0] || offset > entry[1] || length > entry[1] - offset) {
        return false;
    }
    return board_slot_read(slot, entry[0] + offset, buffer, length);
}
