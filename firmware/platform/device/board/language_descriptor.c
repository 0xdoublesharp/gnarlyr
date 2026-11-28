#include <string.h>

#include "board.h"
#include "core/bytes.h"
#include "core/hash.h"
#include "emmc.h"
#include "hal/system.h"
#include "language.h"
#include "overlay.h"
#include "settings/user_state.h"
#include "trial_module.h"

enum {
    SECTOR_BYTES = 512,
    VERSION = 1,
    BUILD_AT = 16,
    COUNT_AT = 32,
    SIZE_AT = 36,
    CHECKSUM_AT = 40,
    TABLE_AT = 44,
    ROWS_AT = 52,
    ROW_BYTES = 8,
    TABLE_CAPACITY = ROWS_AT + LANGUAGE_CAPACITY * ROW_BYTES,
    MODULE_CODE_CRC_AT = 12
};

#define DESCRIPTOR_MAGIC 0x494C5344u

static const UserStatePlace PLACE = {USER_STATE_INTERNAL, USER_STATE_FOLDER "languages.bin"};

typedef struct LanguageTable {
    uint32_t slot;
    uint32_t rows[LANGUAGE_CAPACITY][2];
    uint8_t header[TABLE_CAPACITY];
} LanguageTable;

static void encode_header(uint8_t *header, const char *build)
{
    bytes_put_le32(header, DESCRIPTOR_MAGIC);
    bytes_put_le16(header + 4, VERSION);
    bytes_put_le16(header + 6, LANGUAGE_CAPACITY);
    bytes_put_le32(header + 8, I18N_FILE_CAPACITY);
    bytes_put_le32(header + 12, I18N_PACK_CAPACITY);
    for (uint32_t i = BUILD_AT; i < COUNT_AT && *build != '\0'; i++) {
        header[i] = (uint8_t)*build++;
    }
    bytes_put_le32(header + TABLE_AT, BOARD_LANGUAGE_TABLE_MAGIC);
}

static bool current(const UserStateIo *io, const uint8_t *header)
{
    uint8_t existing[ROWS_AT];
    uint32_t size = io->size(io->context, &PLACE);
    if (size < ROWS_AT || size > TABLE_CAPACITY + LANGUAGE_CAPACITY * I18N_FILE_CAPACITY ||
        !io->read(io->context, &PLACE, (UserStateChunk){0, existing, sizeof existing})) {
        return false;
    }
    uint32_t count = bytes_le32(existing + COUNT_AT);
    return memcmp(existing, header, COUNT_AT) == 0 && count != 0 && count <= LANGUAGE_CAPACITY &&
           bytes_le32(existing + SIZE_AT) == size && size >= ROWS_AT + count * ROW_BYTES &&
           bytes_le32(existing + TABLE_AT) == BOARD_LANGUAGE_TABLE_MAGIC &&
           bytes_le32(existing + TABLE_AT + 4) == count;
}

static bool matches_running_code(uint32_t slot)
{
    static const BoardImageTable MODULES = {MODULE_TABLE_OFFSET, MODULE_TABLE_MAGIC,
                                            MODULE_TABLE_ROWS};
    const uint8_t *running = (const uint8_t *)(uintptr_t)MODULE_SLOT_BASE;
    uint32_t row[2];
    uint32_t crc;
    return board_slot_table_row(slot, &MODULES, TRIAL_BOOT_MODULE_NUMBER - 1, row) &&
           row[0] < 0x500000u - OVERLAY_HEADER_BYTES && row[1] >= OVERLAY_HEADER_BYTES &&
           board_slot_read(slot, row[0] + MODULE_CODE_CRC_AT, &crc, sizeof crc) &&
           crc == bytes_le32(running + MODULE_CODE_CRC_AT);
}

