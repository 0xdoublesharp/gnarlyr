#pragma once

#include "hal/storage.h"
#include "library/byte_source.h"
#include "theme_palette.h"

enum {
    THEME_PACK_HEADER_BYTES = 512,
    THEME_PACK_ASSET_BYTES = 16,
    THEME_PACK_MAX_ASSETS = 32,
    THEME_PACK_MAX_BYTES = 4194304,
    THEME_PACK_SIGNATURE_BYTES = 88,
    THEME_PACK_NAME_BYTES = 32,
    THEME_PACK_FILENAME_BYTES = 64,
    THEME_PACK_MAX_INSTALLED = 12,
    THEME_PACK_ANY = 255
};

typedef struct ThemeAsset {
    uint16_t slot;
    uint8_t orientation;
    uint8_t text_size;
    uint16_t width;
    uint16_t height;
    uint32_t offset;
    uint32_t bytes;
} ThemeAsset;

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
    ThemeAsset assets[THEME_PACK_MAX_ASSETS];
} ThemePack;

bool theme_pack_open(ThemePack *pack, ByteSource source, uint32_t size);
uint32_t theme_pack_crc(uint32_t crc, const uint8_t *bytes, uint32_t length);
