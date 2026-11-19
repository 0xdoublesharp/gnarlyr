#include "sunset_art.h"

#include "core/i18n.h"
#include "ui/label.h"

enum {
    SUNSET_CHROME_BANDS = 6,
    SUNSET_RAY_START = 8,
    SUNSET_TICK = 4
};

typedef struct ChromeText {
    const Font *font;
    Rect area;
    const char *text;
    Color edge;
} ChromeText;

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

static Color sun_hot(const SunsetArt *art, int y)
{
    const ThemeColors *colors = &art->theme->colors;
    int top = sunset_scene_top(&art->scene);
    return color_between(color_between(colors->accent_alt, colors->text, 1, 2),
                         color_between(colors->accent_alt, colors->danger, 1, 2), y - top,
                         art->scene.horizon - top);
}

static Color half_ink(const SunsetArt *art, int side, int y, Color hot)
{
    const SunsetScene *scene = &art->scene;
    if (sunset_art_peak_lit(art->meter, side, art->now_ms) &&
        y < sunset_scene_top(scene) + SUNSET_FLASH_ROWS) {
        return art->theme->colors.text;
    }
    bool lit = y >= sunset_scene_lit_top(scene, art->meter->shown[side]);
    return lit ? hot : color_between(sky_at(art, y), hot, 1, 4);
}

typedef void (*SunsetRow)(const SunsetArt *art, Canvas *canvas, int y, int half);

static void disc_row(const SunsetArt *art, Canvas *canvas, int y, int half)
{
    canvas_fill_rect(canvas, (Rect){art->scene.sun.x - half, y, half * 2, 1}, sun_hot(art, y));
}

static void meter_row(const SunsetArt *art, Canvas *canvas, int y, int half)
{
    int x = art->scene.sun.x;
    Color hot = sun_hot(art, y);
    canvas_fill_rect(canvas, (Rect){x - half, y, half - 1, 1}, half_ink(art, 0, y, hot));
    canvas_fill_rect(canvas, (Rect){x + 2, y, half - 1, 1}, half_ink(art, 1, y, hot));
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

static void marks(const SunsetArt *art, Canvas *canvas)
{
    const ThemeColors *colors = &art->theme->colors;
    const SunsetScene *scene = &art->scene;
    Color tick = color_between(colors->backdrop_bottom, colors->text_secondary, 1, 2);
    for (int mark = 0; mark < SUNSET_MARKS; mark++) {
        int y = sunset_scene_mark(scene, mark);
        int half = sunset_scene_half_width(scene, y);
        canvas_fill_rect(canvas, (Rect){scene->sun.x - half - 6, y, SUNSET_TICK, 1}, tick);
        canvas_fill_rect(canvas, (Rect){scene->sun.x + half + 3, y, SUNSET_TICK, 1}, tick);
    }
}

static void sun(const SunsetArt *art, Canvas *canvas)
{
    sun_rows(art, canvas, disc_row);
}

static void sun_meter(const SunsetArt *art, Canvas *canvas)
{
    if (sun_rows(art, canvas, meter_row)) {
        marks(art, canvas);
    }
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
}

static void flat_text(Canvas *canvas, const Font *font, Rect area, const char *text, Color color)
{
    label_draw(canvas, (TextStyle){font, color, false}, (Label){area, text});
}

static void chrome_text(Canvas *canvas, const ThemeColors *colors, ChromeText chrome)
{
    Rect area = chrome.area;
    Color edge = chrome.edge;
    Color ramp[SUNSET_CHROME_BANDS] = {colors->text,
                                       color_between(colors->text, edge, 1, 3),
                                       color_between(colors->text, edge, 2, 3),
                                       color_between(colors->backdrop_bottom, edge, 1, 3),
                                       edge,
                                       color_between(edge, colors->text, 1, 2)};
    Rect shadow = {area.x + 1, area.y + 1, area.width, area.height};
    flat_text(canvas, chrome.font, shadow, chrome.text, colors->backdrop_bottom);
    int top = area.y + area.height / 6;
    int span = area.height * 2 / 3;
    for (int band = 0; band < SUNSET_CHROME_BANDS; band++) {
        int from = band == 0 ? area.y : top + span * band / SUNSET_CHROME_BANDS;
        int to = band == SUNSET_CHROME_BANDS - 1 ? area.y + area.height + 1
                                                 : top + span * (band + 1) / SUNSET_CHROME_BANDS;
        Canvas strip = canvas_clipped(canvas, (Rect){area.x - 1, from, area.width + 2, to - from});
        flat_text(&strip, chrome.font, area, chrome.text, ramp[band]);
    }
}

static void labels(const SunsetArt *art, Canvas *canvas)
{
    const Theme *theme = art->theme;
    const ThemeColors *colors = &theme->colors;
    for (int side = 0; side < 2; side++) {
        Rect channel = art->scene.channels[side];
        Rect peak = art->scene.peaks[side];
        if (touches(canvas,
                    (Rect){channel.x - 1, channel.y, channel.width + 2, channel.height + 1})) {
            const char *text = i18n_text(side == 0 ? STR_METER_LEFT : STR_METER_RIGHT);
            chrome_text(canvas, colors,
                        (ChromeText){theme->fonts.title, channel, text, colors->accent});
        }
        if (!touches(canvas, (Rect){peak.x - 1, peak.y, peak.width + 2, peak.height + 1})) {
            continue;
        }
        const char *text = i18n_text(STR_METER_PEAK);
        if (sunset_art_peak_lit(art->meter, side, art->now_ms)) {
            chrome_text(canvas, colors,
                        (ChromeText){theme->fonts.small, peak, text, colors->danger});
        } else {
            flat_text(canvas, theme->fonts.small, peak, text,
                      color_between(colors->backdrop_bottom, colors->text, 1, 3));
        }
    }
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
    static const SunsetLayer METER[SUNSET_LAYERS] = {sky, sun_meter, grid, labels};
    const SunsetLayer *layers = art->sun_meter ? METER : SPECTRUM;
    Canvas stage = canvas_clipped(canvas, art->scene.stage);
    if (rect_is_empty(stage.clip)) {
        return;
    }
    for (int layer = 0; layer < SUNSET_LAYERS; layer++) {
        layers[layer](art, &stage);
    }
}
