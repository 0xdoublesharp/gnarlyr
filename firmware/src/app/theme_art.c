#include "theme_runtime.h"

#include <stddef.h>

#include "core/bytes.h"
#include "gfx/art_codec.h"
#include "ui/icons.h"

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

static bool size_fits(const uint8_t *bytes, uint16_t slot)
{
    uint32_t width = (uint32_t)(bytes[4] | bytes[5] << 8);
    uint32_t height = (uint32_t)(bytes[6] | bytes[7] << 8);
    bool portrait = bytes[2] == 1;
    if (slot >= THEME_ART_ICON) {
        return bytes[19] == 0 && width - 1 < ICON_LARGE_GRID && height - 1 < ICON_LARGE_GRID;
    }
    if (bytes[19] != 0) {
        return bytes[19] == THEME_PACK_TILE && slot == THEME_ART_LIST && width - 1 < 64 &&
               height - 1 < 64;
    }
    if (slot == THEME_ART_CASSETTE_LABEL) {
        return width == 160 && height == 48;
    }
    return bytes[2] < THEME_PACK_EITHER && width == (portrait ? 222u : 480u) &&
           height == (portrait ? 480u : 222u);
}

static bool descriptor_kept(const ThemeRuntime *runtime, int index, const uint8_t *bytes)
{
    const ThemeAssetKey *key = &runtime->pack.assets[index];
    uint16_t slot = (uint16_t)(bytes[0] | bytes[1] << 8);
    bool indexed = bytes[16] == ART_CODEC_INDEXED && bytes[18] <= bytes[17] + 1u;
    bool lossless = bytes[16] == ART_CODEC_LOSSLESS && bytes[17] == 0 && bytes[18] <= 1 &&
                    slot != THEME_ART_NOW_PLAYING;
    return slot == key->slot && bytes[2] == key->orientation && bytes[3] == key->text_size &&
           (indexed || lossless) && (bytes[18] == 0 || slot >= THEME_ART_ICON) && bytes[23] == 1 &&
           size_fits(bytes, slot);
}

static bool describe(ThemeRuntime *runtime, int index)
{
    uint8_t bytes[THEME_PACK_ASSET_BYTES];
    if (runtime->art.index == index) {
        return true;
    }
    if (!hal_file_read(runtime->file,
                       THEME_PACK_HEADER_BYTES + (uint32_t)index * THEME_PACK_ASSET_BYTES, bytes,
                       sizeof bytes) ||
        !descriptor_kept(runtime, index, bytes)) {
        return false;
    }
    uint32_t entries = bytes[16] == ART_CODEC_INDEXED ? bytes[17] + 1u : 0;
    uint32_t nibbles = entries > 0 ? (bytes[18] + 1u) / 2 : 0;
    ThemeArtCache *art = &runtime->art;
    art->offset = bytes_le32(bytes + 8);
    art->bytes = bytes_le32(bytes + 12);
    art->table = THEME_PACK_PAYLOAD_HEAD_BYTES + entries * 2 + nibbles;
    art->width = (uint16_t)(bytes[4] | bytes[5] << 8);
    art->height = (uint16_t)(bytes[6] | bytes[7] << 8);
    art->codec = bytes[16];
    art->alpha = bytes[18];
    art->index = (int8_t)index;
    art->palette_ready = false;
    uint32_t size = runtime->file.size;
    return art->offset <= size && art->bytes <= size - art->offset &&
           art->table + 4u * (art->height + 1u) <= art->bytes;
}

static bool load_palette(ThemeRuntime *runtime, ArtPalette *palette, uint8_t *scratch)
{
    ThemeArtCache *art = &runtime->art;
    uint32_t colors = art->table - THEME_PACK_PAYLOAD_HEAD_BYTES;
    uint32_t nibbles = art->codec == ART_CODEC_INDEXED ? (art->alpha + 1u) / 2 : 0;
    uint32_t color_bytes = colors - nibbles;
    palette->alpha_entries = art->alpha;
    palette->entries = color_bytes / 2;
    if (art->alpha == 0 && art->palette_ready) {
        __builtin_memcpy(palette->colors, art->palette, sizeof palette->colors);
        return true;
    }
    if (art->table > ART_WINDOW_BYTES ||
        !hal_file_read(runtime->file, art->offset, scratch, art->table) ||
        scratch[0] != art->codec ||
        scratch[1] != (palette->entries > 0 ? palette->entries - 1 : 0) ||
        scratch[2] != art->alpha || scratch[3] != 1) {
        return false;
    }
    __builtin_memset(palette->colors, 0, sizeof palette->colors);
    __builtin_memcpy(palette->colors, scratch + THEME_PACK_PAYLOAD_HEAD_BYTES, color_bytes);
    __builtin_memcpy(palette->alpha, scratch + THEME_PACK_PAYLOAD_HEAD_BYTES + color_bytes,
                     nibbles);
    if (art->alpha == 0) {
        __builtin_memcpy(art->palette, palette->colors, sizeof art->palette);
        art->palette_ready = true;
    }
    return true;
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

static bool fill(const ArtJob *job, int row, int wanted_rows)
{
    ArtWindow *window = job->window;
    const ThemeArtCache *asset = job->asset;
    int remaining = asset->height - row;
    int count = wanted_rows < remaining ? wanted_rows : remaining;
    if (count > ART_BATCH) {
        count = ART_BATCH;
    }
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
    uint32_t length = asset->offset + window->offsets[window->count] - window->base;
    return hal_file_read(job->runtime->file, window->base, window->bytes, length);
}

static bool draw_row(const ArtJob *job, int row, int wanted_rows, ArtSpan span)
{
    ArtWindow *window = job->window;
    if ((row < window->first || row >= window->first + window->count) &&
        !fill(job, row, wanted_rows)) {
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

static bool draw_line(const ArtJob *job, Canvas *canvas, Rect clip, Point origin, int wanted_rows)
{
    const ThemeArtCache *asset = job->asset;
    int row = (clip.y - origin.y) % asset->height;
    Color *pixels = canvas->pixels + (clip.y - canvas->bounds.y) * canvas->bounds.width;
    for (int x = clip.x; x < clip.x + clip.width;) {
        int from = (x - origin.x) % asset->width;
        int to = from + clip.x + clip.width - x;
        to = to < asset->width ? to : asset->width;
        if (!draw_row(job, row, wanted_rows,
                      (ArtSpan){pixels + (x - canvas->bounds.x), from, to})) {
            return false;
        }
        x += to - from;
    }
    return true;
}

static bool draw_asset(const ArtJob *job, Canvas *canvas, Rect clip, Point origin)
{
    for (int y = clip.y; y < clip.y + clip.height; y++) {
        int left = clip.y + clip.height - y;
        if (!draw_line(job, canvas, (Rect){clip.x, y, clip.width, 1}, origin, left)) {
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
    if (!describe(runtime, index)) {
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
    Rect clip = rect_intersection(canvas->clip, area);
    if (rect_is_empty(clip)) {
        return true;
    }
    if (!load_palette(runtime, &palette, window.bytes)) {
        theme_runtime_close(runtime);
        return false;
    }
    ArtJob job = {runtime, asset, &palette, &window};
    Point origin = placed ? (Point){area.x, area.y} : (Point){0, 0};
    if (!draw_asset(&job, canvas, clip, origin)) {
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
