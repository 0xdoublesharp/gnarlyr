#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/theme.h"
#include "ui/wire_figure.h"

enum {
    WIRE_MOTION_DELAY_MS = 300,
    WIRE_MOTION_FRAME_MS = 72,
    WIRE_MOTION_TURN_MS = 16,
    WIRE_MOTION_MAX_SIDE = 104,
    WIRE_MOTION_MIN_SIDE = 40,
    WIRE_MOTION_ENDLESS = WIRE_FIGURE_ENDLESS,
    WIRE_SHAPE_COUNT = WIRE_FIGURE_COUNT
};

typedef struct WireMotion {
    const void *owner;
    Rect area;
    uint32_t started_ms;
    uint32_t ticked_ms;
    uint8_t turn;
    uint8_t shape;
    uint8_t percent;
    bool running;
    bool ticked;
} WireMotion;

void wire_motion_run(WireMotion *motion, const void *owner, WireShape shape, uint32_t now_ms);
void wire_motion_progress(WireMotion *motion, uint32_t done, uint32_t total);
void wire_motion_end(WireMotion *motion, const void *owner);
bool wire_motion_shown(const WireMotion *motion, const void *owner);
bool wire_motion_due(const WireMotion *motion, uint32_t now_ms);
void wire_motion_tick(WireMotion *motion, uint32_t now_ms);
Rect wire_motion_fit(Rect space);
void wire_motion_draw(WireMotion *motion, Canvas *canvas, const Theme *theme, Rect space);
