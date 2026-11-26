#include "sunset_scene.h"

#include <stddef.h>

enum {
    SUNSET_ARC_STEPS = 32,
    SUNSET_SLAT_PITCH = 9,
    SUNSET_SLATS_AUTO = UINT8_MAX,
    SUNSET_INSET = 6,
    SUNSET_MARK_ROOM = 12,
    SUNSET_STAR_TOP = 3,
    SUNSET_STAR_GAP = 4,
    SUNSET_BED_INSET = 8,
    SUNSET_BED_LIFT = 20,
    SUNSET_BED_PAD = 4,
    SUNSET_ROW_HEIGHT = 18,
    SUNSET_LABEL_WIDTH = 16,
    SUNSET_SEGMENT_GAP = 2,
    SUNSET_SEGMENT_HEIGHT = 8
};

static const uint8_t ARC[SUNSET_ARC_STEPS + 1] = {
    255, 255, 255, 254, 253, 252, 250, 249, 247, 245, 242, 239, 236, 233, 229, 225, 221,
    216, 211, 205, 199, 192, 185, 177, 169, 159, 149, 137, 123, 108, 89,  63,  0};
static const uint8_t RIDGE[SUNSET_RIDGE_POINTS][2] = {
    {0, 2},   {23, 8},  {34, 6},   {52, 13},  {73, 6},   {91, 8},  {110, 2}, {144, 2},
    {162, 7}, {175, 6}, {193, 13}, {207, 10}, {226, 17}, {245, 9}, {255, 8}};
static const uint8_t STARS[SUNSET_STARS][2] = {{8, 40},   {31, 200},  {52, 90},   {77, 10},
                                               {98, 160}, {121, 60},  {139, 230}, {162, 120},
                                               {184, 20}, {203, 180}, {226, 70},  {247, 140}};

static int smaller(int first, int second)
{
    return first < second ? first : second;
}

static int scene_value(const Theme *theme, ThemeSceneTarget target, int fallback)
{
    const ThemeVisualStyle *style = theme->visual_style;
    return style != NULL && (style->scene_mask & (1u << target)) != 0 ? style->scene[target]
                                                                      : fallback;
}

SunsetScene sunset_scene_of(const Theme *theme, Rect stage)
{
    bool portrait = theme_is_portrait(theme);
    SunsetScene scene = {.stage = stage};
    int horizon = scene_value(theme, THEME_SCENE_HORIZON, portrait ? 64 : 66);
    scene.horizon = stage.y + stage.height * horizon / 100;
    int above = scene.horizon - stage.y;
    int radius = scene_value(theme, THEME_SCENE_SUN_RADIUS, 0);
    scene.radius = portrait ? smaller(stage.width / 2 - SUNSET_MARK_ROOM, above * 72 / 100)
                            : smaller(stage.width * 22 / 100, above * 80 / 100);
    if (radius != 0) {
        int room = above - SUNSET_STAR_TOP - SUNSET_STAR_GAP - 2;
        scene.radius = smaller(stage.width * radius / 100, room * 10 / 13);
    }
    scene.slats = (uint8_t)scene_value(theme, THEME_SCENE_SLATS, SUNSET_SLATS_AUTO);
    scene.floor_lines = (uint8_t)scene_value(theme, THEME_SCENE_FLOOR_LINES, SUNSET_FLOOR_LINES);
    scene.rays = (uint8_t)scene_value(theme, THEME_SCENE_RAYS, SUNSET_RAYS);
    scene.pulse = scene_value(theme, THEME_SCENE_PULSE, 1) != 0;
    scene.sun = (Point){stage.x + stage.width / 2, scene.horizon - scene.radius * 3 / 10};
    scene.bars = (Rect){stage.x + SUNSET_INSET, stage.y + SUNSET_INSET,
                        stage.width - SUNSET_INSET * 2, above - SUNSET_INSET};
    int bed = 2 * SUNSET_ROW_HEIGHT + SUNSET_BED_PAD;
    scene.bed = (Rect){stage.x + SUNSET_BED_INSET, stage.y + stage.height - SUNSET_BED_LIFT - bed,
                       stage.width - 2 * SUNSET_BED_INSET, bed};
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
    if (rise < 0 || rise >= scene->radius || scene->slats == 0) {
        return false;
    }
    int gap = 1 + (scene->radius - rise) * 4 / scene->radius;
    if (scene->slats == SUNSET_SLATS_AUTO) {
        return rise % SUNSET_SLAT_PITCH < gap;
    }
    int band = rise * scene->slats / scene->radius;
    int start = (band * scene->radius + scene->slats - 1) / scene->slats;
    int end = ((band + 1) * scene->radius + scene->slats - 1) / scene->slats;
    return rise - start < smaller(gap, (end - start) / 2);
}

Rect sunset_scene_row(const SunsetScene *scene, int side)
{
    Rect bed = scene->bed;
    return (Rect){bed.x, bed.y + SUNSET_BED_PAD / 2 + side * SUNSET_ROW_HEIGHT, bed.width,
                  SUNSET_ROW_HEIGHT};
}

Rect sunset_scene_segment(const SunsetScene *scene, int side, int segment)
{
    Rect row = sunset_scene_row(scene, side);
    int left = row.x + SUNSET_BED_PAD + SUNSET_LABEL_WIDTH + SUNSET_BED_PAD;
    int room = row.x + row.width - SUNSET_BED_PAD - left + SUNSET_SEGMENT_GAP;
    int pitch = room / SUNSET_SEGMENTS;
    return (Rect){left + segment * pitch, row.y + (row.height - SUNSET_SEGMENT_HEIGHT) / 2,
                  pitch - SUNSET_SEGMENT_GAP, SUNSET_SEGMENT_HEIGHT};
}

int sunset_scene_lit(uint8_t level)
{
    return (level * SUNSET_SEGMENTS + 127) / 255;
}

Point sunset_scene_ridge(const SunsetScene *scene, int point)
{
    Rect stage = scene->stage;
    int rise = (scene->horizon - stage.y) * RIDGE[point][1] / 59;
    return (Point){stage.x + RIDGE[point][0] * (stage.width - 1) / 255, scene->horizon - 1 - rise};
}

Rect sunset_scene_floor(const SunsetScene *scene)
{
    return (Rect){scene->stage.x, scene->horizon, scene->stage.width,
                  scene->stage.y + scene->stage.height - scene->horizon};
}

int sunset_scene_floor_line(const SunsetScene *scene, int line)
{
    int span = sunset_scene_floor(scene).height - 4;
    int steps = (scene->floor_lines + 1) * (scene->floor_lines + 1);
    int offset = (line + 1) * (line + 1) * span / steps;
    return scene->horizon + 3 + (offset > line ? offset : line);
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
