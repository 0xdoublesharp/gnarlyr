#pragma once

#include "equalizer_screen.h"

typedef struct ParametricScreenConfig {
    Settings *settings;
    bool (*commit)(void *context, const ParametricEq *eq, int slot);
    void (*preview)(void *context, const SettingsEqualizerCurve *curve);
    void *context;
    const Theme *theme;
    int slot;
} ParametricScreenConfig;

typedef struct ParametricScreen {
    Screen screen;
    ParametricScreenConfig config;
    ParametricEq working;
    ParametricEq kept;
    EqualizerResponse response;
    ListModel model;
    ListView list;
    char (*labels)[48];
    char (*details)[48];
    uint32_t now_ms;
    int filter;
    int slot;
    bool adjusting;
    bool previewing;
} ParametricScreen;

Screen *parametric_screen_init(ParametricScreen *editor, ParametricScreenConfig config);
