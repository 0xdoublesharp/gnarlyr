#include "ui/wire_figure.h"

enum {
    PART_FIXED,
    PART_SPIN,
    PART_SUPPLY,
    PART_TAKEUP,
    PART_GAUGE,
    INK_ACCENT = 0,
    INK_TEXT = 1,
    PART_POINTS = 16,
    PERCENT = 100,
    HALF = 50,
    PACK_SMALLEST = 16,
    PACK_LARGEST = 30,
    REEL_SIZE = 14,
    REEL_X = 36,
    REEL_Y = -6,
    FRONT = -10,
    CASSETTE_SWAY = 20,
    CASSETTE_TILT = 244,
    DISC_SWAY = 10,
    DISC_TILT = 34,
    SOLID_TILT = 20,
    SOLID_WOBBLE = 16,
    ALPHA_FAR = 7,
    ALPHA_RANGE = 8,
    ALPHA_GHOST = 3,
    MARGIN_SIXTEENTHS = 15,
    WIDE_QUARTERS = 5,
    QUARTERS = 4
};

typedef struct WirePart {
    const WireModel *model;
    WirePlace place;
    uint8_t role;
    uint8_t ink;
} WirePart;

typedef struct WireFigure {
    const WirePart *parts;
    uint8_t count;
    uint8_t sway;
    uint8_t tilt;
    uint8_t wobble;
} WireFigure;

typedef struct WirePainter {
    Canvas canvas;
    const Theme *theme;
    WireFigureFrame frame;
    WireView view;
} WirePainter;

static const WireVertex CASSETTE_VERTICES[] = {
    {-104, -64, -10}, {104, -64, -10}, {104, 64, -10}, {-104, 64, -10},
    {-104, -64, 10},  {104, -64, 10},  {104, 64, 10},  {-104, 64, 10},
    {-56, -30, -10},  {56, -30, -10},  {56, 18, -10},  {-56, 18, -10},
    {-72, 64, -10},   {-60, 42, -10},  {60, 42, -10},  {72, 64, -10},
};

static const WireEdge CASSETTE_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},  {4, 5},   {5, 6},  {6, 7},   {7, 4},   {0, 4},   {1, 5},
    {2, 6}, {3, 7}, {8, 9}, {9, 10}, {10, 11}, {11, 8}, {12, 13}, {13, 14}, {14, 15},
};

static const WireVertex REEL_VERTICES[] = {
    {127, 0, 0}, {64, 110, 0}, {-64, 110, 0}, {-127, 0, 0}, {-64, -110, 0}, {64, -110, 0},
};

static const WireEdge REEL_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}, {0, 2}, {2, 4}, {4, 0},
};

static const WireVertex RING_VERTICES[] = {
    {127, 0, 0},  {110, 64, 0},   {64, 110, 0},   {0, 127, 0},  {-64, 110, 0}, {-110, 64, 0},
    {-127, 0, 0}, {-110, -64, 0}, {-64, -110, 0}, {0, -127, 0}, {64, -110, 0}, {110, -64, 0},
};

static const WireEdge RING_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 4},  {4, 5},   {5, 6},
    {6, 7}, {7, 8}, {8, 9}, {9, 10}, {10, 11}, {11, 0},
};

static const WireVertex ICOSAHEDRON_VERTICES[] = {
    {0, -67, -108}, {0, -67, 108}, {0, 67, -108},  {0, 67, 108},  {-67, -108, 0}, {-67, 108, 0},
    {67, -108, 0},  {67, 108, 0},  {-108, 0, -67}, {-108, 0, 67}, {108, 0, -67},  {108, 0, 67},
};

static const WireEdge ICOSAHEDRON_EDGES[] = {
    {0, 2}, {0, 4}, {0, 6}, {0, 8},  {0, 10}, {1, 3},  {1, 4},  {1, 6},  {1, 9}, {1, 11},
    {2, 5}, {2, 7}, {2, 8}, {2, 10}, {3, 5},  {3, 7},  {3, 9},  {3, 11}, {4, 6}, {4, 8},
    {4, 9}, {5, 7}, {5, 8}, {5, 9},  {6, 10}, {6, 11}, {7, 10}, {7, 11}, {8, 9}, {10, 11},
};

#define WIRE_MODEL(vertices, edges)                                                                \
    {vertices, edges, (uint8_t)(sizeof vertices / sizeof *vertices),                               \
     (uint8_t)(sizeof edges / sizeof *edges)}

const WireModel WIRE_MODEL_CASSETTE = WIRE_MODEL(CASSETTE_VERTICES, CASSETTE_EDGES);
const WireModel WIRE_MODEL_REEL = WIRE_MODEL(REEL_VERTICES, REEL_EDGES);
const WireModel WIRE_MODEL_RING = WIRE_MODEL(RING_VERTICES, RING_EDGES);
const WireModel WIRE_MODEL_ICOSAHEDRON = WIRE_MODEL(ICOSAHEDRON_VERTICES, ICOSAHEDRON_EDGES);

