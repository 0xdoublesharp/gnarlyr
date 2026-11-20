#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "gfx/art_codec.h"
#include "playback/store.h"
#include "theme_fixture.h"
#include "theme_pack.h"
#include "ui/theme.h"

enum {
    BUFFER_BYTES = 4096
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

static Color pixel(int x, int y, int index)
{
    return (Color)(100 + index * 40 + y * 5 + x);
}

static uint32_t sample(void)
{
    static const ThemeFixtureArt art[] = {
        {THEME_ART_LIST, 2, 255, 3, 2, ART_CODEC_LOSSLESS, 1},
        {THEME_ART_LIST, 0, 255, 5, 4, ART_CODEC_INDEXED, 1},
        {THEME_ART_ICON + 1, 0, 255, 20, 20, ART_CODEC_INDEXED, 0}};
    static const uint8_t style[] = {1, 0, 60, 0, 3, 0, 100, 0};
    ThemeFixture fixture = {1, 1, 3, art, pixel, style, sizeof style};
    return theme_fixture_build(structured, sizeof structured, &fixture);
}

static uint8_t byte_at(const uint8_t *data, size_t size, size_t at)
{
    return size == 0 ? 0 : data[at % size];
}

static bool open_as(const uint8_t *bytes, uint32_t size, ThemePack *pack, ThemePackCheck check)
{
    MemorySource source = {bytes, size};
    return theme_pack_open(pack, (ByteSource){read_memory, &source}, size, check);
}

static void check_open(const uint8_t *bytes, uint32_t size)
{
    ThemePack pack;
    memset(&pack, 0xA5, sizeof pack);
    ThemePack before = pack;
    bool structure = open_as(bytes, size, &pack, THEME_PACK_STRUCTURE);
    if (!structure) {
        fuzz_require(memcmp(&pack, &before, sizeof pack) == 0);
    }
    ThemePack complete;
    memset(&complete, 0xA5, sizeof complete);
    if (!open_as(bytes, size, &complete, THEME_PACK_COMPLETE)) {
        fuzz_require(memcmp(&complete, &before, sizeof complete) == 0);
        return;
    }
    fuzz_require(structure);
    ThemePack repeated;
    fuzz_require(open_as(bytes, size, &repeated, THEME_PACK_COMPLETE));
    fuzz_require(memcmp(&complete, &repeated, sizeof complete) == 0);
}

static uint32_t mutated(const uint8_t *data, size_t size)
{
    uint32_t length = sample();
    for (size_t at = 1; at + 3 <= size && at < 64; at += 3) {
        uint32_t offset =
            (byte_at(data, size, at) | (uint32_t)byte_at(data, size, at + 1) << 8) % length;
        if (offset < 8 || offset >= 16) {
            structured[offset] = byte_at(data, size, at + 2);
        }
    }
    theme_fixture_seal(structured, length);
    return length;
}

static uint32_t replaced_payload(const uint8_t *data, size_t size)
{
    uint32_t length = sample();
    uint32_t first = store_get32(structured + THEME_PACK_HEADER_BYTES + 8);
    for (uint32_t at = first; at < length; at++) {
        structured[at] = byte_at(data, size, at - first + 1);
    }
    theme_fixture_seal(structured, length);
    return length;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_require(size <= UINT32_MAX);
    uint8_t mode = size == 0 ? 0 : data[0] % 3u;
    if (mode == 0) {
        check_open(data, (uint32_t)size);
    } else if (mode == 1) {
        uint32_t length = mutated(data, size);
        check_open(structured, length);
    } else {
        uint32_t length = replaced_payload(data, size);
        check_open(structured, length);
    }
    return 0;
}
