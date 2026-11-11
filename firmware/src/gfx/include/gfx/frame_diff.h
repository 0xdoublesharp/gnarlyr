#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"

typedef struct FrameDiff {
    uint32_t *hashes;
    int rows;
    Rect masked;
} FrameDiff;

void frame_diff_init(FrameDiff *diff, uint32_t *hashes, int rows);
void frame_diff_forget(FrameDiff *diff);
void frame_diff_mask(FrameDiff *diff, Rect area);
bool frame_diff_changed(FrameDiff *diff, Rect strip, const Color *pixels, Rect *changed);
