#include "gfx/wireframe.h"
#include "usb_screen_parts.h"

enum {
    TERRAIN_COLUMNS = 9,
    TERRAIN_MIDDLE = TERRAIN_COLUMNS / 2,
    TERRAIN_NEAR = 24,
    TERRAIN_ONE = 256,
    TERRAIN_HORIZON_SHARE = 5,
    TERRAIN_PEAK_SHARE = 4,
    TERRAIN_FLOOR = 1000,
    TERRAIN_HEADROOM_SHARE = 5,
    TERRAIN_FADE_STEP = 2,
    TERRAIN_FLOOR_ALPHA = 96,
    TERRAIN_DIM_STEPS = 5,
    TERRAIN_ROW_DEPTH = 2
};

static const uint8_t HUMP[TERRAIN_COLUMNS] = {0, 3, 4, 3, 0, 3, 4, 3, 0};

typedef struct Terrain {
    const UsbPainter *painter;
    Rect area;
    int horizon;
    int peak;
    uint32_t top;
    bool lively;
} Terrain;

static int depth_scale(int depth)
{
    return TERRAIN_NEAR * TERRAIN_ONE / (TERRAIN_NEAR + depth);
}

static uint32_t terrain_top(const UsbMeter *meter)
{
    uint32_t top = TERRAIN_FLOOR;
    for (int age = 0; age < meter->count; age++) {
        uint32_t total = usb_meter_rate(meter, USB_METER_READ, age);
        uint32_t written = usb_meter_rate(meter, USB_METER_WRITE, age);
        top = total > top ? total : top;
        top = written > top ? written : top;
    }
    return top + top / TERRAIN_HEADROOM_SHARE;
}

static int column_height(const Terrain *terrain, int age, int column)
{
    UsbMeterDirection direction = column < TERRAIN_MIDDLE ? USB_METER_READ : USB_METER_WRITE;
    uint32_t rate = usb_meter_rate(&terrain->painter->scene->meter, direction, age);
    uint64_t height = (uint64_t)rate * (uint32_t)terrain->peak * HUMP[column];
    return (int)(height / (terrain->top * HUMP[TERRAIN_MIDDLE / 2]));
}

static WirePoint project(const Terrain *terrain, int column, int depth, int height)
{
    Rect area = terrain->area;
    int scale = depth_scale(depth);
    int floor = area.y + area.height - 1;
    int half = area.width / 2 - 1;
    int world = (column - TERRAIN_MIDDLE) * half / TERRAIN_MIDDLE;
    int x = (area.x + half) * WIREFRAME_SUBPIXEL + world * scale * WIREFRAME_SUBPIXEL / TERRAIN_ONE;
    int ground = (floor - terrain->horizon) * scale;
    int y = terrain->horizon * WIREFRAME_SUBPIXEL +
            (ground - height * scale) * WIREFRAME_SUBPIXEL / TERRAIN_ONE;
    return (WirePoint){x, y, depth};
}

static Color terrain_color(const Terrain *terrain, int column)
{
    const ThemeColors *colors = &terrain->painter->theme->colors;
    Color lit = column < TERRAIN_MIDDLE ? colors->accent : colors->success;
    return terrain->lively ? lit
                           : color_between(colors->backdrop_bottom, lit, 2, TERRAIN_DIM_STEPS);
}

static void draw_floor(const Terrain *terrain, int far)
{
    Canvas *canvas = terrain->painter->canvas;
    Color rule = terrain->painter->theme->colors.text_tertiary;
    for (int column = 0; column < TERRAIN_COLUMNS; column++) {
        wireframe_line(canvas, project(terrain, column, 0, 0), project(terrain, column, far, 0),
                       (WireStroke){rule, TERRAIN_FLOOR_ALPHA});
    }
}

typedef struct TerrainPoint {
    WirePoint at;
    int height;
} TerrainPoint;

static TerrainPoint terrain_point(const Terrain *terrain, int column, int age)
{
    int height = column_height(terrain, age, column);
    return (TerrainPoint){project(terrain, column, age * TERRAIN_ROW_DEPTH, height), height};
}

static void draw_segment(const Terrain *terrain, TerrainPoint from, TerrainPoint to,
                         WireStroke stroke)
{
    if (from.height == 0 && to.height == 0) {
        stroke = (WireStroke){terrain->painter->theme->colors.text_tertiary, TERRAIN_FLOOR_ALPHA};
    }
    wireframe_line(terrain->painter->canvas, from.at, to.at, stroke);
}

static void draw_row(const Terrain *terrain, int age)
{
    uint8_t alpha = (uint8_t)(UINT8_MAX - age * TERRAIN_ROW_DEPTH * TERRAIN_FADE_STEP);
    TerrainPoint left = terrain_point(terrain, 0, age);
    for (int column = 1; column < TERRAIN_COLUMNS; column++) {
        TerrainPoint right = terrain_point(terrain, column, age);
        WireStroke stroke = {terrain_color(terrain, column - (column > TERRAIN_MIDDLE ? 0 : 1)),
                             alpha};
        draw_segment(terrain, left, right, stroke);
        if (age + 1 < USB_METER_SAMPLES) {
            draw_segment(terrain, right, terrain_point(terrain, column, age + 1), stroke);
        }
        left = right;
    }
}

void usb_screen_draw_graph(const UsbPainter *painter)
{
    Rect area = usb_screen_graph(painter->theme, &painter->layout);
    const UsbScene *scene = painter->scene;
    Terrain terrain = {.painter = painter,
                       .area = area,
                       .horizon = area.y + area.height / TERRAIN_HORIZON_SHARE,
                       .top = terrain_top(&scene->meter),
                       .lively = scene->busy};
    terrain.peak =
        (area.y + area.height - terrain.horizon) * (TERRAIN_PEAK_SHARE - 1) / TERRAIN_PEAK_SHARE;
    Rect clip = painter->canvas->clip;
    painter->canvas->clip = rect_intersection(clip, area);
    draw_floor(&terrain, (USB_METER_SAMPLES - 1) * TERRAIN_ROW_DEPTH);
    for (int age = USB_METER_SAMPLES - 1; age >= 0; age--) {
        draw_row(&terrain, age);
        usb_screen_service(scene);
    }
    painter->canvas->clip = clip;
}
