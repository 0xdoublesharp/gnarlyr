#include "theme_pack.h"

#include <stddef.h>

#include "core/hash.h"
#include "core/utf8.h"
#include "gfx/art_codec.h"
#include "hal/audio.h"
#include "playback/store.h"
#include "settings/settings.h"
#include "theme_signature.h"
#include "ui/icons.h"
#include "ui/theme.h"

uint32_t theme_pack_crc(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
    static const uint32_t table[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
                                       0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
                                       0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                       0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    for (uint32_t index = 0; index < length; index++) {
        crc ^= bytes[index];
        crc = (crc >> 4) ^ table[crc & 15];
        crc = (crc >> 4) ^ table[crc & 15];
    }
    return crc;
}

static bool zeroes(const uint8_t *bytes, uint32_t length)
{
    for (uint32_t at = 0; at < length; at++) {
        if (bytes[at] != 0) {
            return false;
        }
    }
    return true;
}

static bool text_valid(const uint8_t *bytes, uint32_t length, bool required)
{
    uint32_t end = 0;
    while (end < length && bytes[end] != 0) {
        if (bytes[end] < 32 || bytes[end] == 127) {
            return false;
        }
        end++;
    }
    if (end == length || (required && end == 0) || !zeroes(bytes + end, length - end)) {
        return false;
    }
    const char *cursor = (const char *)bytes;
    while (*cursor != '\0') {
        if (utf8_next(&cursor) == UTF8_REPLACEMENT_CHARACTER) {
            return false;
        }
    }
    return true;
}

static bool url_valid(const uint8_t *bytes)
{
    if (!text_valid(bytes, 128, false)) {
        return false;
    }
    static const uint8_t prefix[] = {'h', 't', 't', 'p', 's', ':', '/', '/'};
    if (bytes[0] == 0) {
        return true;
    }
    for (unsigned at = 0; at < sizeof prefix; at++) {
        if (bytes[at] != prefix[at]) {
            return false;
        }
    }
    return bytes[sizeof prefix] != 0;
}

static bool version_valid(const uint8_t *bytes)
{
    for (int part = 0; part < 3; part++) {
        if (*bytes < '0' || *bytes > '9' || (*bytes == '0' && bytes[1] >= '0' && bytes[1] <= '9')) {
            return false;
        }
        unsigned value = 0;
        while (*bytes >= '0' && *bytes <= '9') {
            value = value * 10 + (unsigned)(*bytes++ - '0');
            if (value > 65535) {
                return false;
            }
        }
        if (*bytes++ != (part < 2 ? '.' : 0)) {
            return false;
        }
    }
    return true;
}

static bool header_valid(const uint8_t *bytes, uint32_t size)
{
    uint32_t style = store_get16(bytes + 494);
    return bytes[0] == 'D' && bytes[1] == 'S' && bytes[2] == 'T' && bytes[3] == 'P' &&
           store_get16(bytes + 4) == THEME_PACK_VERSION &&
           store_get16(bytes + 6) == THEME_PACK_HEADER_BYTES && store_get32(bytes + 8) == size &&
           bytes[16] < APPEARANCE_THEME_COUNT && bytes[17] < 4 && bytes[18] == 20 &&
           bytes[19] <= THEME_PACK_MAX_ASSETS && !zeroes(bytes + 20, 16) &&
           text_valid(bytes + 36, 32, true) && text_valid(bytes + 68, 32, true) &&
           text_valid(bytes + 100, 32, true) && version_valid(bytes + 100) &&
           text_valid(bytes + 132, 64, true) && url_valid(bytes + 196) && url_valid(bytes + 324) &&
           (store_get16(bytes + 492) == 0 ||
            store_get16(bytes + 492) == THEME_PACK_SIGNATURE_BYTES) &&
           style % 4 == 0 && style <= THEME_PACK_STYLE_MAX_BYTES && zeroes(bytes + 496, 16);
}

static bool crc_valid(ByteSource source, uint32_t size, uint32_t expected)
{
    uint8_t block[512];
    uint32_t crc = UINT32_MAX;
    for (uint32_t offset = 0; offset < size;) {
        uint32_t count = size - offset < sizeof block ? size - offset : sizeof block;
        if (!source.read(source.context, offset, block, count)) {
            return false;
        }
        if (offset == 0) {
            crc = theme_pack_crc(crc, block, 12);
            crc = theme_pack_crc(crc, block + 16, count - 16);
        } else {
            crc = theme_pack_crc(crc, block, count);
        }
        offset += count;
        hal_audio_service();
    }
    return ~crc == expected;
}

ThemeAsset theme_pack_asset(const uint8_t *bytes)
{
    return (ThemeAsset){.offset = store_get32(bytes + 8),
                        .bytes = store_get32(bytes + 12),
                        .slot = store_get16(bytes),
                        .width = store_get16(bytes + 4),
                        .height = store_get16(bytes + 6),
                        .entries = (uint16_t)(bytes[16] == ART_CODEC_INDEXED ? bytes[17] + 1 : 0),
                        .orientation = bytes[2],
                        .text_size = bytes[3],
                        .codec = bytes[16],
                        .alpha = bytes[18],
                        .flags = bytes[19],
                        .anchor = bytes[20],
                        .dx = bytes[21],
                        .dy = bytes[22],
                        .frames = bytes[23]};
}

uint32_t theme_pack_table(const ThemeAsset *asset)
{
    uint32_t nibbles = asset->codec == ART_CODEC_INDEXED ? (asset->alpha + 1u) / 2 : 0;
    return THEME_PACK_PAYLOAD_HEAD_BYTES + asset->entries * 2u + nibbles;
}

static bool codec_valid(const ThemeAsset *asset, const uint8_t *bytes, bool alpha)
{
    if (asset->codec == ART_CODEC_INDEXED) {
        return asset->alpha <= asset->entries && (alpha || asset->alpha == 0);
    }
    return asset->codec == ART_CODEC_LOSSLESS && bytes[17] == 0 && asset->alpha <= (alpha ? 1 : 0);
}

static bool full_screen(const ThemeAsset *asset)
{
    return asset->orientation < THEME_PACK_EITHER &&
           asset->width == (asset->orientation == 0 ? 480 : 222) &&
           asset->height == (asset->orientation == 0 ? 222 : 480);
}

static bool icon_valid(const ThemeAsset *asset)
{
    IconId icon = (IconId)(asset->slot - THEME_ART_ICON);
    int height = icon_rows(icon);
    return icon != ICON_NONE && asset->width == icon_width(icon, height) &&
           asset->height == height && asset->flags == 0;
}

static bool slot_valid(const ThemeAsset *asset)
{
    if (asset->slot >= THEME_ART_ICON && asset->slot < THEME_ART_ICON + ICON_COUNT) {
        return icon_valid(asset);
    }
    if (asset->text_size != THEME_PACK_ANY || asset->slot < THEME_ART_HOME ||
        asset->slot > THEME_ART_CASSETTE_LABEL ||
        (asset->flags != 0 && asset->slot != THEME_ART_LIST)) {
        return false;
    }
    if (asset->slot == THEME_ART_CASSETTE_LABEL) {
        return asset->width == 160 && asset->height == 48;
    }
    if (asset->flags == THEME_PACK_TILE) {
        return asset->width <= 64 && asset->height <= 64;
    }
    return full_screen(asset);
}

static bool descriptor_valid(const ThemeAsset *asset, const uint8_t *bytes)
{
    bool icon = asset->slot >= THEME_ART_ICON;
    return asset->orientation <= THEME_PACK_EITHER &&
           (asset->text_size <= 2 || asset->text_size == THEME_PACK_ANY) && asset->width > 0 &&
           asset->height > 0 && asset->flags <= THEME_PACK_TILE && asset->anchor == 0 &&
           asset->dx == 0 && asset->dy == 0 && asset->frames == 1 && slot_valid(asset) &&
           codec_valid(asset, bytes, icon) &&
           theme_pack_table(asset) + 4u * ((uint32_t)asset->height + 1) <= asset->bytes;
}

typedef struct ThemeLayout {
    uint32_t payload;
    uint32_t end;
    uint8_t count;
} ThemeLayout;

static bool read_asset(ByteSource source, unsigned index, ThemeAsset *asset)
{
    uint8_t bytes[THEME_PACK_ASSET_BYTES];
    if (!source.read(source.context, THEME_PACK_HEADER_BYTES + index * sizeof bytes, bytes,
                     sizeof bytes)) {
        return false;
    }
    *asset = theme_pack_asset(bytes);
    return descriptor_valid(asset, bytes);
}

static bool key_taken(const ThemePack *pack, unsigned count, ThemeAssetKey key)
{
    for (unsigned previous = 0; previous < count; previous++) {
        const ThemeAssetKey *other = &pack->assets[previous];
        uint16_t slot = other->slot == THEME_PACK_HIDDEN ? THEME_ART_NOW_PLAYING : other->slot;
        if (key.slot == slot && key.orientation == other->orientation &&
            key.text_size == other->text_size) {
            return true;
        }
    }
    return false;
}

static bool assets_valid(ThemePack *pack, ByteSource source, const ThemeLayout *layout)
{
    uint32_t next = layout->payload;
    for (unsigned at = 0; at < layout->count; at++) {
        ThemeAsset asset;
        if (!read_asset(source, at, &asset) || asset.offset != next ||
            asset.bytes > layout->end - next) {
            return false;
        }
        ThemeAssetKey key = {asset.slot, asset.orientation, asset.text_size};
        if (key_taken(pack, at, key)) {
            return false;
        }
        bool refused = asset.slot == THEME_ART_NOW_PLAYING && asset.codec == ART_CODEC_LOSSLESS;
        key.slot = refused ? THEME_PACK_HIDDEN : key.slot;
        pack->assets[at] = key;
        next += asset.bytes;
    }
    return next == layout->end;
}

static bool style_entry_valid(uint8_t kind, uint8_t target, uint32_t value)
{
    static const uint16_t SCENE_LOW[] = {40, 15, 0, 3, 0, 0};
    static const uint16_t SCENE_HIGH[] = {75, 35, 8, 10, 16, 1};
    if (kind == 1) {
        return target < 6 && value >= SCENE_LOW[target] && value <= SCENE_HIGH[target];
    }
    if (kind == 3) {
        return target == 0 && value >= 40 && value <= 500;
    }
    return kind == 4 && ((target == 0 && value <= 1) ||
                         (target == 1 && (value == 4 || value == 6 || value == 8)));
}

static bool style_valid(ByteSource source, uint32_t offset, uint32_t length)
{
    uint8_t bytes[THEME_PACK_STYLE_MAX_BYTES];
    if (!source.read(source.context, offset, bytes, length)) {
        return false;
    }
    for (uint32_t at = 0; at < length; at += 4) {
        if (!style_entry_valid(bytes[at], bytes[at + 1], store_get16(bytes + at + 2))) {
            return false;
        }
        for (uint32_t before = 0; before < at; before += 4) {
            if (bytes[before] == bytes[at] && bytes[before + 1] == bytes[at + 1]) {
                return false;
            }
        }
    }
    return true;
}

typedef struct ThemeRows {
    uint32_t table[17];
    uint8_t row[ART_ROW_MAX_BYTES];
} ThemeRows;

static bool head_valid(ByteSource source, const ThemeAsset *asset)
{
    uint8_t head[THEME_PACK_PAYLOAD_HEAD_BYTES + ART_PALETTE_MAX / 2];
    uint32_t nibbles = asset->codec == ART_CODEC_INDEXED ? (asset->alpha + 1u) / 2 : 0;
    if (!source.read(source.context, asset->offset, head, THEME_PACK_PAYLOAD_HEAD_BYTES) ||
        head[0] != asset->codec || head[1] != (asset->entries > 0 ? asset->entries - 1 : 0) ||
        head[2] != asset->alpha || head[3] != asset->frames ||
        !source.read(source.context, asset->offset + theme_pack_table(asset) - nibbles,
                     head + THEME_PACK_PAYLOAD_HEAD_BYTES, nibbles)) {
        return false;
    }
    for (uint32_t index = 0; index < (nibbles > 0 ? asset->alpha : 0u); index++) {
        if ((head[THEME_PACK_PAYLOAD_HEAD_BYTES + index / 2] >> (index % 2 * 4) & 15u) == 15u) {
            return false;
        }
    }
    return asset->alpha % 2 == 0 || nibbles == 0 ||
           (head[THEME_PACK_PAYLOAD_HEAD_BYTES + nibbles - 1] >> 4) == 0;
}

static bool rows_batch_valid(ByteSource source, const ThemeAsset *asset, ThemeRows *rows,
                             unsigned first)
{
    unsigned count = asset->height - first < 16 ? asset->height - first : 16;
    if (!source.read(source.context, asset->offset + theme_pack_table(asset) + first * 4u,
                     rows->table, (count + 1) * 4)) {
        return false;
    }
    uint32_t start = theme_pack_table(asset) + 4u * ((uint32_t)asset->height + 1);
    for (unsigned at = 0; at < count; at++) {
        uint32_t from = rows->table[at];
        uint32_t to = rows->table[at + 1];
        if ((first + at == 0 && from != start) || from < start || to <= from ||
            to - from > ART_ROW_MAX_BYTES || to > asset->bytes ||
            (first + at + 1 == asset->height && to != asset->bytes) ||
            !source.read(source.context, asset->offset + from, rows->row, to - from) ||
            !art_row_check((ArtRow){rows->row, rows->row + (to - from), asset->width}, asset->codec,
                           asset->entries, asset->alpha != 0)) {
            return false;
        }
    }
    hal_audio_service();
    return true;
}

static bool payloads_valid(ByteSource source, uint8_t count)
{
    ThemeRows rows;
    for (unsigned at = 0; at < count; at++) {
        ThemeAsset asset;
        if (!read_asset(source, at, &asset) || !head_valid(source, &asset)) {
            return false;
        }
        for (unsigned first = 0; first < asset.height; first += 16) {
            if (!rows_batch_valid(source, &asset, &rows, first)) {
                return false;
            }
        }
    }
    return true;
}

static unsigned luminance(Color color)
{
    unsigned red = ((color >> 11) & 31u) * 255 / 31;
    unsigned green = ((color >> 5) & 63u) * 255 / 63;
    unsigned blue = (color & 31u) * 255 / 31;
    return (red * red * 2126u + green * green * 7152u + blue * blue * 722u) / 10000u;
}

static bool low_contrast(Color text, Color background)
{
    unsigned first = luminance(text) + 3251;
    unsigned second = luminance(background) + 3251;
    return (first > second ? first : second) * 2 < (first < second ? first : second) * 9;
}

static void decode_palette(ThemePack *pack, const uint8_t *header)
{
    uint8_t bytes[THEME_PALETTE_BYTES] = {'D', 'S', 'T', 'H', 1, 0, 20, 0};
    bytes[5] = header[17];
    for (int at = 0; at < 40; at++) {
        bytes[8 + at] = header[452 + at];
    }
    hash_store(bytes + 48, hash_update(HASH_START, bytes, 48));
    theme_palette_decode(bytes, sizeof bytes, &pack->palette);
    const ThemeColors *colors = &pack->palette.colors;
    pack->low_contrast = low_contrast(colors->text, colors->background) ||
                         low_contrast(colors->text_secondary, colors->background) ||
                         low_contrast(colors->selection_text, colors->selection_background) ||
                         low_contrast(colors->header_text, colors->header_background);
}

static bool structure_valid(ThemePack *candidate, ByteSource source, const uint8_t *header,
                            uint32_t size)
{
    uint32_t style = THEME_PACK_HEADER_BYTES + header[19] * (uint32_t)THEME_PACK_ASSET_BYTES;
    ThemeLayout layout = {style + store_get16(header + 494), size - store_get16(header + 492),
                          header[19]};
    return layout.payload <= layout.end && style_valid(source, style, store_get16(header + 494)) &&
           assets_valid(candidate, source, &layout);
}

static bool complete_valid(ThemePack *candidate, ByteSource source, const uint8_t *header,
                           uint32_t size)
{
    uint32_t end = size - store_get16(header + 492);
    return crc_valid(source, end, store_get32(header + 12)) &&
           theme_signature_inspect(source, end, size - end, &candidate->verified) &&
           payloads_valid(source, header[19]);
}

bool theme_pack_open(ThemePack *pack, ByteSource source, uint32_t size, ThemePackCheck check)
{
    uint8_t header[THEME_PACK_HEADER_BYTES];
    if (source.read == NULL || size < sizeof header || size > THEME_PACK_MAX_BYTES ||
        !source.read(source.context, 0, header, sizeof header) || !header_valid(header, size)) {
        return false;
    }
    ThemePack candidate = {.base = header[16], .asset_count = header[19]};
    if (!structure_valid(&candidate, source, header, size) ||
        (check == THEME_PACK_COMPLETE && !complete_valid(&candidate, source, header, size))) {
        return false;
    }
    for (int at = 0; at < 16; at++) {
        candidate.id[at] = header[20 + at];
    }
    for (int at = 0; at < 32; at++) {
        candidate.name[at] = (char)header[36 + at];
        candidate.author[at] = (char)header[68 + at];
        candidate.version[at] = (char)header[100 + at];
    }
    decode_palette(&candidate, header);
    *pack = candidate;
    return true;
}
