#pragma once

#include "charge_screen.h"
#include "core/string_builder.h"
#include "gfx/text.h"
#include "usb_scene.h"

enum {
    USB_SCREEN_TEXT_CAPACITY = 64,
    USB_SCREEN_NUMBER_CAPACITY = 16
};

typedef struct UsbPainter {
    Canvas *canvas;
    const Theme *theme;
    HalBattery battery;
    AppUsbStatus status;
    const UsbScene *scene;
    UsbScreenLayout layout;
} UsbPainter;

const UsbScene *usb_screen_scene(AppUsbStatus status);
Color usb_screen_tint(Color color, Color toward);
Color usb_screen_lamp_color(const Theme *theme, const UsbScene *scene);
void usb_screen_text(const UsbPainter *painter, Rect area, TextStyle style, const char *text);
void usb_screen_row(const UsbPainter *painter, Rect row, const char *name, const char *value);
int usb_screen_draw_state(const UsbPainter *painter, const char *text);
void usb_screen_append_tenths(StringBuilder *builder, uint32_t tenths);
void usb_screen_draw_frame(const UsbPainter *painter, const char *title);
Rect usb_screen_lines_below(const UsbPainter *painter, int below);
bool usb_screen_touches(const UsbPainter *painter, Rect area);
void usb_screen_draw_dac(const UsbPainter *painter);
void usb_screen_draw_power(const UsbPainter *painter);
