#include <string.h>

#include "board.h"
#include "check.h"
#include "core/hash.h"
#include "emmc.h"
#include "hal/system.h"
#include "overlay.h"
#include "test_pack.h"
#include "trial_module.h"

enum {
    SLOT = 1,
    SECTOR_BYTES = 512,
    PACK_AT = 0x240000,
    MODULE_AT = 0x280000,
    MODULE_CRC_AT = 12,
    CHECKSUM_AT = 40,
    TABLE_AT = 44,
    PACK_BODY_AT = 60 + I18N_INDEX_BYTES
};

static const char BUILD[] = "test-build";
static const uint32_t MODULE_HEADER[] = {0, 0, 0, 0x76543210};
static uint8_t pack[TEST_PACK_CAPACITY];
static uint32_t pack_size;
static uint8_t file[TEST_PACK_CAPACITY];
static uint32_t file_size;
static int creates;
static uint8_t buffer[SECTOR_BYTES];

bool emmc_start(EmmcInfo *info)
{
    *info = (EmmcInfo){.layout = {.slot_1 = SLOT, .slot_sectors = 0x500000 / SECTOR_BYTES}};
    return true;
}

const void *hal_system_loaded_module(void)
{
    return MODULE_HEADER + sizeof MODULE_HEADER / sizeof MODULE_HEADER[0];
}

bool board_slot_table_row(uint32_t slot, const BoardImageTable *table, uint32_t index,
                          uint32_t *row)
{
    CHECK_EQUAL(SLOT, slot);
    CHECK_EQUAL(MODULE_TABLE_OFFSET, table->offset);
    CHECK_EQUAL(TRIAL_BOOT_MODULE_NUMBER - 1, index);
    row[0] = MODULE_AT;
    row[1] = OVERLAY_HEADER_BYTES;
    return true;
}

bool board_slot_read(uint32_t slot, uint32_t at, void *out, uint32_t length)
{
    CHECK_EQUAL(SLOT, slot);
    const uint32_t table[] = {BOARD_LANGUAGE_TABLE_MAGIC, 1, PACK_AT, pack_size};
    if (at == MODULE_AT + MODULE_CRC_AT && length == sizeof(uint32_t)) {
        memcpy(out, (const uint8_t *)MODULE_HEADER + MODULE_CRC_AT, length);
    } else if (at - BOARD_LANGUAGE_TABLE_OFFSET < sizeof table) {
        memcpy(out, (const uint8_t *)table + at - BOARD_LANGUAGE_TABLE_OFFSET, length);
    } else {
        uint32_t offset = at - PACK_AT;
        CHECK(offset <= pack_size && length <= pack_size - offset);
        memcpy(out, pack + offset, length);
    }
    return true;
}

void hal_system_keep_alive(void)
{
}

void hal_audio_service(void)
{
}

static uint32_t size(void *context, const UserStatePlace *place)
{
    (void)context;
    CHECK_EQUAL(USER_STATE_INTERNAL, place->volume);
    return file_size;
}

static bool read(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    CHECK(chunk.offset <= size(context, place) && chunk.length <= file_size - chunk.offset);
    memcpy(chunk.bytes, file + chunk.offset, chunk.length);
    return true;
}

static bool create(void *context, const UserStatePlace *place, uint32_t length)
{
    (void)size(context, place);
    CHECK(length <= sizeof file);
    file_size = length;
    creates++;
    return true;
}

static bool write(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    CHECK(chunk.offset <= size(context, place) && chunk.length <= file_size - chunk.offset);
    memcpy(file + chunk.offset, chunk.bytes, chunk.length);
    return true;
}

static const UserStateIo IO = {
    .size = size,
    .read = read,
    .create = create,
    .write = write,
    .buffer = buffer,
    .buffer_bytes = sizeof buffer,
};

static uint32_t checksum(void)
{
    return hash_update(hash_update(HASH_START, file, CHECKSUM_AT), file + TABLE_AT,
                       file_size - TABLE_AT);
}

static void start(void)
{
    file_size = USER_STATE_MISSING;
    creates = 0;
    pack_size = test_pack_build(pack, (TestPackLanguage){"en", "English", NULL});
    language_descriptor_refresh(&IO, BUILD);
    CHECK_EQUAL(1, creates);
    CHECK_EQUAL(60 + pack_size, file_size);
    CHECK_EQUAL(checksum(), hash_load(file + CHECKSUM_AT));
}

static void a_current_descriptor_is_kept(void)
{
    start();
    language_descriptor_refresh(&IO, BUILD);
    CHECK_EQUAL(1, creates);
}

static void a_descriptor_with_a_damaged_body_is_rewritten(void)
{
    start();
    file[PACK_BODY_AT] ^= 1;
    language_descriptor_refresh(&IO, BUILD);
    CHECK_EQUAL(2, creates);
    CHECK(memcmp(pack, file + 60, pack_size) == 0);
    CHECK_EQUAL(checksum(), hash_load(file + CHECKSUM_AT));
}

static void a_descriptor_with_a_valid_checksum_for_different_bytes_is_rewritten(void)
{
    start();
    file[PACK_BODY_AT] ^= 1;
    hash_store(file + CHECKSUM_AT, checksum());
    language_descriptor_refresh(&IO, BUILD);
    CHECK_EQUAL(2, creates);
    CHECK(memcmp(pack, file + 60, pack_size) == 0);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(a_current_descriptor_is_kept),
        TEST_CASE(a_descriptor_with_a_damaged_body_is_rewritten),
        TEST_CASE(a_descriptor_with_a_valid_checksum_for_different_bytes_is_rewritten),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
