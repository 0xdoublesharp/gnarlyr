#include "sunset_art.h"

#include "core/i18n.h"
#include "neon_art.h"
#include "ui/label.h"

enum {
    SUNSET_RAY_START = 8
};

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static int larger(int first, int second)
{
    return first > second ? first : second;
}

static bool touches(const Canvas *canvas, Rect area)
{
    return !rect_is_empty(rect_intersection(canvas->clip, area));
}

static int glow_top(const SunsetScene *scene)
{
    return scene->horizon - (scene->horizon - scene->stage.y) / 3;
}

static Color glow_color(const ThemeColors *colors)
{
    return color_between(colors->backdrop_bottom, colors->accent_alt, 1, 3);
}

static Color sky_at(const SunsetArt *art, int y)
{
    const ThemeColors *colors = &art->theme->colors;
    const SunsetScene *scene = &art->scene;
    int glow = glow_top(scene);
    if (y < glow) {
        return color_between(colors->backdrop_top, colors->backdrop_bottom, y - scene->stage.y,
                             glow - scene->stage.y - 1);
    }
    return color_between(colors->backdrop_bottom, glow_color(colors), y - glow,
                         scene->horizon - glow - 1);
}

static void stars(const SunsetArt *art, Canvas *canvas)
{
    const ThemeColors *colors = &art->theme->colors;
    for (int star = 0; star < SUNSET_STARS; star++) {
        Point point = sunset_scene_star(&art->scene, star);
        Color ink = color_between(colors->backdrop_bottom, colors->text, 1, 2 + star % 2);
        canvas_fill_rect(canvas, (Rect){point.x, point.y, 1, 1}, ink);
    }
}

static void sky(const SunsetArt *art, Canvas *canvas)
{
    const ThemeColors *colors = &art->theme->colors;
    const SunsetScene *scene = &art->scene;
    int glow = glow_top(scene);
    Rect upper = {scene->stage.x, scene->stage.y, scene->stage.width, glow - scene->stage.y};
    Rect lower = {scene->stage.x, glow, scene->stage.width, scene->horizon - glow};
    canvas_fill_gradient(canvas, upper, colors->backdrop_top, colors->backdrop_bottom);
    canvas_fill_gradient(canvas, lower, colors->backdrop_bottom, glow_color(colors));
    if (touches(canvas, upper)) {
        stars(art, canvas);
    }
}

bool sunset_art_peak_lit(const MeterState *meter, int channel, uint32_t now_ms)
{
    return meter->peak_until[channel] != 0 && (int32_t)(meter->peak_until[channel] - now_ms) > 0;
}

SunsetArt sunset_art_make(const Theme *theme, Rect stage, const MeterState *meter, bool sun_meter)
{
    const ThemeColors *colors = &theme->colors;
    return (SunsetArt){.theme = theme,
                       .scene = sunset_scene_of(theme, stage),
                       .meter = meter,
                       .sun_meter = sun_meter,
                       .crown = color_between(colors->accent_alt, colors->text, 1, 2),
                       .base = color_between(colors->accent_alt, colors->danger, 1, 2)};
}

static Color sun_hot(const SunsetArt *art, int y)
{
    int top = sunset_scene_top(&art->scene);
    return color_between(art->crown, art->base, y - top, art->scene.horizon - top);
}

typedef void (*SunsetRow)(const SunsetArt *art, Canvas *canvas, int y, int half);

static void disc_row(const SunsetArt *art, Canvas *canvas, int y, int half)
{
    canvas_fill_rect(canvas, (Rect){art->scene.sun.x - half, y, half * 2, 1}, sun_hot(art, y));
}

static bool sun_rows(const SunsetArt *art, Canvas *canvas, SunsetRow row)
{
    const SunsetScene *scene = &art->scene;
    int top = sunset_scene_top(scene);
    Rect disc = {scene->sun.x - scene->radius - 8, top, scene->radius * 2 + 16,
                 scene->horizon - top};
    if (!touches(canvas, disc)) {
        return false;
    }
    int last = smaller(canvas->clip.y + canvas->clip.height, scene->horizon);
    for (int y = larger(canvas->clip.y, top); y < last; y++) {
        int half = sunset_scene_half_width(scene, y);
        if (half > 0 && !sunset_scene_slat_gap(scene, y)) {
            row(art, canvas, y, half);
        }
    }
    return true;
}

