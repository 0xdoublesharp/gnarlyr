#include "sunset_scene.h"

#include "core/i18n.h"
#include "gfx/text.h"

enum {
    SUNSET_ARC_STEPS = 32,
    SUNSET_SLAT_PITCH = 9,
    SUNSET_INSET = 6,
    SUNSET_MARK_ROOM = 12,
    SUNSET_STAR_TOP = 3,
    SUNSET_STAR_GAP = 4
};

static const uint8_t ARC[SUNSET_ARC_STEPS + 1] = {
    255, 255, 255, 254, 253, 252, 250, 249, 247, 245, 242, 239, 236, 233, 229, 225, 221,
    216, 211, 205, 199, 192, 185, 177, 169, 159, 149, 137, 123, 108, 89,  63,  0};
static const uint8_t MARK_SHIFTS[SUNSET_MARKS] = {0, 1, 2, 4};
static const uint8_t STARS[SUNSET_STARS][2] = {{8, 40},   {31, 200},  {52, 90},   {77, 10},
                                               {98, 160}, {121, 60},  {139, 230}, {162, 120},
                                               {184, 20}, {203, 180}, {226, 70},  {247, 140}};

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static int larger(int first, int second)
{
    return first > second ? first : second;
}

static Rect text_box(const Font *font, int x, int y, StringId text)
{
    return (Rect){x, y, text_width(font, i18n_text(text)), font_line_height(font)};
}

static Rect aligned(Rect box, const SunsetScene *scene, int side)
{
    box.x = side == 0 ? scene->stage.x + SUNSET_INSET
                      : scene->stage.x + scene->stage.width - SUNSET_INSET - box.width;
    return box;
}

static void place_labels(SunsetScene *scene, const Theme *theme, bool portrait)
{
    const Font *small = theme->fonts.small;
    const Font *title = theme->fonts.title;
    int peak_y = portrait ? scene->stage.y + SUNSET_INSET
                          : scene->horizon - font_line_height(title) - font_line_height(small) - 6;
    for (int side = 0; side < 2; side++) {
        Rect peak = aligned(text_box(small, 0, peak_y, STR_METER_PEAK), scene, side);
        Rect channel = text_box(title, 0, peak.y + peak.height + 2,
                                side == 0 ? STR_METER_LEFT : STR_METER_RIGHT);
        scene->peaks[side] = peak;
        scene->channels[side] = aligned(channel, scene, side);
    }
}

SunsetScene sunset_scene_of(const Theme *theme, Rect stage)
{
    bool portrait = theme_is_portrait(theme);
    SunsetScene scene = {.stage = stage};
    scene.horizon = stage.y + stage.height * (portrait ? 64 : 66) / 100;
    int above = scene.horizon - stage.y;
    scene.radius = portrait ? smaller(stage.width / 2 - SUNSET_MARK_ROOM, above * 72 / 100)
                            : smaller(stage.width * 22 / 100, above * 80 / 100);
    scene.sun = (Point){stage.x + stage.width / 2, scene.horizon - scene.radius * 3 / 10};
    scene.bars = (Rect){stage.x + SUNSET_INSET, stage.y + SUNSET_INSET,
                        stage.width - SUNSET_INSET * 2, above - SUNSET_INSET};
    place_labels(&scene, theme, portrait);
    return scene;
}

int sunset_scene_top(const SunsetScene *scene)
{
    return scene->sun.y - scene->radius;
}

int sunset_scene_half_width(const SunsetScene *scene, int y)
{
    int distance = y - scene->sun.y;
    distance = distance < 0 ? -distance : distance;
    if (y >= scene->horizon || distance >= scene->radius) {
        return -1;
    }
    return scene->radius * ARC[distance * SUNSET_ARC_STEPS / scene->radius] / 255;
}

bool sunset_scene_slat_gap(const SunsetScene *scene, int y)
{
    int rise = scene->horizon - 1 - y;
    if (rise < 0 || rise >= scene->radius) {
        return false;
    }
    int gap = 1 + (scene->radius - rise) * 4 / scene->radius;
    return rise % SUNSET_SLAT_PITCH < gap;
}

int sunset_scene_lit_top(const SunsetScene *scene, uint8_t level)
{
    return scene->horizon - level * (scene->horizon - sunset_scene_top(scene)) / 255;
}

Rect sunset_scene_half(const SunsetScene *scene, int side)
{
    int top = sunset_scene_top(scene);
    int x = side == 0 ? scene->sun.x - scene->radius : scene->sun.x + 2;
    return (Rect){x, top, scene->radius - 1, scene->horizon - top};
}

int sunset_scene_mark(const SunsetScene *scene, int mark)
{
    return sunset_scene_lit_top(scene, audio_visual_db(UINT32_C(32768) >> MARK_SHIFTS[mark]));
}

Rect sunset_scene_floor(const SunsetScene *scene)
{
    return (Rect){scene->stage.x, scene->horizon, scene->stage.width,
                  scene->stage.y + scene->stage.height - scene->horizon};
}

int sunset_scene_floor_line(const SunsetScene *scene, int line)
{
    int span = sunset_scene_floor(scene).height - 4;
    int steps = (SUNSET_FLOOR_LINES + 1) * (SUNSET_FLOOR_LINES + 1);
    return scene->horizon + 3 + (line + 1) * (line + 1) * span / steps;
}

Rect sunset_scene_bar(const SunsetScene *scene, int band)
{
    Rect area = scene->bars;
    int pitch = area.width / AUDIO_VISUAL_BANDS;
    int width = pitch - (pitch > 10 ? 4 : 2);
    int x = area.x + band * pitch + (area.width - pitch * AUDIO_VISUAL_BANDS) / 2;
    return (Rect){x, area.y, width, area.height};
}

int sunset_scene_bar_top(const SunsetScene *scene, uint8_t level)
{
    return scene->horizon - 1 - level * (scene->bars.height - 6) / 255;
}

int sunset_scene_reflection(const SunsetScene *scene, uint8_t level)
{
    return (scene->horizon - 1 - sunset_scene_bar_top(scene, level)) / 3;
}

static Rect star_band(const SunsetScene *scene)
{
    int left = scene->stage.x + SUNSET_INSET;
    int right = scene->stage.x + scene->stage.width - SUNSET_INSET;
    int top = scene->stage.y + SUNSET_STAR_TOP;
    if (scene->peaks[0].y < sunset_scene_top(scene)) {
        int labels = larger(scene->peaks[0].width, scene->channels[0].width);
        left += labels + SUNSET_INSET;
        right -= larger(scene->peaks[1].width, scene->channels[1].width) + SUNSET_INSET;
    }
    return (Rect){left, top, right - left, sunset_scene_top(scene) - SUNSET_STAR_GAP - top};
}

Point sunset_scene_star(const SunsetScene *scene, int star)
{
    Rect band = star_band(scene);
    return (Point){band.x + STARS[star][0] * (band.width - 1) / 255,
                   band.y + STARS[star][1] * (band.height - 1) / 255};
}

uint8_t sunset_scene_pulse(uint8_t previous, uint8_t energy)
{
    uint8_t target = energy >= 224 ? 3 : energy >= 200 ? 2 : energy >= 176 ? 1 : 0;
    uint8_t decayed = previous > 0 ? (uint8_t)(previous - 1) : 0;
    return target > decayed ? target : decayed;
}
