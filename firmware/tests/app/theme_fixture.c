#include "theme_fixture.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/art_codec.h"
#include "playback/store.h"
#include "theme_pack.h"

typedef struct Writer {
    uint8_t *bytes;
    uint32_t length;
    uint32_t capacity;
} Writer;

static void put(Writer *writer, const void *data, uint32_t count)
{
    if (writer->length + count > writer->capacity) {
        abort();
    }
    memcpy(writer->bytes + writer->length, data, count);
    writer->length += count;
}

static void put_byte(Writer *writer, uint8_t value)
{
    put(writer, &value, 1);
}

static void header(Writer *writer, const ThemeFixture *fixture)
{
    uint8_t head[THEME_PACK_HEADER_BYTES] = {'D', 'S', 'T', 'P'};
    store_put16(head + 4, THEME_PACK_VERSION);
    store_put16(head + 6, THEME_PACK_HEADER_BYTES);
    head[16] = fixture->base;
    head[18] = 20;
    head[19] = fixture->count;
    head[20] = fixture->id;
    const char *name = fixture->id == 1 ? "First" : (fixture->id == 2 ? "Second" : "Other");
    memcpy(head + 36, name, strlen(name));
    memcpy(head + 68, "Maker", 6);
    memcpy(head + 100, "1.0.0", 6);
    memcpy(head + 132, "CC0-1.0", 8);
    for (int at = 0; at < 20; at++) {
        store_put16(head + 452 + at * 2, at == 0 ? 0 : UINT16_MAX);
    }
    store_put16(head + 494, fixture->style_bytes);
    put(writer, head, sizeof head);
}

static uint32_t palette_of(const ThemeFixture *fixture, int index, Color *colors)
{
    const ThemeFixtureArt *art = &fixture->art[index];
    uint32_t count = 0;
    for (int y = 0; y < art->height; y++) {
        for (int x = 0; x < art->width; x++) {
            Color color = fixture->pixel(x, y, index);
            uint32_t at = 0;
            while (at < count && colors[at] != color) {
                at++;
            }
            if (at == count) {
                if (count == ART_PALETTE_MAX) {
                    abort();
                }
                colors[count++] = color;
            }
        }
    }
    return count;
}

static uint8_t index_of(const Color *colors, uint32_t count, Color color)
{
    for (uint32_t at = 0; at < count; at++) {
        if (colors[at] == color) {
            return (uint8_t)at;
        }
    }
    abort();
}

static void row(Writer *writer, const ThemeFixture *fixture, int index, int y)
{
    const ThemeFixtureArt *art = &fixture->art[index];
    static Color colors[ART_PALETTE_MAX];
    uint32_t count = art->codec == ART_CODEC_INDEXED ? palette_of(fixture, index, colors) : 0;
    int limit = art->codec == ART_CODEC_INDEXED ? 128 : 64;
    for (int x = 0; x < art->width;) {
        int run = art->width - x < limit ? art->width - x : limit;
        put_byte(writer, (uint8_t)((art->codec == ART_CODEC_INDEXED ? 0x00 : 0x80) | (run - 1)));
        for (int at = x; at < x + run; at++) {
            Color color = fixture->pixel(at, y, index);
            if (art->codec == ART_CODEC_INDEXED) {
                put_byte(writer, index_of(colors, count, color));
            } else {
                put_byte(writer, (uint8_t)color);
                put_byte(writer, (uint8_t)(color >> 8));
            }
        }
        x += run;
    }
}

static void payload(Writer *writer, const ThemeFixture *fixture, int index, uint8_t *descriptor)
{
    const ThemeFixtureArt *art = &fixture->art[index];
    static Color colors[ART_PALETTE_MAX];
    uint32_t start = writer->length;
    uint32_t count = art->codec == ART_CODEC_INDEXED ? palette_of(fixture, index, colors) : 0;
    uint8_t head[4] = {art->codec, (uint8_t)(count > 0 ? count - 1 : 0), 0, 1};
    put(writer, head, sizeof head);
    for (uint32_t at = 0; at < count; at++) {
        uint8_t color[2] = {(uint8_t)colors[at], (uint8_t)(colors[at] >> 8)};
        put(writer, color, 2);
    }
    uint32_t table = writer->length;
    writer->length += 4u * (art->height + 1u);
    for (int y = 0; y <= art->height; y++) {
        store_put32(writer->bytes + table + 4u * (uint32_t)y, writer->length - start);
        if (y < art->height) {
            row(writer, fixture, index, y);
        }
    }
    store_put32(writer->bytes + table + 4u * art->height, writer->length - start);
    store_put16(descriptor, art->slot);
    descriptor[2] = art->orientation;
    descriptor[3] = art->text_size;
    store_put16(descriptor + 4, art->width);
    store_put16(descriptor + 6, art->height);
    store_put32(descriptor + 8, start);
    store_put32(descriptor + 12, writer->length - start);
    descriptor[16] = art->codec;
    descriptor[17] = head[1];
    descriptor[19] = art->flags;
    descriptor[23] = 1;
}

void theme_fixture_seal(uint8_t *bytes, uint32_t length)
{
    uint32_t signature = store_get16(bytes + 492);
    uint32_t end = signature < length - 16 ? length - signature : length;
    store_put32(bytes + 8, length);
    uint32_t crc = theme_pack_crc(UINT32_MAX, bytes, 12);
    store_put32(bytes + 12, ~theme_pack_crc(crc, bytes + 16, end - 16));
}

uint32_t theme_fixture_build(uint8_t *bytes, uint32_t capacity, const ThemeFixture *fixture)
{
    Writer writer = {bytes, 0, capacity};
    memset(bytes, 0, capacity);
    header(&writer, fixture);
    uint32_t directory = writer.length;
    writer.length += (uint32_t)fixture->count * THEME_PACK_ASSET_BYTES;
    if (fixture->style_bytes > 0) {
        put(&writer, fixture->style, fixture->style_bytes);
    }
    for (int index = 0; index < fixture->count; index++) {
        payload(&writer, fixture, index,
                bytes + directory + (uint32_t)index * THEME_PACK_ASSET_BYTES);
    }
    theme_fixture_seal(bytes, writer.length);
    return writer.length;
}
