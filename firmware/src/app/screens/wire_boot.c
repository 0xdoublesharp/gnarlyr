#include "screens/wire_boot.h"

#include "gfx/text.h"
#include "gfx/wireframe.h"
#include "hal/clock.h"

enum {
    BOOT_COLUMNS = 5,
    BOOT_COLUMN_SHARE = 4,
    BOOT_NEAR = 24,
    BOOT_ROW_DEPTH = 16,
    BOOT_FAR = 288,
    BOOT_ROW_FAR = 128,
    BOOT_TRAVEL = 3 * BOOT_ROW_DEPTH,
    BOOT_HORIZON_SHARE = 9,
    BOOT_HORIZON_PARTS = 16,
    BOOT_COLUMN_ALPHA = 150,
    BOOT_ROW_ALPHA = 240,
    BOOT_LINE_END = 96,
    BOOT_FLOOR_START = 32,
    BOOT_FLOOR_END = 208,
    BOOT_MARK_START = 80,
    BOOT_MARK_END = 232,
    BOOT_FOLD_END = 160,
    BOOT_SINK_END = 128,
    BOOT_SHRINK_START = 112,
    BOOT_DARK_START = 64
};

typedef struct WireBootPose {
    int line;
    int floor;
    int mark;
    int light;
} WireBootPose;

typedef struct Ground {
    Canvas *canvas;
    Rect area;
    int horizon;
    int drop;
    int travel;
    int fade;
    int light;
} Ground;

static int phase(int progress, int start, int end)
{
    if (progress <= start) {
        return 0;
    }
    if (progress >= end) {
        return WIRE_BOOT_ONE;
    }
    return (progress - start) * WIRE_BOOT_ONE / (end - start);
}

static int ease(int value)
{
    return value * (2 * WIRE_BOOT_ONE - value) / WIRE_BOOT_ONE;
}

static int share_of(int value, int share)
{
    return value * share / WIRE_BOOT_ONE;
}

static WireBootPose opening_pose(int progress)
{
    return (WireBootPose){ease(phase(progress, 0, BOOT_LINE_END)),
                          ease(phase(progress, BOOT_FLOOR_START, BOOT_FLOOR_END)),
                          ease(phase(progress, BOOT_MARK_START, BOOT_MARK_END)), WIRE_BOOT_ONE};
}

static WireBootPose closing_pose(int progress)
{
    return (WireBootPose){WIRE_BOOT_ONE - ease(phase(progress, BOOT_SHRINK_START, WIRE_BOOT_ONE)),
                          WIRE_BOOT_ONE - ease(phase(progress, 0, BOOT_FOLD_END)),
                          WIRE_BOOT_ONE - phase(progress, 0, BOOT_SINK_END),
                          WIRE_BOOT_ONE - phase(progress, BOOT_DARK_START, WIRE_BOOT_ONE)};
}

static Ground ground_make(Canvas *canvas, Rect area, WireBootPose pose)
{
    int horizon = area.y + area.height * BOOT_HORIZON_SHARE / BOOT_HORIZON_PARTS;
    return (Ground){.canvas = canvas,
                    .area = area,
                    .horizon = horizon,
                    .drop = share_of(area.y + area.height - horizon, pose.floor),
                    .travel = share_of(BOOT_TRAVEL, WIRE_BOOT_ONE - pose.floor),
                    .fade = pose.floor,
                    .light = pose.light};
}

static Color ink(const Ground *ground, Color color)
{
    return color_scale(color, (unsigned)ground->light);
}

static int depth_scale(int depth)
{
    return BOOT_NEAR * WIRE_BOOT_ONE / (BOOT_NEAR + depth);
}

static WirePoint ground_point(const Ground *ground, int across, int depth)
{
    int scale = depth_scale(depth);
    int middle = (ground->area.x * 2 + ground->area.width) * WIREFRAME_SUBPIXEL / 2;
    int x = middle + share_of(across * WIREFRAME_SUBPIXEL, scale);
    int y =
        ground->horizon * WIREFRAME_SUBPIXEL + share_of(ground->drop * WIREFRAME_SUBPIXEL, scale);
    return (WirePoint){x, y, depth};
}

static void ground_line(const Ground *ground, WirePoint from, WirePoint to, WireStroke stroke)
{
    stroke.alpha = (uint8_t)share_of(stroke.alpha, ground->fade);
    wireframe_line(ground->canvas, from, to, stroke);
}