static const WirePart CASSETTE_PARTS[] = {
    {&WIRE_MODEL_CASSETTE, {0, WIREFRAME_FULL_SIZE, 0, 0, 0}, PART_FIXED, INK_ACCENT},
    {&WIRE_MODEL_REEL, {0, REEL_SIZE, -REEL_X, REEL_Y, FRONT}, PART_SPIN, INK_TEXT},
    {&WIRE_MODEL_REEL, {0, REEL_SIZE, REEL_X, REEL_Y, FRONT}, PART_SPIN, INK_TEXT},
    {&WIRE_MODEL_RING, {0, 0, -REEL_X, REEL_Y, FRONT}, PART_SUPPLY, INK_ACCENT},
    {&WIRE_MODEL_RING, {0, 0, REEL_X, REEL_Y, FRONT}, PART_TAKEUP, INK_ACCENT},
};

static const WirePart ICOSAHEDRON_PARTS[] = {
    {&WIRE_MODEL_ICOSAHEDRON, {0, WIREFRAME_FULL_SIZE, 0, 0, 0}, PART_GAUGE, INK_ACCENT},
};

static const WirePart DISC_PARTS[] = {
    {&WIRE_MODEL_RING, {0, WIREFRAME_RADIUS, 0, 0, 0}, PART_GAUGE, INK_ACCENT},
    {&WIRE_MODEL_RING, {0, 88, 0, 0, 0}, PART_FIXED, INK_TEXT},
    {&WIRE_MODEL_REEL, {0, 46, 0, 0, 0}, PART_SPIN, INK_ACCENT},
};

static const WireFigure FIGURES[WIRE_FIGURE_COUNT] = {
    {CASSETTE_PARTS, sizeof CASSETTE_PARTS / sizeof *CASSETTE_PARTS, CASSETTE_SWAY, CASSETTE_TILT,
     0},
    {ICOSAHEDRON_PARTS, sizeof ICOSAHEDRON_PARTS / sizeof *ICOSAHEDRON_PARTS, 0, SOLID_TILT,
     SOLID_WOBBLE},
    {DISC_PARTS, sizeof DISC_PARTS / sizeof *DISC_PARTS, DISC_SWAY, DISC_TILT, 0},
};

static int swayed(uint8_t turn, int reach)
{
    return wireframe_sin(turn) * reach / WIREFRAME_ONE;
}

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static WireView view_of(WireFigureFrame frame, Rect area)
{
    const WireFigure *figure = &FIGURES[frame.shape];
    uint8_t turn = frame.turn;
    int span = smaller(area.width, area.height * WIDE_QUARTERS / QUARTERS);
    int yaw = figure->sway != 0 ? swayed(turn, figure->sway) : turn;
    return (WireView){
        .yaw = (uint8_t)yaw,
        .pitch = (uint8_t)(figure->tilt + swayed(turn, figure->wobble)),
        .center = {area.x + area.width / 2, area.y + area.height / 2},
        .radius = (span / 2 - 1) * MARGIN_SIXTEENTHS / WIREFRAME_SUBPIXEL,
    };
}

static int shown_percent(WireFigureFrame frame)
{
    return frame.percent == WIRE_FIGURE_ENDLESS ? HALF : frame.percent;
}

static WirePlace placed(const WirePart *part, WireFigureFrame frame)
{
    WirePlace place = part->place;
    int range = PACK_LARGEST - PACK_SMALLEST;
    if (part->role == PART_SPIN) {
        place.spin = (uint8_t)(frame.turn * 2);
    } else if (part->role == PART_SUPPLY) {
        place.size = (uint8_t)(PACK_LARGEST - range * shown_percent(frame) / PERCENT);
    } else if (part->role == PART_TAKEUP) {
        place.size = (uint8_t)(PACK_SMALLEST + range * shown_percent(frame) / PERCENT);
    }
    return place;
}

static int lit_edges(const WirePart *part, WireFigureFrame frame)
{
    int count = part->model->edge_count;
    if (part->role != PART_GAUGE || frame.percent == WIRE_FIGURE_ENDLESS) {
        return count;
    }
    return count * frame.percent / PERCENT;
}

static void draw_part(WirePainter *painter, const WirePart *part)
{
    const WireModel *model = part->model;
    WirePoint points[PART_POINTS];
    WirePlace place = placed(part, painter->frame);
    for (int index = 0; index < model->vertex_count && index < PART_POINTS; index++) {
        points[index] = wireframe_project(model->vertices[index], place, &painter->view);
    }
    const ThemeColors *colors = &painter->theme->colors;
    Color color = part->ink == INK_ACCENT ? colors->accent : colors->text_secondary;
    int lit = lit_edges(part, painter->frame);
    for (int index = 0; index < model->edge_count; index++) {
        WirePoint from = points[model->edges[index].from];
        WirePoint to = points[model->edges[index].to];
        int near = (from.depth + to.depth) / 2;
        int alpha = index < lit ? ALPHA_FAR + ((near * ALPHA_RANGE) >> 8) : ALPHA_GHOST;
        wireframe_line(&painter->canvas, from, to, (WireStroke){color, (uint8_t)alpha});
    }
}

void wire_figure_draw(Canvas *canvas, const Theme *theme, Rect area, WireFigureFrame frame)
{
    WirePainter painter = {canvas_clipped(canvas, area), theme, frame, view_of(frame, area)};
    if (rect_is_empty(painter.canvas.clip) || frame.shape >= WIRE_FIGURE_COUNT) {
        return;
    }
    const WireFigure *figure = &FIGURES[frame.shape];
    for (int index = 0; index < figure->count; index++) {
        draw_part(&painter, &figure->parts[index]);
    }
}
