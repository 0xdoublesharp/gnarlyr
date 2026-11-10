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

static uint32_t row_hash(const Color *row, int width)
{
    uint32_t hash = HASH_START;
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
    return hash == UNKNOWN ? REPLACEMENT : hash;
}

static bool row_changed(FrameDiff *diff, int y, const Color *row, int width)
{
    if (y < 0 || y >= diff->rows) {
        return true;
    }
    uint32_t hash = row_hash(row, width);
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
