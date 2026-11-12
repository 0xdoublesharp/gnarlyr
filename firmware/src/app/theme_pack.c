#include "theme_pack.h"

#include <stddef.h>

#include "core/hash.h"
#include "core/utf8.h"
#include "hal/audio.h"
#include "playback/store.h"
#include "settings/settings.h"
#include "theme_signature.h"
#include "ui/icons.h"

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
    return bytes[0] == 'D' && bytes[1] == 'S' && bytes[2] == 'T' && bytes[3] == 'P' &&
           store_get16(bytes + 4) == 1 && store_get16(bytes + 6) == THEME_PACK_HEADER_BYTES &&
           store_get32(bytes + 8) == size && bytes[16] < APPEARANCE_THEME_COUNT && bytes[17] < 4 &&
           bytes[18] == 20 && bytes[19] <= THEME_PACK_MAX_ASSETS && !zeroes(bytes + 20, 16) &&
           text_valid(bytes + 36, 32, true) && text_valid(bytes + 68, 32, true) &&
           text_valid(bytes + 100, 32, true) && version_valid(bytes + 100) &&
           text_valid(bytes + 132, 64, true) && url_valid(bytes + 196) && url_valid(bytes + 324) &&
           (store_get16(bytes + 492) == 0 ||
            store_get16(bytes + 492) == THEME_PACK_SIGNATURE_BYTES) &&
           zeroes(bytes + 494, 18);
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

static ThemeAsset decode_asset(const uint8_t *bytes)
{
    return (ThemeAsset){store_get16(bytes),
                        bytes[2],
                        bytes[3],
                        store_get16(bytes + 4),
                        store_get16(bytes + 6),
                        store_get32(bytes + 8),
                        store_get32(bytes + 12)};
}

static bool dimensions_valid(const ThemeAsset *asset)
{
    if (asset->orientation > 2 || (asset->text_size > 2 && asset->text_size != THEME_PACK_ANY) ||
        asset->width == 0 || asset->height == 0) {
        return false;
    }
    if (asset->slot >= 256 && asset->slot < 256 + ICON_COUNT) {
        IconId icon = (IconId)(asset->slot - 256);
        int height = icon_rows(icon);
        return icon != ICON_NONE && asset->width == icon_width(icon, height) &&
               asset->height == height;
    }
    if (asset->text_size != THEME_PACK_ANY || asset->slot < 1 || asset->slot > 5) {
        return false;
    }
    if (asset->slot == 5) {
        return asset->width == 160 && asset->height == 48;
    }
    if (asset->slot == 3 && asset->width <= 64 && asset->height <= 64) {
        return true;
    }
    return asset->orientation < 2 && asset->width == (asset->orientation == 0 ? 480 : 222) &&
           asset->height == (asset->orientation == 0 ? 222 : 480);
}

static bool assets_valid(ThemePack *pack, ByteSource source, uint32_t end)
{
    uint32_t next = THEME_PACK_HEADER_BYTES + (uint32_t)pack->asset_count * THEME_PACK_ASSET_BYTES;
    for (unsigned at = 0; at < pack->asset_count; at++) {
        uint8_t bytes[THEME_PACK_ASSET_BYTES];
        if (!source.read(source.context, THEME_PACK_HEADER_BYTES + at * sizeof bytes, bytes,
                         sizeof bytes)) {
            return false;
        }
        ThemeAsset asset = decode_asset(bytes);
        if (!dimensions_valid(&asset) || asset.offset != next || asset.offset > end ||
            asset.bytes != (uint32_t)asset.width * asset.height * 2 ||
            asset.bytes > end - asset.offset) {
            return false;
        }
        for (unsigned previous = 0; previous < at; previous++) {
            const ThemeAsset *other = &pack->assets[previous];
            if (asset.slot == other->slot && asset.orientation == other->orientation &&
                asset.text_size == other->text_size) {
                return false;
            }
        }
        pack->assets[at] = asset;
        next += asset.bytes;
    }
    return next == end;
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

bool theme_pack_open(ThemePack *pack, ByteSource source, uint32_t size)
{
    uint8_t header[THEME_PACK_HEADER_BYTES];
    if (source.read == NULL || size < sizeof header || size > THEME_PACK_MAX_BYTES ||
        !source.read(source.context, 0, header, sizeof header) || !header_valid(header, size)) {
        return false;
    }
    uint32_t end = size - store_get16(header + 492);
    ThemePack candidate = {.base = header[16], .asset_count = header[19]};
    if (!assets_valid(&candidate, source, end) ||
        !crc_valid(source, end, store_get32(header + 12)) ||
        !theme_signature_inspect(source, end, size - end, &candidate.verified)) {
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
