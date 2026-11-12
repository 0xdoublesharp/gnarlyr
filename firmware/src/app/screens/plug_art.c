#include "gfx/wireframe.h"
#include "plug_chooser.h"

enum {
    INK_ACCENT,
    INK_DETAIL,
    POINTS = 24,
    YAW = 236,
    PITCH = 244,
    ALPHA_FAR = 7,
    ALPHA_RANGE = 8,
    MARGIN_SIXTEENTHS = 14,
    SIXTEENTHS = 16,
    WIDE_QUARTERS = 5,
    QUARTERS = 4
};

typedef struct PlugArtPart {
    WireModel model;
    uint8_t ink;
} PlugArtPart;

typedef struct PlugArt {
    const PlugArtPart *parts;
    uint8_t count;
} PlugArt;

static const WireVertex BATTERY_VERTICES[] = {
    {-96, -48, -24}, {80, -48, -24}, {80, 48, -24},  {-96, 48, -24},  {-96, -48, 24}, {80, -48, 24},
    {80, 48, 24},    {-96, 48, 24},  {80, -18, -12}, {100, -18, -12}, {100, 18, -12}, {80, 18, -12},
    {80, -18, 12},   {100, -18, 12}, {100, 18, 12},  {80, 18, 12},
};

static const WireEdge BOX_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static const WireEdge TWO_BOX_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},  {4, 5},   {5, 6},   {6, 7},   {7, 4},   {0, 4},  {1, 5},
    {2, 6}, {3, 7}, {8, 9}, {9, 10}, {10, 11}, {12, 13}, {13, 14}, {14, 15}, {9, 13}, {10, 14},
};

static const WireVertex BOLT_VERTICES[] = {
    {14, -40, -24}, {-30, 6, -24}, {-4, 6, -24}, {-18, 40, -24}, {30, -8, -24}, {4, -8, -24},
};

static const WireEdge LOOP_6_EDGES[] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}};

static const WireVertex PLUG_VERTICES[] = {
    {-48, -16, -30}, {48, -16, -30}, {48, 56, -30}, {-48, 56, -30},
    {-48, -16, 30},  {48, -16, 30},  {48, 56, 30},  {-48, 56, 30},
};

static const WireVertex PRONG_VERTICES[] = {
    {-28, -80, 0}, {-16, -80, 0}, {-16, -16, 0}, {-28, -16, 0}, {16, -80, 0}, {28, -80, 0},
    {28, -16, 0},  {16, -16, 0},  {0, 56, 0},    {0, 92, 0},    {24, 116, 0}, {72, 116, 0},
};

static const WireEdge PRONG_EDGES[] = {
    {0, 1}, {1, 2}, {3, 0}, {4, 5}, {5, 6}, {7, 4}, {8, 9}, {9, 10}, {10, 11},
};

static const WireVertex LAPTOP_VERTICES[] = {
    {-104, 48, -64}, {104, 48, -64}, {104, 48, 36},  {-104, 48, 36}, {-104, 60, -64},
    {104, 60, -64},  {104, 60, 36},  {-104, 60, 36}, {-96, 48, 36},  {96, 48, 36},
    {96, -88, 52},   {-96, -88, 52}, {-80, 34, 38},  {80, 34, 38},   {80, -74, 50},
    {-80, -74, 50},  {-72, 48, -46}, {72, 48, -46},  {-72, 48, -14}, {72, 48, -14},
};

static const WireEdge LAPTOP_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},  {6, 7},   {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}, {8, 9}, {9, 10}, {10, 11}, {11, 8},
};

static const WireEdge SCREEN_EDGES[] = {{12, 13}, {13, 14}, {14, 15}, {15, 12}, {16, 17}, {18, 19}};

static const WireVertex SPEAKER_VERTICES[] = {
    {-104, -28, -28}, {-64, -28, -28}, {-64, 28, -28}, {-104, 28, -28},
    {-104, -28, 28},  {-64, -28, 28},  {-64, 28, 28},  {-104, 28, 28},
    {-20, -72, -72},  {-20, 72, -72},  {-20, 72, 72},  {-20, -72, 72},
};

static const WireEdge SPEAKER_EDGES[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5},  {5, 6},  {6, 7}, {7, 4},  {0, 4},   {1, 5},
    {2, 6}, {3, 7}, {1, 8}, {2, 9}, {6, 10}, {5, 11}, {8, 9}, {9, 10}, {10, 11}, {11, 8},
};

static const WireVertex WAVE_VERTICES[] = {
    {12, -40, 0}, {34, -22, 0}, {40, 0, 0}, {34, 22, 0}, {12, 40, 0},
    {32, -84, 0}, {72, -48, 0}, {84, 0, 0}, {72, 48, 0}, {32, 84, 0},
};

static const WireEdge WAVE_EDGES[] = {{0, 1}, {1, 2}, {2, 3}, {3, 4},
                                      {5, 6}, {6, 7}, {7, 8}, {8, 9}};

