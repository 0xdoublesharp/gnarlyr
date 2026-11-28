#include <string.h>

#include "board.h"
#include "core/bytes.h"
#include "core/hash.h"
#include "emmc.h"
#include "hal/audio.h"
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

typedef struct DescriptorStream {
    const UserStateIo *io;
    uint32_t hash;
    uint32_t offset;
    uint32_t used;
    bool writing;
} DescriptorStream;

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

static bool matches_running_code(uint32_t slot)
{
    static const BoardImageTable MODULES = {MODULE_TABLE_OFFSET, MODULE_TABLE_MAGIC,
                                            MODULE_TABLE_ROWS};
    const uint8_t *running = hal_system_loaded_module();
    uint32_t row[2];
    uint32_t crc;
    return running != NULL &&
           board_slot_table_row(slot, &MODULES, TRIAL_BOOT_MODULE_NUMBER - 1, row) &&
           row[0] < 0x500000u - OVERLAY_HEADER_BYTES && row[1] >= OVERLAY_HEADER_BYTES &&
           board_slot_read(slot, row[0] + MODULE_CODE_CRC_AT, &crc, sizeof crc) &&
           crc == bytes_le32(running - OVERLAY_HEADER_BYTES + MODULE_CODE_CRC_AT);
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

static bool flush(DescriptorStream *stream)
{
    const UserStateIo *io = stream->io;
    bool written =
        !stream->writing || stream->used == 0 ||
        io->write(io->context, &PLACE, (UserStateChunk){stream->offset, io->buffer, stream->used});
    stream->offset += stream->used;
    stream->used = 0;
    return written;
}

static bool stream_pack(DescriptorStream *stream, const LanguageTable *table, uint32_t index)
{
    const uint32_t *source = table->rows[index];
    const UserStateIo *io = stream->io;
    for (uint32_t done = 0; done < source[1];) {
        uint32_t room = io->buffer_bytes - stream->used;
        uint32_t left = source[1] - done;
        uint32_t length = left < room ? left : room;
        uint8_t *target = io->buffer + stream->used;
        hal_system_keep_alive();
        hal_audio_service();
        if (!board_slot_read(table->slot, source[0] + done, target, length)) {
            return false;
        }
        stream->hash = hash_update(stream->hash, target, length);
        done += length;
        stream->used += length;
        if (stream->used == io->buffer_bytes && !flush(stream)) {
            return false;
        }
    }
    return true;
}

static bool stream_table(DescriptorStream *stream, const LanguageTable *table)
{
    const uint8_t *header = table->header;
    uint32_t count = bytes_le32(header + COUNT_AT);
    uint32_t header_bytes = ROWS_AT + count * ROW_BYTES;
    stream->hash = hash_update(HASH_START, header, CHECKSUM_AT);
    stream->hash = hash_update(stream->hash, header + TABLE_AT, header_bytes - TABLE_AT);
    if (stream->writing) {
        memcpy(stream->io->buffer, header, header_bytes);
        bytes_put_le32(stream->io->buffer, 0);
        stream->used = header_bytes;
    }
    for (uint32_t index = 0; index < count; index++) {
        if (!stream_pack(stream, table, index)) {
            return false;
        }
    }
    return flush(stream);
}

static bool hash_file_tail(const UserStateIo *io, uint32_t offset, uint32_t size, uint32_t *hash)
{
    while (offset < size) {
        uint32_t left = size - offset;
        uint32_t length = left < io->buffer_bytes ? left : io->buffer_bytes;
        hal_system_keep_alive();
        hal_audio_service();
        if (!io->read(io->context, &PLACE, (UserStateChunk){offset, io->buffer, length})) {
            return false;
        }
        *hash = hash_update(*hash, io->buffer, length);
        offset += length;
    }
    return true;
}

static bool current(const UserStateIo *io, const LanguageTable *table)
{
    const uint8_t *header = table->header;
    uint32_t size = bytes_le32(header + SIZE_AT);
    uint32_t rows_bytes = ROWS_AT + bytes_le32(header + COUNT_AT) * ROW_BYTES;
    uint32_t length = size < io->buffer_bytes ? size : io->buffer_bytes;
    uint8_t *bytes = io->buffer;
    if (io->size(io->context, &PLACE) != size ||
        !io->read(io->context, &PLACE, (UserStateChunk){0, bytes, length}) ||
        memcmp(bytes, header, CHECKSUM_AT) != 0 ||
        memcmp(bytes + TABLE_AT, header + TABLE_AT, rows_bytes - TABLE_AT) != 0) {
        return false;
    }
    uint32_t stored = bytes_le32(bytes + CHECKSUM_AT);
    uint32_t hash = hash_update(HASH_START, bytes, CHECKSUM_AT);
    hash = hash_update(hash, bytes + TABLE_AT, length - TABLE_AT);
    DescriptorStream slot = {.io = io};
    return hash_file_tail(io, length, size, &hash) && hash == stored &&
           stream_table(&slot, table) && slot.hash == stored;
}

static bool write_descriptor(const UserStateIo *io, LanguageTable *table)
{
    uint8_t *header = table->header;
    DescriptorStream stream = {.io = io, .writing = true};
    if (!io->create(io->context, &PLACE, bytes_le32(header + SIZE_AT)) ||
        !stream_table(&stream, table)) {
        return false;
    }
    bytes_put_le32(header + CHECKSUM_AT, stream.hash);
    uint32_t header_bytes = ROWS_AT + bytes_le32(header + COUNT_AT) * ROW_BYTES;
    return io->write(io->context, &PLACE, (UserStateChunk){0, header, header_bytes});
}

void language_descriptor_refresh(const UserStateIo *io, const char *build)
{
    LanguageTable table = {0};
    encode_header(table.header, build);
    EmmcInfo info;
    if (io->buffer_bytes < TABLE_CAPACITY || !emmc_start(&info)) {
        return;
    }
    uint32_t bytes = info.layout.slot_sectors * SECTOR_BYTES;
    if ((read_table(&table, info.layout.slot_1, bytes) ||
         read_table(&table, info.layout.slot_2, bytes)) &&
        !current(io, &table)) {
        (void)write_descriptor(io, &table);
    }
}