static bool read_row(LanguageTable *table, uint32_t index, uint32_t slot_bytes)
{
    uint32_t *row = table->rows[index];
    uint32_t at = BOARD_LANGUAGE_TABLE_OFFSET + 8 + index * ROW_BYTES;
    uint8_t header[I18N_INDEX_BYTES];
    I18nPackInfo info;
    return board_slot_read(table->slot, at, row, ROW_BYTES) && row[0] <= slot_bytes &&
           row[1] <= slot_bytes - row[0] && row[1] >= sizeof header &&
           row[1] <= I18N_FILE_CAPACITY &&
           board_slot_read(table->slot, row[0], header, sizeof header) &&
           i18n_pack_info(header, sizeof header, &info) && row[1] == info.size;
}

static bool read_table(LanguageTable *table, uint32_t slot, uint32_t slot_bytes)
{
    uint32_t words[2];
    table->slot = slot;
    if (!matches_running_code(slot) ||
        !board_slot_read(slot, BOARD_LANGUAGE_TABLE_OFFSET, words, sizeof words) ||
        words[0] != BOARD_LANGUAGE_TABLE_MAGIC || words[1] == 0 || words[1] > LANGUAGE_CAPACITY) {
        return false;
    }
    uint32_t offset = ROWS_AT + words[1] * ROW_BYTES;
    for (uint32_t index = 0; index < words[1]; index++) {
        if (!read_row(table, index, slot_bytes)) {
            return false;
        }
        uint8_t *row = table->header + ROWS_AT + index * ROW_BYTES;
        bytes_put_le32(row, offset);
        bytes_put_le32(row + 4, table->rows[index][1]);
        offset += table->rows[index][1];
    }
    bytes_put_le32(table->header + COUNT_AT, words[1]);
    bytes_put_le32(table->header + SIZE_AT, offset);
    bytes_put_le32(table->header + TABLE_AT + 4, words[1]);
    return true;
}

static bool stream_pack(const UserStateIo *io, const LanguageTable *table, uint32_t index,
                        uint32_t *hash)
{
    const uint32_t *source = table->rows[index];
    uint32_t offset = bytes_le32(table->header + ROWS_AT + index * ROW_BYTES);
    for (uint32_t done = 0; done < source[1];) {
        uint32_t left = source[1] - done;
        uint32_t length = left < io->buffer_bytes ? left : io->buffer_bytes;
        hal_system_keep_alive();
        if (!board_slot_read(table->slot, source[0] + done, io->buffer, length) ||
            !io->write(io->context, &PLACE, (UserStateChunk){offset + done, io->buffer, length})) {
            return false;
        }
        *hash = hash_update(*hash, io->buffer, length);
        done += length;
    }
    return true;
}

static bool write_descriptor(const UserStateIo *io, LanguageTable *table)
{
    uint8_t *header = table->header;
    uint32_t count = bytes_le32(header + COUNT_AT);
    uint32_t header_bytes = ROWS_AT + count * ROW_BYTES;
    uint32_t hash = hash_update(HASH_START, header, CHECKSUM_AT);
    hash = hash_update(hash, header + TABLE_AT, header_bytes - TABLE_AT);
    uint8_t invalid[4] = {0};
    if (!io->create(io->context, &PLACE, bytes_le32(header + SIZE_AT)) ||
        !io->write(io->context, &PLACE, (UserStateChunk){0, invalid, sizeof invalid})) {
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        if (!stream_pack(io, table, index, &hash)) {
            return false;
        }
    }
    bytes_put_le32(header + CHECKSUM_AT, hash);
    return io->write(io->context, &PLACE, (UserStateChunk){0, header, header_bytes});
}

void language_descriptor_refresh(const UserStateIo *io, const char *build)
{
    LanguageTable table = {0};
    encode_header(table.header, build);
    EmmcInfo info;
    if (current(io, table.header) || !emmc_start(&info)) {
        return;
    }
    uint32_t bytes = info.layout.slot_sectors * SECTOR_BYTES;
    if (read_table(&table, info.layout.slot_1, bytes) ||
        read_table(&table, info.layout.slot_2, bytes)) {
        (void)write_descriptor(io, &table);
    }
}