static void sun(const SunsetArt *art, Canvas *canvas)
{
    sun_rows(art, canvas, disc_row);
}

static int floored(int value, int step)
{
    return value >= 0 ? value / step : -((-value + step - 1) / step);
}

static void ray_row(const SunsetArt *art, Canvas *canvas, int y, Color ray)
{
    const SunsetScene *scene = &art->scene;
    Rect floor = sunset_scene_floor(scene);
    int step = SUNSET_RAY_START + (y - scene->horizon) * floor.width / (floor.height * 4);
    int first = floored(canvas->clip.x - scene->sun.x + step - 1, step);
    int last = floored(canvas->clip.x + canvas->clip.width - 1 - scene->sun.x, step);
    first = larger(first, -SUNSET_RAYS);
    last = smaller(last, SUNSET_RAYS);
    for (int index = first; index <= last; index++) {
        canvas_fill_rect(canvas, (Rect){scene->sun.x + index * step, y, 1, 1}, ray);
    }
}

static void horizon_lines(const SunsetArt *art, Canvas *canvas)
{
    const ThemeColors *colors = &art->theme->colors;
    Rect stage = art->scene.stage;
    int horizon = art->scene.horizon;
    canvas_fill_rect(canvas, (Rect){stage.x, horizon - 1, stage.width, 1},
                     color_between(colors->accent, colors->backdrop_top, 1, 2));
    canvas_fill_rect(canvas, (Rect){stage.x, horizon, stage.width, 1},
                     color_between(colors->accent, colors->text, 1, 2));
}

static void ridge(const SunsetArt *art, Canvas *canvas)
{
    const ThemeColors *colors = &art->theme->colors;
    Color ink = color_between(colors->backdrop_bottom, colors->text_secondary, 2, 3);
    NeonInk soft = {ink, color_between(sky_at(art, art->scene.horizon - 4), ink, 1, 3)};
    for (int point = 1; point < SUNSET_RIDGE_POINTS; point++) {
        if (point != SUNSET_RIDGE_BREAK) {
            neon_art_line(canvas, sunset_scene_ridge(&art->scene, point - 1),
                          sunset_scene_ridge(&art->scene, point), soft);
        }
    }
}

static void grid(const SunsetArt *art, Canvas *canvas)
{
    const ThemeColors *colors = &art->theme->colors;
    Rect area = sunset_scene_floor(&art->scene);
    Canvas floor = canvas_clipped(canvas, area);
    if (!rect_is_empty(floor.clip)) {
        canvas_fill_gradient(&floor, area, colors->backdrop_bottom, colors->background);
        Color ray = color_between(colors->divider, colors->accent, 1, 3);
        for (int y = floor.clip.y; y < floor.clip.y + floor.clip.height; y++) {
            ray_row(art, &floor, y, ray);
        }
        Color line = color_between(colors->divider, colors->accent, 1 + art->meter->pulse,
                                   SUNSET_PULSE_STEPS);
        for (int index = 0; index < SUNSET_FLOOR_LINES; index++) {
            int y = sunset_scene_floor_line(&art->scene, index);
            canvas_fill_rect(&floor, (Rect){area.x, y, area.width, 1}, line);
        }
    }
    horizon_lines(art, canvas);
    ridge(art, canvas);
}

static Color segment_ink(const SunsetArt *art, int side, int segment)
{
    const ThemeColors *colors = &art->theme->colors;
    int lit = sunset_scene_lit(art->meter->shown[side]);
    int hold = sunset_scene_lit(art->meter->caps[side]);
    if (segment == SUNSET_SEGMENTS - 1 && sunset_art_peak_lit(art->meter, side, art->now_ms)) {
        return colors->danger;
    }
    if (segment < lit) {
        return colors->accent;
    }
    return segment == hold - 1 ? colors->accent_alt : colors->divider;
}