static void draw_columns(const Ground *ground, Color color)
{
    int spacing = ground->area.width / BOOT_COLUMN_SHARE;
    WireStroke stroke = {color, BOOT_COLUMN_ALPHA};
    for (int column = -BOOT_COLUMNS; column <= BOOT_COLUMNS; column++) {
        ground_line(ground, ground_point(ground, column * spacing, 0),
                    ground_point(ground, column * spacing, BOOT_FAR), stroke);
    }
}

static void draw_rows(const Ground *ground, Color color)
{
    int left = ground->area.x * WIREFRAME_SUBPIXEL;
    int right = (ground->area.x + ground->area.width) * WIREFRAME_SUBPIXEL;
    for (int depth = ground->travel; depth <= BOOT_ROW_FAR; depth += BOOT_ROW_DEPTH) {
        int y = ground_point(ground, 0, depth).y;
        WireStroke stroke = {color, (uint8_t)share_of(BOOT_ROW_ALPHA, depth_scale(depth))};
        ground_line(ground, (WirePoint){left, y, depth}, (WirePoint){right, y, depth}, stroke);
    }
}

static void draw_horizon(const Ground *ground, Color color, int share)
{
    if (share == 0) {
        return;
    }
    int middle = (ground->area.x * 2 + ground->area.width) * WIREFRAME_SUBPIXEL / 2;
    int half = share_of(ground->area.width * WIREFRAME_SUBPIXEL / 2, share);
    int y = ground->horizon * WIREFRAME_SUBPIXEL;
    wireframe_line(ground->canvas, (WirePoint){middle - half, y, 0},
                   (WirePoint){middle + half, y, 0}, (WireStroke){color, UINT8_MAX});
}

static void draw_mark(const Ground *ground, const Theme *theme, const char *text, int shown)
{
    const Font *font = theme->fonts.title;
    Rect sky = {ground->area.x, ground->area.y, ground->area.width,
                ground->horizon - ground->area.y};
    int rest = sky.y + (sky.height + font->ascent - font->descent) / 2;
    int hidden = ground->horizon + font->ascent;
    Color color =
        color_between(theme->colors.backdrop_bottom, theme->colors.text, shown, WIRE_BOOT_ONE);
    Canvas clipped = canvas_clipped(ground->canvas, sky);
    Point pen = {sky.x + (sky.width - text_width(font, text)) / 2,
                 hidden - share_of(hidden - rest, shown)};
    text_draw(&clipped, (TextStyle){font, ink(ground, color), false}, pen, text);
}

static void draw_backdrop(const Ground *ground, const Theme *theme, bool art)
{
    if (art && theme_art_draw(theme, ground->canvas, THEME_ART_SPLASH, ground->area)) {
        return;
    }
    canvas_fill_gradient(ground->canvas, ground->area, ink(ground, theme->colors.backdrop_top),
                         ink(ground, theme->colors.backdrop_bottom));
}

void wire_boot_draw(Canvas *canvas, const Theme *theme, Rect area, WireBootFrame frame)
{
    WireBootPose pose = frame.closing ? closing_pose(frame.progress) : opening_pose(frame.progress);
    Canvas clipped = canvas_clipped(canvas, area);
    Ground ground = ground_make(&clipped, area, pose);
    Color grid = ink(&ground, theme->colors.accent);
    draw_backdrop(&ground, theme, !frame.closing);
    draw_columns(&ground, grid);
    draw_rows(&ground, grid);
    draw_mark(&ground, theme, frame.text, pose.mark);
    draw_horizon(&ground, grid, pose.line);
}

void wire_boot_fade(Canvas *canvas, const Theme *theme, const Fade *fade, const char *text)
{
    uint32_t elapsed = hal_clock_milliseconds() - fade->start_ms;
    uint32_t length = fade->timing.fade_ms;
    uint32_t progress = elapsed < length ? elapsed * WIRE_BOOT_ONE / length : WIRE_BOOT_ONE;
    Rect area = {0, 0, theme->metrics.screen_width, theme->metrics.screen_height};
    wire_boot_draw(canvas, theme, area,
                   (WireBootFrame){text, (uint16_t)progress, !fade->timing.rising});
}
