#pragma once

#include <stddef.h>

#include "app/app.h"
#include "gfx/canvas.h"
#include "hal/power.h"
#include "ui/theme.h"

enum {
    CHARGE_SCREEN_FRAME_MS = 1000,
    CHARGE_SCREEN_PHASES = 8
};

typedef struct ChargeScreenLayout {
    Rect header;
    Rect battery;
    Rect percent;
    Rect status;
    Rect voltage;
    Rect detail;
} ChargeScreenLayout;

ChargeScreenLayout charge_screen_art_layout(const Theme *theme);
void usb_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery, AppUsbStatus status);
void usb_screen_draw_fallback(Canvas *canvas, const Theme *theme, HalBattery battery,
                              AppUsbStatus status);
const char *charge_screen_usb_title(AppUsbView view);
void charge_art_observe(HalBattery battery, uint32_t now_ms);
void charge_art_draw(Canvas *canvas, const Theme *theme, HalBattery battery, uint32_t now_ms);
void charge_screen_draw_at(Canvas *canvas, const Theme *theme, HalBattery battery, uint32_t now_ms);
void charge_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery);
void charge_screen_text(HalBattery battery, char *text, size_t capacity);
void charge_screen_level_text(HalBattery battery, char *text, size_t capacity);
