#include "gfx/frame_diff.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    UNKNOWN = 0,
    REPLACEMENT = 1,
    PIXELS_PER_ROUND = 8
};

void frame_diff_init(FrameDiff *diff, uint32_t *hashes, int rows)
{
    *diff = (FrameDiff){.hashes = hashes, .rows = rows};
    frame_diff_forget(diff);
}

void frame_diff_forget(FrameDiff *diff)
{
    for (int row = 0; row < diff->rows; row++) {
        diff->hashes[row] = UNKNOWN;
    }
}

static uint32_t pair_at(const Color *row)
{
    uint32_t pair;
    __builtin_memcpy(&pair, row, sizeof pair);
    return pair;
}

void frame_diff_mask(FrameDiff *diff, Rect area)
{
    diff->masked = area;
    frame_diff_forget(diff);
}

static uint32_t span_hash(uint32_t hash, const Color *row, int width)
{
    int index = 0;
    for (; index + PIXELS_PER_ROUND <= width; index += PIXELS_PER_ROUND) {
        hash = (hash ^ pair_at(row + index)) * HASH_PRIME;
        hash = (hash ^ pair_at(row + index + 2)) * HASH_PRIME;
        hash = (hash ^ pair_at(row + index + 4)) * HASH_PRIME;
        hash = (hash ^ pair_at(row + index + 6)) * HASH_PRIME;
    }
    for (; index + 1 < width; index += 2) {
        hash = (hash ^ pair_at(row + index)) * HASH_PRIME;
    }
    if (index < width) {
        hash = (hash ^ row[index]) * HASH_PRIME;
    }
    return hash;
}

static bool row_masked(const FrameDiff *diff, int y)
{
    Rect masked = diff->masked;
    return !rect_is_empty(masked) && y >= masked.y && y < masked.y + masked.height;
}

static uint32_t row_hash(const FrameDiff *diff, int y, const Color *row, int width)
{
    uint32_t hash = HASH_START;
    if (row_masked(diff, y)) {
        Rect masked = diff->masked;
        int right = masked.x + masked.width;
        hash = span_hash(hash, row, masked.x);
        hash = span_hash(hash, row + right, width - right);
    } else {
        hash = span_hash(hash, row, width);
    }
    return hash == UNKNOWN ? REPLACEMENT : hash;
}

static bool row_changed(FrameDiff *diff, int y, const Color *row, int width)
{
    if (y < 0 || y >= diff->rows) {
        return true;
    }
    uint32_t hash = row_hash(diff, y, row, width);
    bool changed = diff->hashes[y] != hash;
    diff->hashes[y] = hash;
    return changed;
}

bool frame_diff_changed(FrameDiff *diff, Rect strip, const Color *pixels, Rect *changed)
{
    int first = -1;
    int last = -1;
    for (int row = 0; row < strip.height; row++) {
        if (row_changed(diff, strip.y + row, pixels + (size_t)row * (size_t)strip.width,
                        strip.width)) {
            first = first < 0 ? row : first;
            last = row;
        }
    }
    if (first < 0) {
        return false;
    }
    *changed = (Rect){strip.x, strip.y + first, strip.width, last - first + 1};
    return true;
}
