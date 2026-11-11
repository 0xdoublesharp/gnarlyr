#pragma once

#include <stdint.h>

#include "gfx/canvas.h"

enum {
    WIREFRAME_TURN = 256,
    WIREFRAME_QUARTER = WIREFRAME_TURN / 4,
    WIREFRAME_ONE = 1 << 14,
    WIREFRAME_SUBPIXEL = 16,
    WIREFRAME_FULL_SIZE = 128,
    WIREFRAME_RADIUS = 127,
    WIREFRAME_DISTANCE = 384,
    WIREFRAME_NEAREST = 255
};

typedef struct WireVertex {
    int8_t x;
    int8_t y;
    int8_t z;
} WireVertex;

typedef struct WireEdge {
    uint8_t from;
    uint8_t to;
} WireEdge;

typedef struct WireModel {
    const WireVertex *vertices;
    const WireEdge *edges;
    uint8_t vertex_count;
    uint8_t edge_count;
} WireModel;

typedef struct WirePlace {
    uint8_t spin;
    uint8_t size;
    int8_t x;
    int8_t y;
    int8_t z;
} WirePlace;

typedef struct WireView {
    uint8_t yaw;
    uint8_t pitch;
    Point center;
    int radius;
} WireView;

typedef struct WirePoint {
    int x;
    int y;
    int depth;
} WirePoint;

typedef struct WireStroke {
    Color color;
    uint8_t alpha;
} WireStroke;

int wireframe_sin(uint8_t angle);
int wireframe_cos(uint8_t angle);
WirePoint wireframe_project(WireVertex vertex, WirePlace place, const WireView *view);
void wireframe_line(Canvas *canvas, WirePoint from, WirePoint to, WireStroke stroke);
