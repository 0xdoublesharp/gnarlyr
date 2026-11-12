#include "theme_runtime.h"

#include <stddef.h>

static int find_key(const ThemeRuntime *runtime, ThemeArtRequest request)
{
    int best = -1;
    int score = -1;
    for (unsigned at = 0; at < runtime->pack.asset_count; at++) {
        const ThemeAssetKey *key = &runtime->pack.assets[at];
        if (key->slot != request.slot ||
            (key->orientation != 2 && key->orientation != request.orientation) ||
            (key->text_size != THEME_PACK_ANY && key->text_size != request.text_size)) {
            continue;
        }
        int next = (key->orientation == request.orientation ? 2 : 0) +
                   (key->text_size == request.text_size ? 1 : 0);
        if (next > score) {
            best = (int)at;
            score = next;
        }
    }
    return best;
}

static uint32_t key_bytes(const ThemeAssetKey *key)
{
    return (uint32_t)key->width * key->height * 2;
}

static ThemeAsset asset_at(const ThemeRuntime *runtime, unsigned index)
{
    const ThemeAssetKey *keys = runtime->pack.assets;
    uint32_t offset =
        THEME_PACK_HEADER_BYTES + (uint32_t)runtime->pack.asset_count * THEME_PACK_ASSET_BYTES;
    for (unsigned at = 0; at < index; at++) {
        offset += key_bytes(&keys[at]);
    }
    const ThemeAssetKey *key = &keys[index];
    return (ThemeAsset){key->slot,   key->orientation, key->text_size, key->width,
                        key->height, offset,           key_bytes(key)};
}

static Rect clip_of(Rect first, Rect second)
{
    int left = first.x > second.x ? first.x : second.x;
    int top = first.y > second.y ? first.y : second.y;
    int right = first.x + first.width < second.x + second.width ? first.x + first.width
                                                                : second.x + second.width;
    int bottom = first.y + first.height < second.y + second.height ? first.y + first.height
                                                                   : second.y + second.height;
    return (Rect){left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0};
}

typedef struct ThemeBlit {
    Canvas *canvas;
    const ThemeAsset *asset;
    Rect clip;
    Point origin;
} ThemeBlit;

static bool read_row(ThemeRuntime *runtime, const ThemeBlit *blit, int y)
{
    Canvas *canvas = blit->canvas;
    const ThemeAsset *asset = blit->asset;
    int source_y = (y - blit->origin.y) % asset->height;
    for (int x = blit->clip.x; x < blit->clip.x + blit->clip.width;) {
        int source_x = (x - blit->origin.x) % asset->width;
        int count = asset->width - source_x;
        int room = blit->clip.x + blit->clip.width - x;
        count = count < room ? count : room;
        uint32_t offset =
            asset->offset + ((uint32_t)source_y * asset->width + (uint32_t)source_x) * 2;
        Color *pixels =
            canvas->pixels + (y - canvas->bounds.y) * canvas->bounds.width + x - canvas->bounds.x;
        if (runtime->tile_valid && runtime->tile_offset == asset->offset) {
            const Color *source =
                runtime->tile + (unsigned)source_y * asset->width + (unsigned)source_x;
            for (int at = 0; at < count; at++) {
                pixels[at] = source[at];
            }
        } else if (!hal_file_read(runtime->file, offset, pixels, (uint32_t)count * 2)) {
            return false;
        }
        x += count;
    }
    return true;
}

bool theme_runtime_draw(void *context, Canvas *canvas, ThemeArtRequest request)
{
    ThemeRuntime *runtime = context;
    int index = runtime->active ? find_key(runtime, request) : -1;
    if (index < 0) {
        return false;
    }
    ThemeAsset found = asset_at(runtime, (unsigned)index);
    const ThemeAsset *asset = &found;
    if (asset->bytes <= sizeof runtime->tile &&
        (!runtime->tile_valid || runtime->tile_offset != asset->offset)) {
        if (!hal_file_read(runtime->file, asset->offset, runtime->tile, asset->bytes)) {
            theme_runtime_close(runtime);
            return false;
        }
        runtime->tile_offset = asset->offset;
        runtime->tile_valid = true;
    }
    bool placed = request.slot >= THEME_ART_ICON || request.slot == THEME_ART_CASSETTE_LABEL;
    Rect area =
        placed ? (Rect){request.area.x, request.area.y, asset->width, asset->height} : request.area;
    if (placed && (request.area.width < asset->width || request.area.height < asset->height)) {
        return false;
    }
    Rect clip = clip_of(canvas->clip, area);
    if (clip.width == 0 || clip.height == 0) {
        return true;
    }
    ThemeBlit blit = {canvas, asset, clip, placed ? (Point){area.x, area.y} : (Point){0, 0}};
    if (clip.width == asset->width && canvas->bounds.width == asset->width && clip.x == 0 &&
        clip.y >= 0 && clip.y + clip.height <= asset->height && !placed) {
        Color *pixels = canvas->pixels + (clip.y - canvas->bounds.y) * canvas->bounds.width;
        if (hal_file_read(runtime->file, asset->offset + (uint32_t)clip.y * asset->width * 2,
                          pixels, (uint32_t)clip.width * (uint32_t)clip.height * 2)) {
            return true;
        }
        theme_runtime_close(runtime);
        return false;
    }
    for (int y = clip.y; y < clip.y + clip.height; y++) {
        if (!read_row(runtime, &blit, y)) {
            theme_runtime_close(runtime);
            return false;
        }
    }
    return true;
}

bool theme_art_module_draw(const Theme *theme, Canvas *canvas, int slot, Rect area)
{
    ThemeArtRequest request = {.area = area,
                               .slot = (uint16_t)slot,
                               .orientation = theme_is_portrait(theme) ? 1 : 0,
                               .text_size = (uint8_t)theme->metrics.text_size};
    return theme_runtime_draw(theme->art_context, canvas, request);
}
