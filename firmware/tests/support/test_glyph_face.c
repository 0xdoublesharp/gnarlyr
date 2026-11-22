#include "test_glyph_face.h"

#include <string.h>

#include "core/bytes.h"
#include "core/hash.h"
#include "gfx/font_pack.h"

uint32_t test_glyph_face_codepoint(TestGlyphFace face, uint32_t glyph)
{
    return face.first + face.step * glyph;
}

uint32_t test_glyph_face_index_sectors(TestGlyphFace face)
{
    return (face.count + FONT_PACK_ENTRIES_PER_SECTOR - 1) / FONT_PACK_ENTRIES_PER_SECTOR;
}

uint32_t test_glyph_face_roots(TestGlyphFace face)
{
    return (test_glyph_face_index_sectors(face) + FONT_PACK_DIRECTORY_ENTRIES - 1) /
           FONT_PACK_DIRECTORY_ENTRIES;
}

uint32_t test_glyph_face_data_offset(TestGlyphFace face)
{
    return (1 + test_glyph_face_roots(face) + test_glyph_face_index_sectors(face)) *
           FONT_PACK_SECTOR_BYTES;
}

static uint32_t glyph_width(TestGlyphFace face)
{
    return face.width != 0 ? face.width : TEST_GLYPH_WIDTH;
}

static uint32_t glyph_height(TestGlyphFace face)
{
    return face.height != 0 ? face.height : TEST_GLYPH_HEIGHT;
}

uint32_t test_glyph_face_glyph_bytes(TestGlyphFace face)
{
    return (glyph_width(face) * glyph_height(face) + 1) / 2;
}

uint32_t test_glyph_face_size(TestGlyphFace face)
{
    return test_glyph_face_data_offset(face) + face.count * test_glyph_face_glyph_bytes(face);
}

void test_glyph_face_seal_header(uint8_t *out)
{
    bytes_put_le32(out + FONT_PACK_HEADER_CHECK,
                   hash_update(HASH_START, out, FONT_PACK_HEADER_CHECK));
}

void test_glyph_face_seal_sector(uint8_t *out, uint32_t sector)
{
    uint8_t *bytes = out + sector * FONT_PACK_SECTOR_BYTES;
    uint32_t seed = bytes_le32(out + FONT_PACK_HEADER_CHECK);
    bytes_put_le32(bytes + FONT_PACK_SECTOR_CHECK,
                   hash_update(seed, bytes, FONT_PACK_SECTOR_CHECK));
}

static void write_header(uint8_t *out, TestGlyphFace face)
{
    memcpy(out, "EGLF", 4);
    out[4] = FONT_PACK_VERSION;
    out[5] = FONT_PACK_ENTRY_BYTES;
    bytes_put_le16(out + 6, FONT_PACK_ENTRIES_PER_SECTOR);
    bytes_put_le32(out + 8, face.count);
    bytes_put_le32(out + 12, test_glyph_face_size(face));
    bytes_put_le16(out + 16, test_glyph_face_codepoint(face, 0));
    bytes_put_le16(out + 18, test_glyph_face_codepoint(face, face.count - 1));
    bytes_put_le16(out + 20, test_glyph_face_roots(face));
    bytes_put_le16(out + 22, test_glyph_face_glyph_bytes(face));
    uint32_t per_root = FONT_PACK_DIRECTORY_ENTRIES * FONT_PACK_ENTRIES_PER_SECTOR;
    for (uint32_t root = 0; root < test_glyph_face_roots(face); root++) {
        bytes_put_le16(out + FONT_PACK_ROOT_OFFSET + 2 * root,
                       test_glyph_face_codepoint(face, root * per_root));
    }
    test_glyph_face_seal_header(out);
}

static void write_directory(uint8_t *out, TestGlyphFace face)
{
    uint32_t sectors = test_glyph_face_index_sectors(face);
    for (uint32_t sector = 0; sector < sectors; sector++) {
        uint32_t directory = 1 + sector / FONT_PACK_DIRECTORY_ENTRIES;
        uint8_t *entry =
            out + directory * FONT_PACK_SECTOR_BYTES + 2 * (sector % FONT_PACK_DIRECTORY_ENTRIES);
        bytes_put_le16(entry,
                       test_glyph_face_codepoint(face, sector * FONT_PACK_ENTRIES_PER_SECTOR));
    }
    for (uint32_t root = 0; root < test_glyph_face_roots(face); root++) {
        test_glyph_face_seal_sector(out, 1 + root);
    }
}

