#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "gfx/wireframe.h"
#include "ui/theme.h"

typedef enum WireShape {
    WIRE_SHAPE_CASSETTE,
    WIRE_SHAPE_ICOSAHEDRON,
    WIRE_SHAPE_DISC
} WireShape;

enum {
    WIRE_FIGURE_ENDLESS = 255,
    WIRE_FIGURE_COUNT = WIRE_SHAPE_DISC + 1
};

typedef struct WireFigureFrame {
    uint8_t shape;
    uint8_t turn;
    uint8_t percent;
} WireFigureFrame;

extern const WireModel WIRE_MODEL_CASSETTE;
extern const WireModel WIRE_MODEL_REEL;
extern const WireModel WIRE_MODEL_RING;
extern const WireModel WIRE_MODEL_ICOSAHEDRON;

void wire_figure_draw(Canvas *canvas, const Theme *theme, Rect area, WireFigureFrame frame);
