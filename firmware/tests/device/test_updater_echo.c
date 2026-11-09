#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/image_crc.h"
#include "device/updater.h"

enum {
    IMAGE_SIZE = 0x500004,
    FW_SIZE = 0x44C000,
    FW_END = 0x500000,
    STOCK_SIZE = 39845892,
    STOCK_FW_SIZE = 0x025CC000,
    STOCK_LENGTH = 0x2600000,
    SLOT_1 = 16,
    SLOT_SECTORS = 0x2600000 / 512,
    SLOT_2 = SLOT_1 + SLOT_SECTORS,
    STORAGE_SECTORS = SLOT_2 + SLOT_SECTORS,
    CHUNK_BYTES = 8192,
    PAYLOAD_END = 0x5000,
    CODE_OFFSET = 0xD68,
    CODE_SIZE = PAYLOAD_END - CODE_OFFSET - 8,
    MARKER_LENGTH = 11
};

#define RESET_HANDLER 0x03050101u

static uint8_t *sectors;
static uint8_t *bytes;
static uint32_t size;
static uint32_t writes_below_slot_1;
static uint32_t last_done;
static uint32_t last_total;

static void put_le32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static uint8_t *own_image(const char *brand)
{
    uint8_t *image = calloc(IMAGE_SIZE, 1);
    for (uint32_t index = 0; index < 0x1F4; index++) {
        image[index] = (uint8_t)(index + 1);
    }
    memset(image + 0x10, 0, 8);
    memcpy(image + 0x10, brand, strlen(brand));
    put_le32(image + 0x1F4, FW_SIZE);
    memcpy(image + 0x1F8, "RKnanoFW", 8);
    put_le32(image + 0x80, PAYLOAD_END);
    for (uint32_t index = CODE_OFFSET; index < PAYLOAD_END; index++) {
        image[index] = (uint8_t)(index * 13 + 1);
    }
    put_le32(image + 0x200, 0x03050000);
    put_le32(image + 0x204, 91);
    uint32_t rows[3][4] = {{0x03050B68, 0x03050000, CODE_SIZE, 0x03050B68 + CODE_SIZE},
                           {0x03004000, 8, 0x03004008, 0x100},
                           {0x03078CD0, 0x03079DCC, 4, 0}};
    for (int row = 0; row < 3; row++) {
        for (int word = 0; word < 4; word++) {
            put_le32(image + 0x208 + 16 * row + 4 * word, rows[row][word]);
        }
    }
    put_le32(image + CODE_OFFSET, 0x03004000);
    put_le32(image + CODE_OFFSET + 4, RESET_HANDLER);
    put_le32(image + CODE_OFFSET + CODE_SIZE, 0x03004000);
    put_le32(image + CODE_OFFSET + CODE_SIZE + 4, RESET_HANDLER);
    memcpy(image + FW_SIZE, image, 512);
    put_le32(image + FW_END, image_crc_update(0, image, FW_END));
    return image;
}

static void seal_stock(uint8_t *image)
{
    memcpy(image + STOCK_FW_SIZE, image, 512);
    put_le32(image + STOCK_LENGTH, image_crc_update(0, image, STOCK_LENGTH));
}

static uint8_t *echo_stock_image(const char *marker, uint32_t marker_at)
{
    uint8_t *image = calloc(STOCK_SIZE, 1);
    for (uint32_t index = 0; index < 0x1F4; index++) {
        image[index] = (uint8_t)(index * 3 + 7);
    }
    memcpy(image + 0x10, "Rockchip", 8);
    put_le32(image + 0x1F4, STOCK_FW_SIZE);
    memcpy(image + 0x1F8, "RKnanoFW", 8);
    for (uint32_t index = 0x200; index < STOCK_FW_SIZE; index += 4093) {
        image[index] = (uint8_t)(index >> 4);
    }
    if (marker != NULL) {
        memcpy(image + marker_at, marker, MARKER_LENGTH);
    }
    seal_stock(image);
    return image;
}

static bool storage_read(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    (void)context;
    if (lba + count > STORAGE_SECTORS) {
        return false;
    }
    memcpy(buffer, sectors + (size_t)lba * 512, (size_t)count * 512);
    return true;
}