static const WireVertex QUESTION_VERTICES[] = {
    {-36, -44, 0}, {-20, -68, 0}, {20, -68, 0}, {40, -46, 0}, {40, -22, 0}, {6, 4, 0},
    {6, 28, 0},    {-2, 46, 0},   {14, 46, 0},  {14, 62, 0},  {-2, 62, 0},
};

static const WireEdge QUESTION_EDGES[] = {{0, 1}, {1, 2}, {2, 3}, {3, 4},  {4, 5},
                                          {5, 6}, {7, 8}, {8, 9}, {9, 10}, {10, 7}};

static const WireVertex RING_VERTICES[] = {
    {0, -120, 0}, {60, -104, 0}, {104, -60, 0}, {120, 0, 0},  {104, 60, 0},   {60, 104, 0},
    {0, 120, 0},  {-60, 104, 0}, {-104, 60, 0}, {-120, 0, 0}, {-104, -60, 0}, {-60, -104, 0},
};

static const WireEdge RING_EDGES[] = {{0, 1}, {1, 2}, {2, 3}, {3, 4},  {4, 5},   {5, 6},
                                      {6, 7}, {7, 8}, {8, 9}, {9, 10}, {10, 11}, {11, 0}};

#define PLUG_MODEL(vertices, edges)                                                                \
    {vertices, edges, (uint8_t)(sizeof vertices / sizeof *vertices),                               \
     (uint8_t)(sizeof edges / sizeof *edges)}

static const PlugArtPart ASK_PARTS[] = {
    {PLUG_MODEL(RING_VERTICES, RING_EDGES), INK_DETAIL},
    {PLUG_MODEL(QUESTION_VERTICES, QUESTION_EDGES), INK_ACCENT},
};

static const PlugArtPart COMPUTER_PARTS[] = {
    {PLUG_MODEL(LAPTOP_VERTICES, LAPTOP_EDGES), INK_ACCENT},
    {PLUG_MODEL(LAPTOP_VERTICES, SCREEN_EDGES), INK_DETAIL},
};

static const PlugArtPart CHARGE_PARTS[] = {
    {PLUG_MODEL(BATTERY_VERTICES, TWO_BOX_EDGES), INK_ACCENT},
    {PLUG_MODEL(BOLT_VERTICES, LOOP_6_EDGES), INK_DETAIL},
};

static const PlugArtPart USB_POWER_PARTS[] = {
    {PLUG_MODEL(PLUG_VERTICES, BOX_EDGES), INK_ACCENT},
    {PLUG_MODEL(PRONG_VERTICES, PRONG_EDGES), INK_DETAIL},
};

static const PlugArtPart DAC_PARTS[] = {
    {PLUG_MODEL(SPEAKER_VERTICES, SPEAKER_EDGES), INK_ACCENT},
    {PLUG_MODEL(WAVE_VERTICES, WAVE_EDGES), INK_DETAIL},
};

#define PLUG_ART(parts) {parts, (uint8_t)(sizeof parts / sizeof *parts)}

static const PlugArt ARTS[PLUG_MODE_COUNT] = {
    PLUG_ART(ASK_PARTS),       PLUG_ART(COMPUTER_PARTS), PLUG_ART(CHARGE_PARTS),
    PLUG_ART(USB_POWER_PARTS), PLUG_ART(DAC_PARTS),
};

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static WireView view_in(Rect area)
{
    int span = smaller(area.width, area.height * WIDE_QUARTERS / QUARTERS);
    return (WireView){.yaw = YAW,
                      .pitch = PITCH,
                      .center = {area.x + area.width / 2, area.y + area.height / 2},
                      .radius = (span / 2 - 1) * MARGIN_SIXTEENTHS / SIXTEENTHS};
}

static void draw_part(Canvas *canvas, const WireView *view, const PlugArtPart *part, Color color)
{
    static const WirePlace PLACE = {0, WIREFRAME_FULL_SIZE, 0, 0, 0};
    const WireModel *model = &part->model;
    WirePoint points[POINTS];
    for (int index = 0; index < model->vertex_count && index < POINTS; index++) {
        points[index] = wireframe_project(model->vertices[index], PLACE, view);
    }
    for (int index = 0; index < model->edge_count; index++) {
        WirePoint from = points[model->edges[index].from];
        WirePoint to = points[model->edges[index].to];
        int near = (from.depth + to.depth) / 2;
        int alpha = ALPHA_FAR + ((near * ALPHA_RANGE) >> 8);
        wireframe_line(canvas, from, to, (WireStroke){color, (uint8_t)alpha});
    }
}

void plug_art_draw(Canvas *canvas, const Theme *theme, Rect area, PlugMode mode)
{
    Canvas clipped = canvas_clipped(canvas, area);
    if (rect_is_empty(clipped.clip) || mode >= PLUG_MODE_COUNT) {
        return;
    }
    WireView view = view_in(area);
    const PlugArt *art = &ARTS[mode];
    for (int index = 0; index < art->count; index++) {
        const PlugArtPart *part = &art->parts[index];
        Color color = part->ink == INK_ACCENT ? theme->colors.accent : theme->colors.text_secondary;
        draw_part(&clipped, &view, part, color);
    }
}
