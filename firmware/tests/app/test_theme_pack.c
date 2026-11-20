#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "gfx/art_codec.h"
#include "playback/store.h"
#include "theme_fixture.h"
#include "theme_pack.h"
#include "theme_signature.h"
#include "ui/icons.h"
#include "ui/theme.h"

static uint8_t bytes[65536];
static uint32_t length;

enum {
    DIRECTORY = THEME_PACK_HEADER_BYTES
};

static bool read_bytes(void *context, uint32_t offset, void *buffer, uint32_t count)
{
    (void)context;
    if (offset > length || count > length - offset) {
        return false;
    }
    memcpy(buffer, bytes + offset, count);
    return true;
}

static Color tile_pixel(int x, int y, int index)
{
    return (Color)(100 + index * 50 + y * 3 + x);
}

static const ThemeFixtureArt TILE = {THEME_ART_LIST, 2, 255, 3, 2, ART_CODEC_LOSSLESS, 1};
static const ThemeFixtureArt INDEXED_TILE = {THEME_ART_LIST, 0, 255, 4, 4, ART_CODEC_INDEXED, 1};

static void build(const ThemeFixtureArt *art, uint8_t count, const uint8_t *style,
                  uint16_t style_bytes)
{
    ThemeFixture fixture = {1, 1, count, art, tile_pixel, style, style_bytes};
    length = theme_fixture_build(bytes, sizeof bytes, &fixture);
}

static void sample(void)
{
    build(NULL, 0, NULL, 0);
}

static void seal(void)
{
    theme_fixture_seal(bytes, length);
}

static bool open_as(ThemePackCheck check)
{
    ThemePack pack;
    return theme_pack_open(&pack, (ByteSource){read_bytes, NULL}, length, check);
}

static bool open_pack(ThemePack *pack)
{
    return theme_pack_open(pack, (ByteSource){read_bytes, NULL}, length, THEME_PACK_COMPLETE);
}

static void a_palette_only_pack_is_complete_and_keeps_metadata(void)
{
    sample();
    ThemePack pack;
    CHECK(open_pack(&pack));
    CHECK_EQUAL(0, pack.asset_count);
    CHECK_EQUAL(UINT16_MAX, pack.palette.colors.text);
    CHECK(strcmp("First", pack.name) == 0);
    CHECK(strcmp("Maker", pack.author) == 0);
    CHECK(strcmp("1.0.0", pack.version) == 0);
    CHECK_EQUAL(1, pack.id[0]);
    CHECK(!pack.verified);
}

static void a_damaged_crc_fails_only_the_complete_check(void)
{
    build(&TILE, 1, NULL, 0);
    ThemePack pack = {.base = 99};
    bytes[452] ^= 1;
    CHECK(!open_pack(&pack));
    CHECK_EQUAL(99, pack.base);
    CHECK(open_as(THEME_PACK_STRUCTURE));
    build(&TILE, 1, NULL, 0);
    length--;
    CHECK(!open_as(THEME_PACK_STRUCTURE));
    CHECK(!theme_pack_open(&pack, (ByteSource){read_bytes, NULL}, THEME_PACK_MAX_BYTES + 1,
                           THEME_PACK_STRUCTURE));
}

static void version_1_and_reserved_header_fields_are_refused(void)
{
    const int bad_fields[] = {4, 6, 16, 17, 18, 19, 493, 494, 495, 496, 511};
    const uint8_t values[] = {1, 255, 255, 255, 255, 65, 255, 2, 2, 1, 1};
    for (unsigned index = 0; index < sizeof bad_fields / sizeof bad_fields[0]; index++) {
        sample();
        bytes[bad_fields[index]] = values[index];
        seal();
        CHECK(!open_as(THEME_PACK_STRUCTURE));
    }
    sample();
    memset(bytes + 36, 'a', 32);
    seal();
    CHECK(!open_as(THEME_PACK_STRUCTURE));
}

typedef struct DescriptorChange {
    int at;
    uint8_t value;
} DescriptorChange;

static void every_descriptor_bound_is_checked(void)
{
    static const DescriptorChange changes[] = {
        {2, 3},  {3, 3},  {4, 0},  {4, 65}, {5, 1},  {6, 0},  {16, 3}, {17, 1}, {18, 1}, {19, 2},
        {19, 0}, {20, 1}, {21, 1}, {22, 1}, {23, 0}, {23, 2}, {0, 6},  {8, 0},  {12, 1}, {1, 9}};
    build(&TILE, 1, NULL, 0);
    CHECK(open_as(THEME_PACK_COMPLETE));
    for (unsigned index = 0; index < sizeof changes / sizeof changes[0]; index++) {
        build(&TILE, 1, NULL, 0);
        bytes[DIRECTORY + changes[index].at] = changes[index].value;
        seal();
        CHECK(!open_as(THEME_PACK_STRUCTURE));
    }
}

