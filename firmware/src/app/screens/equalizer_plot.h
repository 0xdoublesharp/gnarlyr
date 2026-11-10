#pragma once

#include <stddef.h>

#include "gfx/canvas.h"
#include "settings/equalizer_response.h"
#include "ui/theme.h"

typedef struct EqualizerPlotView {
    const EqualizerResponse *response;
    int selected_band;
    bool adjusting;
} EqualizerPlotView;

typedef struct EqualizerPlotLayout {
    Rect list;
    Rect primary;
    Rect secondary;
    Rect plot;
    Rect preamp;
    Rect reference;
    Rect hint;
} EqualizerPlotLayout;

const char *equalizer_plot_band_name(int band);
void equalizer_plot_gain(char *text, size_t capacity, int tenth_db);
EqualizerPlotLayout equalizer_plot_layout(const Theme *theme, const char *hint, bool browser);
void equalizer_plot_draw(Canvas *canvas, const Theme *theme, Rect area, EqualizerPlotView view);
void equalizer_plot_caption(Canvas *canvas, const Theme *theme, Rect area,
                            const EqualizerResponse *response);
