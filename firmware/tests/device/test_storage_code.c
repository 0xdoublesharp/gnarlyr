#include <string.h>

#include "check.h"
#include "device/image_crc.h"
#include "device/storage_code.h"

enum {
    MAIN_CRC = 0x12345678,
    MODULE_BYTES = 128
};

static uint8_t module[MODULE_BYTES];

static void put32(uint32_t offset, uint32_t value)
{
    for (int byte = 0; byte < 4; byte++) {
        module[offset + (uint32_t)byte] = (uint8_t)(value >> (8 * byte));
    }
}

static void checksum(void)
{
    put32(8, image_crc_update(0, module + STORAGE_CODE_HEADER_BYTES,
                              MODULE_BYTES - STORAGE_CODE_HEADER_BYTES));
}

static void valid_module(void)
{
    memset(module, 0, sizeof module);
    put32(0, STORAGE_CODE_MAGIC);
    put32(4, sizeof module);
    put32(12, MAIN_CRC);
    put32(16, STORAGE_CODE_API_MAGIC);
    put32(20, STORAGE_CODE_ABI);
    put32(24, STORAGE_CODE_API_BYTES);
    for (uint32_t index = 0; index < STORAGE_CODE_FUNCTIONS; index++) {
        put32(28 + index * 4, STORAGE_CODE_BASE + STORAGE_CODE_MINIMUM + 1);
    }
    checksum();
}

static void a_module_is_bound_to_the_current_main_image_and_validated_entry_table(void)
{
    valid_module();
    uint32_t size = 0;
    CHECK(storage_code_size(module, sizeof module, &size));
    CHECK_EQUAL(MODULE_BYTES, size);
    CHECK(storage_code_validate(module, sizeof module, MAIN_CRC));
    CHECK(!storage_code_validate(module, sizeof module, MAIN_CRC + 1));
}

static void headers_outside_the_reserved_region_are_refused(void)
{
    uint32_t size;
    valid_module();
    CHECK(!storage_code_size(module, 15, &size));
    for (uint32_t length = 0; length < STORAGE_CODE_MINIMUM; length++) {
        put32(4, length);
        CHECK(!storage_code_size(module, sizeof module, &size));
    }
    put32(4, STORAGE_CODE_CAPACITY + 4);
    CHECK(!storage_code_size(module, sizeof module, &size));
    put32(4, MODULE_BYTES + 1);
    CHECK(!storage_code_size(module, sizeof module, &size));
    put32(4, MODULE_BYTES);
    put32(0, 0);
    CHECK(!storage_code_size(module, sizeof module, &size));
}

static void corrupt_payloads_and_incompatible_entry_tables_are_refused(void)
{
    valid_module();
    module[MODULE_BYTES - 1] ^= 1;
    CHECK(!storage_code_validate(module, sizeof module, MAIN_CRC));
    valid_module();
    put32(20, STORAGE_CODE_ABI + 1);
    checksum();
    CHECK(!storage_code_validate(module, sizeof module, MAIN_CRC));
    valid_module();
    put32(24, STORAGE_CODE_API_BYTES - 4);
    checksum();
    CHECK(!storage_code_validate(module, sizeof module, MAIN_CRC));
    CHECK(!storage_code_validate(module, sizeof module - 4, MAIN_CRC));
}

static void every_function_must_be_a_thumb_entry_in_the_module(void)
{
    static const uint32_t BAD[] = {0,
                                   STORAGE_CODE_BASE + STORAGE_CODE_MINIMUM,
                                   STORAGE_CODE_BASE + 17,
                                   STORAGE_CODE_BASE + MODULE_BYTES + 1,
                                   0x03050101,
                                   UINT32_MAX};
    for (uint32_t function = 0; function < STORAGE_CODE_FUNCTIONS; function++) {
        for (size_t index = 0; index < sizeof BAD / sizeof BAD[0]; index++) {
            valid_module();
            put32(28 + 4 * function, BAD[index]);
            checksum();
            CHECK(!storage_code_validate(module, sizeof module, MAIN_CRC));
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_module_is_bound_to_the_current_main_image_and_validated_entry_table),
        TEST_CASE(headers_outside_the_reserved_region_are_refused),
        TEST_CASE(corrupt_payloads_and_incompatible_entry_tables_are_refused),
        TEST_CASE(every_function_must_be_a_thumb_entry_in_the_module),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