static void meter_row(const SunsetArt *art, Canvas *canvas, int side)
{
    const Theme *theme = art->theme;
    Rect row = sunset_scene_row(&art->scene, side);
    if (!touches(canvas, row)) {
        return;
    }
    canvas_fill_rect(canvas, row, theme->colors.background);
    Rect label = {row.x + 4, row.y, row.width, row.height};
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
               (Label){label, i18n_text(side == 0 ? STR_METER_LEFT : STR_METER_RIGHT)});
    for (int segment = 0; segment < SUNSET_SEGMENTS; segment++) {
        canvas_fill_rect(canvas, sunset_scene_segment(&art->scene, side, segment),
                         segment_ink(art, side, segment));
    }
}

static void bed(const SunsetArt *art, Canvas *canvas)
{
    canvas_fill_rect(canvas, art->scene.bed, art->theme->colors.background);
    meter_row(art, canvas, 0);
    meter_row(art, canvas, 1);
}

static void bar_body(const SunsetArt *art, Canvas *canvas, Rect column, int top)
{
    const ThemeColors *colors = &art->theme->colors;
    int horizon = art->scene.horizon;
    Rect bar = {column.x, top, column.width, horizon - 1 - top};
    canvas_fill_rect(canvas, (Rect){column.x - 1, top - 1, column.width + 2, bar.height + 1},
                     colors->backdrop_bottom);
    Canvas fill = canvas_clipped(canvas, bar);
    canvas_fill_gradient(&fill, (Rect){column.x, column.y, column.width, horizon - column.y},
                         color_between(colors->text, colors->accent, 1, 2),
                         color_between(colors->backdrop_bottom, colors->accent, 1, 2));
    canvas_fill_rect(canvas, (Rect){column.x, top, column.width, 1}, colors->text);
}

static void reflection(const SunsetArt *art, Canvas *canvas, Rect column, int depth)
{
    const ThemeColors *colors = &art->theme->colors;
    const SunsetScene *scene = &art->scene;
    Color mirror = color_between(colors->backdrop_bottom, colors->accent, 1, 2);
    int floor = sunset_scene_floor(scene).height;
    for (int row = 1; row < depth; row += 2) {
        canvas_fill_rect(canvas, (Rect){column.x, scene->horizon + row, column.width, 1},
                         color_between(mirror, colors->background, row, floor));
    }
}

static void band(const SunsetArt *art, Canvas *canvas, int index)
{
    const SunsetScene *scene = &art->scene;
    Rect column = sunset_scene_bar(scene, index);
    Rect reach = {column.x - 1, scene->stage.y, column.width + 2, scene->stage.height};
    if (!touches(canvas, reach)) {
        return;
    }
    uint8_t level = art->meter->bars[index];
    int top = sunset_scene_bar_top(scene, level);
    if (top < scene->horizon - 1) {
        bar_body(art, canvas, column, top);
    }
    reflection(art, canvas, column, sunset_scene_reflection(scene, level));
    int cap = sunset_scene_bar_top(scene, art->meter->caps[index]);
    canvas_fill_rect(canvas, (Rect){column.x, cap - 3, column.width, 2},
                     art->theme->colors.accent_alt);
}

static void bars(const SunsetArt *art, Canvas *canvas)
{
    for (int index = 0; index < AUDIO_VISUAL_BANDS; index++) {
        band(art, canvas, index);
    }
}

void sunset_art_draw(const SunsetArt *art, Canvas *canvas)
{
    static const SunsetLayer SPECTRUM[SUNSET_LAYERS] = {sky, sun, grid, bars};
    static const SunsetLayer METER[SUNSET_LAYERS] = {sky, sun, grid, bed};
    const SunsetLayer *layers = art->sun_meter ? METER : SPECTRUM;
    Canvas stage = canvas_clipped(canvas, art->scene.stage);
    if (rect_is_empty(stage.clip)) {
        return;
    }
    for (int layer = 0; layer < SUNSET_LAYERS; layer++) {
        layers[layer](art, &stage);
    }
}
