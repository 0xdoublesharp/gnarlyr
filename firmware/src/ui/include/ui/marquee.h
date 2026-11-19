#pragma once

#include <stdint.h>

#include "gfx/rect.h"

enum {
    MARQUEE_STEP_MS = 100
};

#define MARQUEE_STILL UINT32_MAX

typedef struct MarqueeTiming {
    uint32_t pause_ms;
    uint32_t speed_px_per_s;
    int gap_px;
} MarqueeTiming;

int marquee_offset(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms);
uint32_t marquee_wait_ms(MarqueeTiming timing, int text_width, int view_width, uint32_t elapsed_ms);
void marquee_watch_start(void);
uint32_t marquee_watch_wait_ms(void);
int marquee_offset_in(MarqueeTiming timing, int text_width, Rect area, uint32_t elapsed_ms);
Rect marquee_watch_band(void);
