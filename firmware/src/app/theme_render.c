#include "theme_runtime.h"

#include <stddef.h>

const char THEME_SELECTION_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/theme.bin";

void theme_runtime_close(ThemeRuntime *runtime)
{
    if (runtime->active) {
        hal_file_close(&runtime->file);
    }
    runtime->active = false;
    runtime->tile_valid = false;
}

void theme_runtime_apply(ThemeRuntime *runtime, Theme *theme)
{
    if (runtime->active) {
        theme->appearance = UINT8_MAX;
        theme->colors = runtime->pack.palette.colors;
        theme->horizon = runtime->pack.palette.horizon;
        theme->header_rule = runtime->pack.palette.header_rule;
    }
    theme->art_context = runtime;
    theme->draw_art = theme_runtime_draw;
}

static const ThemeAsset *find_asset(const ThemeRuntime *runtime, ThemeArtRequest request)
{
    const ThemeAsset *best = NULL;
    int score = -1;
    for (unsigned at = 0; at < runtime->pack.asset_count; at++) {
        const ThemeAsset *asset = &runtime->pack.assets[at];
        if (asset->slot != request.slot ||
            (asset->orientation != 2 && asset->orientation != request.orientation) ||
            (asset->text_size != THEME_PACK_ANY && asset->text_size != request.text_size)) {
            continue;
        }
        int next = (asset->orientation == request.orientation ? 2 : 0) +
                   (asset->text_size == request.text_size ? 1 : 0);
        if (next > score) {
            best = asset;
            score = next;
        }
    }
    return best;
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
    const ThemeAsset *asset = runtime->active ? find_asset(runtime, request) : NULL;
    if (asset == NULL) {
        return false;
    }
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
    Rect clip = rect_intersection(canvas->clip, area);
    if (rect_is_empty(clip)) {
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