static void write_glyph(uint8_t *out, TestGlyphFace face, uint32_t glyph)
{
    uint32_t sector = 1 + test_glyph_face_roots(face) + glyph / FONT_PACK_ENTRIES_PER_SECTOR;
    uint8_t *entry = out + sector * FONT_PACK_SECTOR_BYTES +
                     (glyph % FONT_PACK_ENTRIES_PER_SECTOR) * FONT_PACK_ENTRY_BYTES;
    uint32_t bitmap = test_glyph_face_data_offset(face) + glyph * test_glyph_face_glyph_bytes(face);
    bytes_put_le16(entry, test_glyph_face_codepoint(face, glyph));
    entry[2] = (uint8_t)glyph_width(face);
    entry[3] = (uint8_t)glyph_height(face);
    entry[4] = (uint8_t)TEST_GLYPH_LEFT;
    entry[5] = (uint8_t)TEST_GLYPH_TOP;
    entry[6] = TEST_GLYPH_ADVANCE;
    bytes_put_le32(entry + 8, bitmap);
    out[bitmap] = (uint8_t)((glyph % 16) << 4 | 15);
}

uint32_t test_glyph_face_build(uint8_t *out, TestGlyphFace face)
{
    uint32_t size = test_glyph_face_size(face);
    memset(out, 0, size);
    write_header(out, face);
    write_directory(out, face);
    for (uint32_t glyph = 0; glyph < face.count; glyph++) {
        write_glyph(out, face, glyph);
    }
    uint32_t first_index = 1 + test_glyph_face_roots(face);
    for (uint32_t sector = 0; sector < test_glyph_face_index_sectors(face); sector++) {
        test_glyph_face_seal_sector(out, first_index + sector);
    }
    return size;
}

void test_extra_font_seal(uint8_t *out)
{
    bytes_put_le32(out + FONT_PACK_SECTOR_CHECK,
                   hash_update(HASH_START, out, FONT_PACK_SECTOR_CHECK));
}

uint32_t test_extra_font_face_offset(const uint8_t *file, uint32_t face)
{
    return bytes_le32(file + FONT_PACK_EXTRA_TABLE + 8 * face);
}

static uint32_t sector_rounded(uint32_t bytes)
{
    return (bytes + FONT_PACK_SECTOR_BYTES - 1) / FONT_PACK_SECTOR_BYTES * FONT_PACK_SECTOR_BYTES;
}

static void write_languages(uint8_t *out, const TestExtraFont *font)
{
    out[6] = (uint8_t)font->language_count;
    for (uint32_t code = 0; code < font->language_count && code < FONT_PACK_EXTRA_LANGUAGES;
         code++) {
        size_t length = strlen(font->languages[code]);
        memcpy(out + FONT_PACK_EXTRA_CODES + FONT_PACK_LANGUAGE_BYTES * code, font->languages[code],
               length < 5 ? length : 5);
    }
}

uint32_t test_extra_font_build(uint8_t *out, uint32_t capacity, const TestExtraFont *font)
{
    uint32_t size = FONT_PACK_SECTOR_BYTES;
    memset(out, 0, capacity);
    memcpy(out, "EGXF", 4);
    out[4] = FONT_PACK_VERSION;
    out[5] = FONT_PACK_EXTRA_FACES;
    for (uint32_t face = 0; face < FONT_PACK_EXTRA_FACES; face++) {
        if (font->faces[face].count == 0) {
            continue;
        }
        uint32_t bytes = test_glyph_face_build(out + size, font->faces[face]);
        bytes_put_le32(out + FONT_PACK_EXTRA_TABLE + 8 * face, size);
        bytes_put_le32(out + FONT_PACK_EXTRA_TABLE + 8 * face + 4, bytes);
        size += sector_rounded(bytes);
    }
    write_languages(out, font);
    bytes_put_le32(out + 8, size);
    test_extra_font_seal(out);
    return size;
}
