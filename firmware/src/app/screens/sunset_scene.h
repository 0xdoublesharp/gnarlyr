#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/audio_visual.h"
#include "gfx/canvas.h"
#include "ui/theme.h"

enum {
    SUNSET_FLOOR_LINES = 6,
    SUNSET_RAYS = 6,
    SUNSET_MARKS = 4,
    SUNSET_STARS = 12,
    SUNSET_PULSE_STEPS = 4,
    SUNSET_FLASH_ROWS = 5
};

typedef struct SunsetScene {
    Rect stage;
    int horizon;
    Point sun;
    int radius;
    Rect channels[2];
    Rect peaks[2];
    Rect bars;
} SunsetScene;

SunsetScene sunset_scene_of(const Theme *theme, Rect stage);
int sunset_scene_top(const SunsetScene *scene);
int sunset_scene_half_width(const SunsetScene *scene, int y);
bool sunset_scene_slat_gap(const SunsetScene *scene, int y);
int sunset_scene_lit_top(const SunsetScene *scene, uint8_t level);
Rect sunset_scene_half(const SunsetScene *scene, int side);
int sunset_scene_mark(const SunsetScene *scene, int mark);
int sunset_scene_floor_line(const SunsetScene *scene, int line);
Rect sunset_scene_floor(const SunsetScene *scene);
Rect sunset_scene_bar(const SunsetScene *scene, int band);
int sunset_scene_bar_top(const SunsetScene *scene, uint8_t level);
int sunset_scene_reflection(const SunsetScene *scene, uint8_t level);
Point sunset_scene_star(const SunsetScene *scene, int star);
uint8_t sunset_scene_pulse(uint8_t previous, uint8_t energy);
