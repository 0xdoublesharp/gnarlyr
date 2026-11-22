#pragma once

#include "ui/menu_screen.h"

typedef struct AboutBand {
    Rect band;
    Rect mark;
    Rect name;
} AboutBand;

AboutBand about_layout_band(const Theme *theme, const char *name);
void about_layout_attach(MenuScreen *menu);
void about_layout_check_fonts(const Theme *theme);
