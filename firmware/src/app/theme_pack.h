#pragma once

#include "hal/storage.h"
#include "library/byte_source.h"
#include "theme_palette.h"

enum {
    THEME_PACK_VERSION = 2,
    THEME_PACK_HEADER_BYTES = 512,
    THEME_PACK_ASSET_BYTES = 24,
    THEME_PACK_MAX_ASSETS = 64,
    THEME_PACK_MAX_BYTES = 2097152,
    THEME_PACK_STYLE_MAX_BYTES = 256,
    THEME_PACK_SIGNATURE_BYTES = 88,
    THEME_PACK_NAME_BYTES = 32,
    THEME_PACK_FILENAME_BYTES = 64,
    THEME_PACK_MAX_INSTALLED = 12,
    THEME_PACK_ANY = 255,
    THEME_PACK_HIDDEN = 0,
    THEME_PACK_EITHER = 2,
    THEME_PACK_TILE = 1,
    THEME_PACK_PAYLOAD_HEAD_BYTES = 4
};

typedef enum ThemePackCheck {
    THEME_PACK_STRUCTURE,
    THEME_PACK_COMPLETE
} ThemePackCheck;

typedef struct ThemeAsset {
    uint32_t offset;
    uint32_t bytes;
    uint16_t slot;
    uint16_t width;
    uint16_t height;
    uint16_t entries;
    uint8_t orientation;
    uint8_t text_size;
    uint8_t codec;
    uint8_t alpha;
    uint8_t flags;
    uint8_t anchor;
    uint8_t dx;
    uint8_t dy;
    uint8_t frames;
} ThemeAsset;

typedef struct ThemeAssetKey {
    uint16_t slot;
    uint8_t orientation;
    uint8_t text_size;
} ThemeAssetKey;

typedef struct ThemePack {
    ThemePalette palette;
    uint8_t id[16];
    char name[THEME_PACK_NAME_BYTES];
    char author[THEME_PACK_NAME_BYTES];
    char version[32];
    uint8_t base;
    uint8_t asset_count;
    bool low_contrast;
    bool verified;
    ThemeAssetKey assets[THEME_PACK_MAX_ASSETS];
    ThemeVisualStyle visual_style;
} ThemePack;

bool theme_pack_open(ThemePack *pack, ByteSource source, uint32_t size, ThemePackCheck check);
uint32_t theme_pack_crc(uint32_t crc, const uint8_t *bytes, uint32_t length);
ThemeAsset theme_pack_asset(const uint8_t *bytes);
uint32_t theme_pack_table(const ThemeAsset *asset);
