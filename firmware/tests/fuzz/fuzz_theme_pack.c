#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "playback/store.h"
#include "theme_pack.h"

enum {
    BUFFER_BYTES = 1024,
    ASSET_OFFSET = THEME_PACK_HEADER_BYTES,
    ASSET_PAYLOAD_BYTES = 8
};

typedef struct MemorySource {
    const uint8_t *bytes;
    uint32_t size;
} MemorySource;

static uint8_t structured[BUFFER_BYTES];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static bool read_memory(void *context, uint32_t offset, void *destination, uint32_t count)
{
    const MemorySource *source = context;
    if (offset > source->size || count > source->size - offset) {
        return false;
    }
    memcpy(destination, source->bytes + offset, count);
    return true;
}

static void seal(uint32_t size)
{
    store_put32(structured + 8, size);
    uint32_t crc = theme_pack_crc(UINT32_MAX, structured, 12);
    crc = theme_pack_crc(crc, structured + 16, size - 16);
    store_put32(structured + 12, ~crc);
}

static uint32_t sample(void)
{
    memset(structured, 0, sizeof structured);
    memcpy(structured, "DSTP", 4);
    store_put16(structured + 4, 1);
    store_put16(structured + 6, THEME_PACK_HEADER_BYTES);
    structured[16] = 1;
    structured[18] = 20;
    structured[20] = 1;
    memcpy(structured + 36, "Fuzz Theme", 11);
    memcpy(structured + 68, "Fuzzer", 7);
    memcpy(structured + 100, "1.0.0", 6);
    memcpy(structured + 132, "CC0-1.0", 8);
    for (int color = 0; color < 20; color++) {
        store_put16(structured + 452 + 2 * color, color == 0 ? 0 : UINT16_MAX);
    }
    seal(THEME_PACK_HEADER_BYTES);
    return THEME_PACK_HEADER_BYTES;
}

static void asset(uint32_t offset, uint16_t slot, uint8_t orientation, uint8_t text_size,
                  uint32_t payload)
{
    store_put16(structured + offset, slot);
    structured[offset + 2] = orientation;
    structured[offset + 3] = text_size;
    store_put16(structured + offset + 4, 2);
    store_put16(structured + offset + 6, 2);
    store_put32(structured + offset + 8, payload);
    store_put32(structured + offset + 12, ASSET_PAYLOAD_BYTES);
}

static uint8_t byte_at(const uint8_t *data, size_t size, size_t at)
{
    return size == 0 ? 0 : data[at % size];
}

static uint32_t one_asset(const uint8_t *data, size_t size)
{
    sample();
    structured[19] = 1;
    uint32_t payload = THEME_PACK_HEADER_BYTES + THEME_PACK_ASSET_BYTES;
    asset(ASSET_OFFSET, 3, 2, THEME_PACK_ANY, payload);
    uint8_t choice = byte_at(data, size, 1) % 8;
    uint32_t value = (uint32_t)byte_at(data, size, 2) | (uint32_t)byte_at(data, size, 3) << 8 |
                     (uint32_t)byte_at(data, size, 4) << 16 |
                     (uint32_t)byte_at(data, size, 5) << 24;
    if (choice == 0) {
        store_put16(structured + ASSET_OFFSET, (uint16_t)value);
    } else if (choice == 1) {
        structured[ASSET_OFFSET + 2] = (uint8_t)value;
    } else if (choice == 2) {
        structured[ASSET_OFFSET + 3] = (uint8_t)value;
    } else if (choice == 3) {
        store_put16(structured + ASSET_OFFSET + 4, (uint16_t)value);
    } else if (choice == 4) {
        store_put16(structured + ASSET_OFFSET + 6, (uint16_t)value);
    } else if (choice == 5) {
        store_put32(structured + ASSET_OFFSET + 8, value);
    } else if (choice == 6) {
        store_put32(structured + ASSET_OFFSET + 12, value);
    }
    uint32_t length = payload + ASSET_PAYLOAD_BYTES;
    if (choice == 7) {
        length =
            THEME_PACK_HEADER_BYTES + value % (THEME_PACK_ASSET_BYTES + ASSET_PAYLOAD_BYTES + 1);
    }
    seal(length);
    return length;
}

static uint32_t two_assets(const uint8_t *data, size_t size)
{
    sample();
    structured[19] = 2;
    uint32_t payload = THEME_PACK_HEADER_BYTES + 2 * THEME_PACK_ASSET_BYTES;
    asset(ASSET_OFFSET, 3, 2, THEME_PACK_ANY, payload);
    asset(ASSET_OFFSET + THEME_PACK_ASSET_BYTES, 3, byte_at(data, size, 1) % 3,
          byte_at(data, size, 2) & 1u ? THEME_PACK_ANY : byte_at(data, size, 3) % 3,
          payload + ASSET_PAYLOAD_BYTES);
    uint32_t length = payload + 2 * ASSET_PAYLOAD_BYTES;
    seal(length);
    return length;
}

static void check_open(const uint8_t *bytes, uint32_t size)
{
    MemorySource source = {bytes, size};
    ThemePack pack;
    memset(&pack, 0xA5, sizeof pack);
    ThemePack before = pack;
    bool opened = theme_pack_open(&pack, (ByteSource){read_memory, &source}, size);
    if (!opened) {
        fuzz_require(memcmp(&pack, &before, sizeof pack) == 0);
        return;
    }
    ThemePack repeated;
    fuzz_require(theme_pack_open(&repeated, (ByteSource){read_memory, &source}, size));
    fuzz_require(memcmp(&pack, &repeated, sizeof pack) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_require(size <= UINT32_MAX);
    uint8_t mode = size == 0 ? 0 : data[0] & 3u;
    if (mode == 0) {
        check_open(data, (uint32_t)size);
    } else if (mode == 1) {
        uint32_t length = sample();
        uint32_t offset = byte_at(data, size, 1) | (uint32_t)byte_at(data, size, 2) << 8;
        offset %= THEME_PACK_HEADER_BYTES;
        if (offset < 12 || offset >= 16) {
            structured[offset] = byte_at(data, size, 3);
        }
        seal(length);
        check_open(structured, length);
    } else if (mode == 2) {
        uint32_t length = one_asset(data, size);
        check_open(structured, length);
    } else {
        uint32_t length = two_assets(data, size);
        check_open(structured, length);
    }
    return 0;
}
