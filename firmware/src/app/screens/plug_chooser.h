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

typedef struct PlugChooser {
    Screen screen;
    PlugChooserConfig config;
    PlugChoices choices;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint32_t until_ms;
    uint32_t seconds;
    bool timed;
    bool expired;
    bool redraw;
    char countdown[PLUG_COUNTDOWN_CAPACITY];
} PlugChooser;

typedef struct PlugChooserLayout {
    Rect art;
    Rect name;
    Rect line;
    Rect footer;
    Rect list;
} PlugChooserLayout;

Screen *plug_chooser_init(PlugChooser *chooser, PlugChooserConfig config);
PlugMode plug_chooser_mode(const PlugChooser *chooser, int row);
const char *plug_chooser_line(const PlugChooser *chooser);
int plug_chooser_footer_rows(const PlugChooser *chooser);
PlugChooserLayout plug_chooser_layout(const Theme *theme, int footer_rows, int rows);
void plug_art_draw(Canvas *canvas, const Theme *theme, Rect area, PlugMode mode);
