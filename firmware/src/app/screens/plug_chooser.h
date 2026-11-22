#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "plug_modes.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/theme.h"

enum {
    PLUG_PROMPT_MS = 15000,
    PLUG_COUNTDOWN_CAPACITY = 40
};

typedef struct PlugChooserConfig {
    PlugState state;
    const char *title;
    void (*choose)(void *context, PlugMode mode, bool always);
    void *context;
    const Theme *theme;
} PlugChooserConfig;

typedef struct PlugChooserLayout {
    Rect title;
    Rect art;
    Rect cue;
    Rect list;
    Rect line;
    Rect footer;
    int column;
} PlugChooserLayout;

typedef struct PlugChooser {
    Screen screen;
    PlugChooserConfig config;
    PlugChoices choices;
    ListModel model;
    ListView list;
    PlugChooserLayout layout;
    uint32_t now_ms;
    uint32_t until_ms;
    uint32_t seconds;
    int row_width;
    uint8_t heights[PLUG_MODE_COUNT];
    bool timed;
    bool paused;
    bool expired;
    bool redraw;
    char countdown[PLUG_COUNTDOWN_CAPACITY];
} PlugChooser;

Screen *plug_chooser_init(PlugChooser *chooser, PlugChooserConfig config);
PlugMode plug_chooser_mode(const PlugChooser *chooser, int row);
const char *plug_chooser_line(const PlugChooser *chooser);
bool plug_chooser_has_footer(const PlugChooser *chooser);
int plug_chooser_shown_rows(const PlugChooser *chooser);
void plug_art_draw(Canvas *canvas, const Theme *theme, Rect area, PlugMode mode);
