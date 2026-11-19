#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "playing_work.h"
#include "sunset_scene.h"

enum {
    SUNSET_LAYERS = 4
};

typedef struct SunsetArt {
    const Theme *theme;
    SunsetScene scene;
    const MeterState *meter;
    uint32_t now_ms;
    bool sun_meter;
} SunsetArt;

typedef void (*SunsetLayer)(const SunsetArt *art, Canvas *canvas);

void sunset_art_draw(const SunsetArt *art, Canvas *canvas);
bool sunset_art_peak_lit(const MeterState *meter, int channel, uint32_t now_ms);