static void slots_keep_their_sizes_and_variants_stay_unique(void)
{
    static const ThemeFixtureArt full_tile = {THEME_ART_HOME, 0, 255, 3, 2, ART_CODEC_LOSSLESS, 1};
    static const ThemeFixtureArt either_home = {THEME_ART_HOME,     2, 255, 3, 2,
                                                ART_CODEC_LOSSLESS, 0};
    static const ThemeFixtureArt sized_list = {THEME_ART_LIST, 0, 1, 3, 2, ART_CODEC_LOSSLESS, 1};
    static const ThemeFixtureArt label = {THEME_ART_CASSETTE_LABEL, 2, 255, 160, 47,
                                          ART_CODEC_LOSSLESS,       0};
    static const ThemeFixtureArt icon = {THEME_ART_ICON + 1, 0, 255, 20, 19, ART_CODEC_INDEXED, 0};
    const ThemeFixtureArt *bad[] = {&full_tile, &either_home, &sized_list, &label, &icon};
    for (unsigned index = 0; index < 5; index++) {
        build(bad[index], 1, NULL, 0);
        CHECK(!open_as(THEME_PACK_STRUCTURE));
    }
    const ThemeFixtureArt twice[] = {TILE, TILE};
    build(twice, 2, NULL, 0);
    CHECK(!open_as(THEME_PACK_STRUCTURE));
    const ThemeFixtureArt variants[] = {TILE, INDEXED_TILE};
    build(variants, 2, NULL, 0);
    CHECK(open_as(THEME_PACK_COMPLETE));
}

static void icon_slots_keep_their_numbers_whether_or_not_the_icon_is_drawn(void)
{
    static const ThemeFixtureArt icons[] = {
        {THEME_ART_ICON + ICON_SHUFFLE, 0, 255, 20, 20, ART_CODEC_INDEXED, 0},
        {THEME_ART_ICON + ICON_KEY_MENU, 0, 255, 20, 20, ART_CODEC_INDEXED, 0},
        {THEME_ART_ICON + ICON_SEEK, 0, 255, 14, 14, ART_CODEC_INDEXED, 0},
        {THEME_ART_ICON + ICON_PLUG, 0, 255, 9, 14, ART_CODEC_INDEXED, 0},
    };
    CHECK_EQUAL(256, THEME_ART_ICON);
    CHECK_EQUAL(273, icons[0].slot);
    CHECK_EQUAL(291, icons[1].slot);
    CHECK_EQUAL(300, icons[2].slot);
    CHECK_EQUAL(309, icons[3].slot);
    build(icons, 4, NULL, 0);
    CHECK(open_as(THEME_PACK_COMPLETE));
}

static void fill_stack(void)
{
    volatile uint8_t junk[16384];
    for (size_t at = 0; at < sizeof junk; at++) {
        junk[at] = 0xFF;
    }
}

static void a_transparent_lossless_icon_passes_the_complete_check(void)
{
    static const ThemeFixtureArt icon = {THEME_ART_ICON + 1, 0, 255, 20, 20, ART_CODEC_LOSSLESS, 0};
    build(&icon, 1, NULL, 0);
    uint8_t *descriptor = bytes + DIRECTORY;
    uint32_t payload = store_get32(descriptor + 8);
    descriptor[18] = 1;
    bytes[payload + 2] = 1;
    seal();
    fill_stack();
    CHECK(open_as(THEME_PACK_COMPLETE));
}

static uint32_t table_at(int index)
{
    const uint8_t *descriptor = bytes + DIRECTORY + index * THEME_PACK_ASSET_BYTES;
    uint32_t entries = descriptor[16] == ART_CODEC_INDEXED ? descriptor[17] + 1u : 0;
    return store_get32(descriptor + 8) + 4 + entries * 2 +
           (entries ? (descriptor[18] + 1u) / 2 : 0);
}

static void payloads_are_decode_checked_only_by_the_complete_check(void)
{
    static const int row_changes[] = {0, 4, 8};
    for (unsigned index = 0; index < 3; index++) {
        build(&TILE, 1, NULL, 0);
        uint32_t table = table_at(0);
        store_put32(bytes + table + (uint32_t)row_changes[index],
                    store_get32(bytes + table + (uint32_t)row_changes[index]) + 1);
        seal();
        CHECK(open_as(THEME_PACK_STRUCTURE));
        CHECK(!open_as(THEME_PACK_COMPLETE));
    }
    build(&TILE, 1, NULL, 0);
    bytes[store_get32(bytes + DIRECTORY + 8)] = ART_CODEC_INDEXED;
    seal();
    CHECK(!open_as(THEME_PACK_COMPLETE));
    build(&TILE, 1, NULL, 0);
    bytes[table_at(0) + 12] = 0xC0;
    seal();
    CHECK(!open_as(THEME_PACK_COMPLETE));
    build(&INDEXED_TILE, 1, NULL, 0);
    CHECK(open_as(THEME_PACK_COMPLETE));
    bytes[table_at(0) + 21] = 200;
    seal();
    CHECK(!open_as(THEME_PACK_COMPLETE));
}

