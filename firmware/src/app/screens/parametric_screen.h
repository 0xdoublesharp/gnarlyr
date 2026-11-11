#pragma once

#include "equalizer_screen.h"
#include "settings/parametric_file.h"
#include "ui/navigation.h"

typedef struct ParametricScreenConfig {
    Settings *settings;
    bool (*commit)(void *context, const ParametricEq *eq, int slot);
    void (*preview)(void *context, const SettingsEqualizerCurve *curve);
    void *context;
    const Theme *theme;
    int slot;
    const ParametricBank *bank;
    bool (*shape_commit)(void *context, const ParametricEq *eq, const SoundShaping *shape,
                         int slot);
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
    SoundShaping shaping;
    SoundShaping kept_shaping;
    ParametricEq base;
    int base_slot;
    int kept_base_slot;
    StringId notice;
    bool shaping_mode;
    bool shaping_requested;
} ParametricScreen;

Screen *parametric_screen_init(ParametricScreen *editor, ParametricScreenConfig config);
void sound_shaping_screen_enter(ParametricScreen *editor);
ScreenAction sound_shaping_screen_key(ParametricScreen *editor, NavigationIntent intent);
void sound_shaping_screen_texts(ParametricScreen *editor);
Rect sound_shaping_screen_slider(const ParametricScreen *editor, Canvas *canvas, Rect area);
