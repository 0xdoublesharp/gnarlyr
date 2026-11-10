#pragma once

#include <stddef.h>

#include "app/app.h"
#include "gfx/canvas.h"
#include "hal/power.h"
#include "ui/theme.h"

typedef struct ChargeScreenLayout {
    Rect header;
    Rect battery;
    Rect percent;
    Rect status;
    Rect voltage;
    Rect detail;
} ChargeScreenLayout;

ChargeScreenLayout charge_screen_layout(const Theme *theme, const char *detail);
void usb_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery, AppUsbStatus status);
void charge_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery);
void charge_screen_text(HalBattery battery, char *text, size_t capacity);