static bool storage_write(void *context, uint32_t lba, uint32_t count, const void *buffer)
{
    (void)context;
    if (lba + count > STORAGE_SECTORS) {
        return false;
    }
    writes_below_slot_1 += lba < SLOT_1 ? 1 : 0;
    memcpy(sectors + (size_t)lba * 512, buffer, (size_t)count * 512);
    return true;
}

static bool file_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (offset > size || length > size - offset) {
        return false;
    }
    memcpy(buffer, bytes + offset, length);
    return true;
}

static void report(void *context, uint32_t done, uint32_t total)
{
    (void)context;
    last_done = done;
    last_total = total;
}

static void load(uint8_t *image, uint32_t image_size)
{
    free(sectors);
    free(bytes);
    sectors = calloc(STORAGE_SECTORS, 512);
    bytes = image;
    size = image_size;
    writes_below_slot_1 = 0;
    last_done = 0;
    last_total = 0;
}

static UpdateCheck check_on(uint8_t *image, uint32_t image_size)
{
    load(image, image_size);
    UpdateStorage storage = {storage_read, storage_write, NULL, SLOT_1, SLOT_2, SLOT_SECTORS};
    UpdateFile file = {file_read, size, NULL};
    return updater_check(&file, &storage);
}

static UpdateCheck check_echo_stock(uint8_t *image)
{
    return check_on(image, STOCK_SIZE);
}

static void a_podium_image_is_valid(void)
{
    CHECK_EQUAL(UPDATE_VALID, check_on(own_image("PODIUM"), IMAGE_SIZE));
}

static void an_echopod_or_recho_image_is_refused(void)
{
    CHECK_EQUAL(UPDATE_INVALID, check_on(own_image("EchoPod"), IMAGE_SIZE));
    CHECK_EQUAL(UPDATE_INVALID, check_on(own_image("RECHO"), IMAGE_SIZE));
}

static void a_damaged_podium_image_is_invalid(void)
{
    uint8_t *image = own_image("PODIUM");
    image[0x2000] ^= 1;
    CHECK_EQUAL(UPDATE_INVALID, check_on(image, IMAGE_SIZE));
}

static void a_stock_echo_image_is_recognised_as_stock(void)
{
    CHECK_EQUAL(UPDATE_STOCK, check_echo_stock(echo_stock_image("ECHOV100IMG", 0x54000)));
}

static void a_stock_echo_marker_across_two_reads_is_found(void)
{
    CHECK_EQUAL(UPDATE_STOCK,
                check_echo_stock(echo_stock_image("ECHOV100IMG", 7 * CHUNK_BYTES - 5)));
    CHECK_EQUAL(UPDATE_STOCK,
                check_echo_stock(echo_stock_image("ECHOV100IMG", 7 * CHUNK_BYTES - 1)));
}

static void a_stock_mini_image_is_refused(void)
{
    uint8_t *image = calloc(0x2000004, 1);
    memcpy(image + 0x10, "Rockchip", 8);
    put_le32(image + 0x1F4, 0x01FCC000);
    memcpy(image + 0x1F8, "RKnanoFW", 8);
    memcpy(image + 0x54000, "HIFIEC00IMG", MARKER_LENGTH);
    memcpy(image + 0x01FCC000, image, 512);
    put_le32(image + 0x2000000, image_crc_update(0, image, 0x2000000));
    CHECK_EQUAL(UPDATE_INVALID, check_on(image, 0x2000004));
}

static void stock_echo_images_without_the_marker_or_with_a_mini_marker_are_invalid(void)
{
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(echo_stock_image(NULL, 0)));
    uint8_t *image = echo_stock_image("ECHOV100IMG", 0x54000);
    memcpy(image + 0x80000, "HIFIEC00IMG", MARKER_LENGTH);
    seal_stock(image);
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
    image = echo_stock_image("ECHOV100IMG", 0x54000);
    memcpy(image + 0x80000, "MINIV100IMG", MARKER_LENGTH);
    seal_stock(image);
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
}

