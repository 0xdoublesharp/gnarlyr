#include "theme_runtime.h"

#include <stddef.h>

#include "gfx/art_codec.h"

enum {
    ART_BATCH = 32,
    ART_WINDOW_BYTES = 4608,
    ART_SECTOR_BYTES = 512
};

typedef struct ArtWindow {
    uint32_t offsets[ART_BATCH + 1];
    uint8_t bytes[ART_WINDOW_BYTES];
    uint32_t base;
    int first;
    int count;
} ArtWindow;

typedef struct ArtJob {
    ThemeRuntime *runtime;
    const ThemeArtCache *asset;
    const ArtPalette *palette;
    ArtWindow *window;
} ArtJob;

static int find_key(const ThemeRuntime *runtime, ThemeArtRequest request)
{
    int best = -1;
    int score = -1;
    for (unsigned at = 0; at < runtime->pack.asset_count; at++) {
        const ThemeAssetKey *key = &runtime->pack.assets[at];
        if (key->slot != request.slot ||
            (key->orientation != THEME_PACK_EITHER && key->orientation != request.orientation) ||
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

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool describe(ThemeRuntime *runtime, int index)
{
    uint8_t bytes[THEME_PACK_ASSET_BYTES];
    if (runtime->art.index == index) {
        return true;
    }
    if (!hal_file_read(runtime->file,
                       THEME_PACK_HEADER_BYTES + (uint32_t)index * THEME_PACK_ASSET_BYTES, bytes,
                       sizeof bytes)) {
        return false;
    }
    uint32_t entries = bytes[16] == ART_CODEC_INDEXED ? bytes[17] + 1u : 0;
    uint32_t nibbles = entries > 0 ? (bytes[18] + 1u) / 2 : 0;
    runtime->art = (ThemeArtCache){get32(bytes + 8),
                                   get32(bytes + 12),
                                   THEME_PACK_PAYLOAD_HEAD_BYTES + entries * 2 + nibbles,
                                   (uint16_t)(bytes[4] | bytes[5] << 8),
                                   (uint16_t)(bytes[6] | bytes[7] << 8),
                                   bytes[16],
                                   bytes[18],
                                   (int8_t)index};
    return runtime->art.width > 0 && runtime->art.height > 0;
}

static bool load_palette(ThemeRuntime *runtime, ArtPalette *palette)
{
    const ThemeArtCache *art = &runtime->art;
    palette->alpha_entries = art->alpha;
    uint32_t colors = art->table - THEME_PACK_PAYLOAD_HEAD_BYTES;
    uint32_t nibbles = art->codec == ART_CODEC_INDEXED ? (art->alpha + 1u) / 2 : 0;
    uint32_t start = art->offset + THEME_PACK_PAYLOAD_HEAD_BYTES;
    return colors == 0 ||
           (hal_file_read(runtime->file, start, palette->colors, colors - nibbles) &&
            hal_file_read(runtime->file, start + colors - nibbles, palette->alpha, nibbles));
}

static int rows_that_fit(const ArtWindow *window, const ThemeArtCache *asset, int count)
{
    int fit = 0;
    while (fit < count && window->offsets[fit + 1] > window->offsets[fit] &&
           window->offsets[fit + 1] <= asset->bytes &&
           asset->offset + window->offsets[fit + 1] - window->base <= ART_WINDOW_BYTES) {
        fit++;
    }
    return fit;
}

static bool fill(const ArtJob *job, int row)
{
    ArtWindow *window = job->window;
    const ThemeArtCache *asset = job->asset;
    int count = asset->height - row < ART_BATCH ? asset->height - row : ART_BATCH;
    if (!hal_file_read(job->runtime->file, asset->offset + asset->table + (uint32_t)row * 4,
                       window->offsets, (uint32_t)(count + 1) * 4) ||
        window->offsets[0] >= asset->bytes) {
        return false;
    }
    window->base = (asset->offset + window->offsets[0]) / ART_SECTOR_BYTES * ART_SECTOR_BYTES;
    window->first = row;
    window->count = rows_that_fit(window, asset, count);
    if (window->count == 0) {
        return false;
    }
    uint32_t end = asset->offset + window->offsets[window->count];
    uint32_t last = asset->offset + asset->bytes;
    uint32_t wanted =
        last - window->base < ART_WINDOW_BYTES ? last - window->base : ART_WINDOW_BYTES;
    uint32_t length =
        end - window->base > ART_WINDOW_BYTES - ART_SECTOR_BYTES ? wanted : end - window->base;
    return hal_file_read(job->runtime->file, window->base, window->bytes, length);
}

static bool draw_row(const ArtJob *job, int row, ArtSpan span)
{
    ArtWindow *window = job->window;
    if ((row < window->first || row >= window->first + window->count) && !fill(job, row)) {
        return false;
    }
    int at = row - window->first;
    uint32_t start = job->asset->offset + window->offsets[at] - window->base;
    uint32_t end = job->asset->offset + window->offsets[at + 1] - window->base;
    ArtRow source = {window->bytes + start, window->bytes + end, job->asset->width};
    return job->asset->codec == ART_CODEC_INDEXED
               ? art_row_indexed(&source, job->palette, span)
               : art_row_lossless(&source, job->palette->alpha_entries != 0, span);
}

static bool draw_line(const ArtJob *job, Canvas *canvas, Rect clip, Point origin)
{
    const ThemeArtCache *asset = job->asset;
    int row = (clip.y - origin.y) % asset->height;
    Color *pixels =
        canvas->pixels + (clip.y - canvas->bounds.y) * canvas->bounds.width - canvas->bounds.x;
    for (int x = clip.x; x < clip.x + clip.width;) {
        int from = (x - origin.x) % asset->width;
        int to = from + clip.x + clip.width - x;
        to = to < asset->width ? to : asset->width;
        if (!draw_row(job, row, (ArtSpan){pixels + x, from, to})) {
            return false;
        }
        x += to - from;
    }
    return true;
}

static bool draw_asset(const ArtJob *job, Canvas *canvas, Rect area, bool placed)
{
    Rect clip = rect_intersection(canvas->clip, area);
    Point origin = placed ? (Point){area.x, area.y} : (Point){0, 0};
    for (int y = clip.y; y < clip.y + clip.height; y++) {
        if (!draw_line(job, canvas, (Rect){clip.x, y, clip.width, 1}, origin)) {
            return false;
        }
    }
    return true;
}

bool theme_runtime_draw(void *context, Canvas *canvas, ThemeArtRequest request)
{
    ThemeRuntime *runtime = context;
    int index = runtime->active ? find_key(runtime, request) : -1;
    ArtPalette palette;
    ArtWindow window = {.count = 0};
    if (index < 0) {
        return false;
    }
    if (!describe(runtime, index) || !load_palette(runtime, &palette)) {
        theme_runtime_close(runtime);
        return false;
    }
    const ThemeArtCache *asset = &runtime->art;
    bool placed = request.slot >= THEME_ART_ICON || request.slot == THEME_ART_CASSETTE_LABEL;
    if (placed && (request.area.width < asset->width || request.area.height < asset->height)) {
        return false;
    }
    Rect area =
        placed ? (Rect){request.area.x, request.area.y, asset->width, asset->height} : request.area;
    ArtJob job = {runtime, asset, &palette, &window};
    if (!draw_asset(&job, canvas, area, placed)) {
        theme_runtime_close(runtime);
        return false;
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
