#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "playback/store.h"
#include "theme_pack.h"
#include "theme_signature.h"

static uint8_t bytes[1024];
static uint32_t length;

static bool read_bytes(void *context, uint32_t offset, void *buffer, uint32_t count)
{
    (void)context;
    if (offset > length || count > length - offset) {
        return false;
    }
    memcpy(buffer, bytes + offset, count);
    return true;
}

static void seal(void)
{
    store_put32(bytes + 8, length);
    uint32_t crc = theme_pack_crc(UINT32_MAX, bytes, 12);
    crc = theme_pack_crc(crc, bytes + 16, length - 16);
    store_put32(bytes + 12, ~crc);
}

static void sample(void)
{
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, "DSTP", 4);
    store_put16(bytes + 4, 1);
    store_put16(bytes + 6, THEME_PACK_HEADER_BYTES);
    bytes[16] = 1;
    bytes[18] = 20;
    bytes[20] = 1;
    memcpy(bytes + 36, "Original", 9);
    memcpy(bytes + 68, "Maker", 6);
    memcpy(bytes + 100, "1.0.0", 6);
    memcpy(bytes + 132, "CC0-1.0", 8);
    for (int color = 0; color < 20; color++) {
        store_put16(bytes + 452 + color * 2, color == 0 ? 0 : UINT16_MAX);
    }
    length = THEME_PACK_HEADER_BYTES;
    seal();
}

static bool open_pack(ThemePack *pack)
{
    return theme_pack_open(pack, (ByteSource){read_bytes, NULL}, length);
}

static void a_palette_only_pack_is_complete_and_keeps_metadata(void)
{
    sample();
    ThemePack pack;
    CHECK(open_pack(&pack));
    CHECK_EQUAL(0, pack.asset_count);
    CHECK_EQUAL(UINT16_MAX, pack.palette.colors.text);
    CHECK(strcmp("Original", pack.name) == 0);
    CHECK(strcmp("Maker", pack.author) == 0);
    CHECK(strcmp("1.0.0", pack.version) == 0);
    CHECK_EQUAL(1, pack.id[0]);
    CHECK(!pack.verified);
}

static void bad_crc_truncation_and_oversize_never_apply(void)
{
    sample();
    ThemePack pack = {.base = 99};
    bytes[452] ^= 1;
    CHECK(!open_pack(&pack));
    CHECK_EQUAL(99, pack.base);
    sample();
    length--;
    CHECK(!open_pack(&pack));
    CHECK(!theme_pack_open(&pack, (ByteSource){read_bytes, NULL}, THEME_PACK_MAX_BYTES + 1));
}

static void versions_dimensions_offsets_duplicates_and_metadata_are_strict(void)
{
    ThemePack pack;
    const int bad_fields[] = {4, 6, 16, 17, 18, 493, 511};
    for (unsigned index = 0; index < sizeof bad_fields / sizeof bad_fields[0]; index++) {
        sample();
        bytes[bad_fields[index]] = 255;
        seal();
        CHECK(!open_pack(&pack));
    }
    sample();
    memset(bytes + 36, 'a', 32);
    seal();
    CHECK(!open_pack(&pack));
    sample();
    bytes[19] = 1;
    length += 16 + 8;
    store_put16(bytes + 512, 3);
    bytes[515] = 255;
    store_put16(bytes + 516, 2);
    store_put16(bytes + 518, 2);
    store_put32(bytes + 520, 528);
    store_put32(bytes + 524, 8);
    seal();
    CHECK(open_pack(&pack));
    store_put16(bytes + 516, 481);
    seal();
    CHECK(!open_pack(&pack));
    store_put16(bytes + 516, 2);
    store_put32(bytes + 520, 512);
    seal();
    CHECK(!open_pack(&pack));
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
        TEST_CASE(bad_crc_truncation_and_oversize_never_apply),
        TEST_CASE(versions_dimensions_offsets_duplicates_and_metadata_are_strict),
        TEST_CASE(low_contrast_is_a_warning),
        TEST_CASE(ed25519ph_checks_the_rfc8032_vector_and_rejects_a_changed_signature),
        TEST_CASE(signature_trailers_require_the_exact_framing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