static void style_entries_are_bounded_known_and_unique(void)
{
    static const uint8_t good[] = {1, 0, 60, 0, 1, 5, 1, 0, 3, 0, 100, 0, 4, 1, 6, 0};
    build(&TILE, 1, good, sizeof good);
    CHECK(open_as(THEME_PACK_COMPLETE));
    static const uint8_t bad[][4] = {{1, 0, 39, 0}, {1, 6, 1, 0}, {2, 0, 0, 0},
                                     {3, 0, 1, 2},  {4, 1, 5, 0}, {9, 0, 0, 0}};
    for (unsigned index = 0; index < 6; index++) {
        build(&TILE, 1, bad[index], 4);
        CHECK(!open_as(THEME_PACK_STRUCTURE));
    }
    static const uint8_t twice[] = {1, 0, 60, 0, 1, 0, 61, 0};
    build(&TILE, 1, twice, sizeof twice);
    CHECK(!open_as(THEME_PACK_STRUCTURE));
}

static void low_contrast_is_a_warning(void)
{
    sample();
    store_put16(bytes + 454, 0);
    seal();
    ThemePack pack;
    CHECK(open_pack(&pack));
    CHECK(pack.low_contrast);
}

static void ed25519ph_checks_the_rfc8032_vector_and_rejects_a_changed_signature(void)
{
    static const uint8_t public_key[32] = {0xec, 0x17, 0x2b, 0x93, 0xad, 0x5e, 0x56, 0x3b,
                                           0xf4, 0x93, 0x2c, 0x70, 0xe1, 0x24, 0x50, 0x34,
                                           0xc3, 0x54, 0x67, 0xef, 0x2e, 0xfd, 0x4d, 0x64,
                                           0xeb, 0xf8, 0x19, 0x68, 0x34, 0x67, 0xe2, 0xbf};
    uint8_t signature[64] = {0x98, 0xa7, 0x02, 0x22, 0xf0, 0xb8, 0x12, 0x1a, 0xa9, 0xd3, 0x0f,
                             0x81, 0x3d, 0x68, 0x3f, 0x80, 0x9e, 0x46, 0x2b, 0x46, 0x9c, 0x7f,
                             0xf8, 0x76, 0x39, 0x49, 0x9b, 0xb9, 0x4e, 0x6d, 0xae, 0x41, 0x31,
                             0xf8, 0x50, 0x42, 0x46, 0x3c, 0x2a, 0x35, 0x5a, 0x20, 0x03, 0xd0,
                             0x62, 0xad, 0xf5, 0xaa, 0xa1, 0x0b, 0x8c, 0x61, 0xe6, 0x36, 0x06,
                             0x2a, 0xaa, 0xd1, 0x1c, 0x2a, 0x26, 0x08, 0x34, 0x06};
    memcpy(bytes, "abc", 3);
    length = 3;
    CHECK(theme_signature_verify((ByteSource){read_bytes, NULL}, length, signature, public_key));
    signature[0] ^= 1;
    CHECK(!theme_signature_verify((ByteSource){read_bytes, NULL}, length, signature, public_key));
}

static void signature_trailers_require_the_exact_framing(void)
{
    static const uint8_t header[] = {'D', 'S', 'S', 'G', 1, 0, 0, 0};
    bool verified = true;
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, header, sizeof header);
    length = THEME_PACK_SIGNATURE_BYTES;

    CHECK(theme_signature_inspect((ByteSource){read_bytes, NULL}, 0, 0, &verified));
    CHECK(!verified);
    CHECK(!theme_signature_inspect((ByteSource){read_bytes, NULL}, 0,
                                   THEME_PACK_SIGNATURE_BYTES - 1, &verified));
    CHECK(!theme_signature_inspect((ByteSource){read_bytes, NULL}, 1, THEME_PACK_SIGNATURE_BYTES,
                                   &verified));

    for (size_t index = 0; index < sizeof header; index++) {
        memcpy(bytes, header, sizeof header);
        bytes[index] ^= UINT8_C(0x80);
        CHECK(!theme_signature_inspect((ByteSource){read_bytes, NULL}, 0,
                                       THEME_PACK_SIGNATURE_BYTES, &verified));
    }

    memcpy(bytes, header, sizeof header);
    CHECK(theme_signature_inspect((ByteSource){read_bytes, NULL}, 0, THEME_PACK_SIGNATURE_BYTES,
                                  &verified));
    CHECK(!verified);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_palette_only_pack_is_complete_and_keeps_metadata),
        TEST_CASE(a_damaged_crc_fails_only_the_complete_check),
        TEST_CASE(version_1_and_reserved_header_fields_are_refused),
        TEST_CASE(every_descriptor_bound_is_checked),
        TEST_CASE(slots_keep_their_sizes_and_variants_stay_unique),
        TEST_CASE(icon_slots_keep_their_numbers_whether_or_not_the_icon_is_drawn),
        TEST_CASE(a_transparent_lossless_icon_passes_the_complete_check),
        TEST_CASE(payloads_are_decode_checked_only_by_the_complete_check),
        TEST_CASE(style_entries_are_bounded_known_and_unique),
        TEST_CASE(low_contrast_is_a_warning),
        TEST_CASE(ed25519ph_checks_the_rfc8032_vector_and_rejects_a_changed_signature),
        TEST_CASE(signature_trailers_require_the_exact_framing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