static void stock_echo_images_with_the_wrong_shape_are_invalid(void)
{
    uint8_t *image = echo_stock_image("ECHOV100IMG", 0x54000);
    put_le32(image + 0x1F4, STOCK_FW_SIZE - 0x2000);
    seal_stock(image);
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
    image = echo_stock_image("ECHOV100IMG", 0x54000);
    memcpy(image + 0x10, "PODIUM\0\0", 8);
    seal_stock(image);
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
    image = echo_stock_image("ECHOV100IMG", 0x54000);
    image[0x100000] ^= 1;
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
    image = echo_stock_image("ECHOV100IMG", 0x54000);
    image[STOCK_FW_SIZE + 5] ^= 1;
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
    image = echo_stock_image("ECHOV100IMG", 0x54000);
    image[0x1F8] = 'X';
    seal_stock(image);
    CHECK_EQUAL(UPDATE_INVALID, check_echo_stock(image));
    CHECK_EQUAL(UPDATE_INVALID, check_on(echo_stock_image("ECHOV100IMG", 0x54000), STOCK_SIZE - 4));
}

static void a_stock_echo_image_needs_a_slot_it_fits(void)
{
    load(echo_stock_image("ECHOV100IMG", 0x54000), STOCK_SIZE);
    UpdateStorage storage = {storage_read, storage_write, NULL,
                             SLOT_1,       SLOT_2,        STOCK_LENGTH / 512 - 1};
    UpdateFile file = {file_read, size, NULL};
    CHECK_EQUAL(UPDATE_INVALID, updater_check(&file, &storage));
    storage.slot_sectors = STOCK_LENGTH / 512;
    CHECK_EQUAL(UPDATE_STOCK, updater_check(&file, &storage));
}

static void installing_a_stock_echo_image_fills_both_slots_with_every_byte_slot_2_first(void)
{
    uint8_t *image = echo_stock_image("ECHOV100IMG", 0x54000);
    load(image, STOCK_SIZE);
    UpdateStorage storage = {storage_read, storage_write, NULL, SLOT_1, SLOT_2, SLOT_SECTORS};
    UpdateFile file = {file_read, size, NULL};
    CHECK(updater_install(&file, &storage, (UpdateProgress){report, NULL}));
    CHECK(memcmp(sectors + (size_t)SLOT_2 * 512, image, STOCK_LENGTH) == 0);
    CHECK(memcmp(sectors + (size_t)SLOT_1 * 512, image, STOCK_LENGTH) == 0);
    CHECK_EQUAL(0, writes_below_slot_1);
    CHECK_EQUAL(last_total, last_done);
    CHECK_EQUAL(2 * (STOCK_LENGTH / CHUNK_BYTES), last_total);
}

static void installing_a_podium_image_fills_both_slots(void)
{
    uint8_t *image = own_image("PODIUM");
    load(image, IMAGE_SIZE);
    UpdateStorage storage = {storage_read, storage_write, NULL, SLOT_1, SLOT_2, SLOT_SECTORS};
    UpdateFile file = {file_read, size, NULL};
    CHECK(updater_install(&file, &storage, (UpdateProgress){report, NULL}));
    CHECK(memcmp(sectors + (size_t)SLOT_2 * 512, image, FW_END) == 0);
    CHECK(memcmp(sectors + (size_t)SLOT_1 * 512, image, FW_END) == 0);
    CHECK_EQUAL(UPDATE_ALREADY_INSTALLED, updater_check(&file, &storage));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_podium_image_is_valid),
        TEST_CASE(an_echopod_or_recho_image_is_refused),
        TEST_CASE(a_damaged_podium_image_is_invalid),
        TEST_CASE(a_stock_echo_image_is_recognised_as_stock),
        TEST_CASE(a_stock_echo_marker_across_two_reads_is_found),
        TEST_CASE(a_stock_mini_image_is_refused),
        TEST_CASE(stock_echo_images_without_the_marker_or_with_a_mini_marker_are_invalid),
        TEST_CASE(stock_echo_images_with_the_wrong_shape_are_invalid),
        TEST_CASE(a_stock_echo_image_needs_a_slot_it_fits),
        TEST_CASE(installing_a_stock_echo_image_fills_both_slots_with_every_byte_slot_2_first),
        TEST_CASE(installing_a_podium_image_fills_both_slots),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
