#include "charge_screen.h"

#include "core/i18n.h"
#include "core/seam.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "ui/header.h"
#include "ui/label.h"

enum {
    FULL_PERCENT = 100,
    TEXT_CAPACITY = 64
};

static int shown_percent(HalBattery battery)
{
    return battery.percent < FULL_PERCENT ? battery.percent : FULL_PERCENT;
}

static StringId power_state(HalBattery battery)
{
    if (!battery.usb_power) {
        return STR_POWER_BATTERY;
    }
    if (battery.charge_disabled) {
        return STR_CHARGE_DISABLED;
    }
    if (battery.charging) {
        return STR_CHARGING;
    }
    return battery.known && shown_percent(battery) == FULL_PERCENT ? STR_CHARGED : STR_POWER_USB;
}

void charge_screen_text(HalBattery battery, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append(&builder, i18n_text(power_state(battery)));
}

void charge_screen_level_text(HalBattery battery, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (battery.known) {
        i18n_format_counts(&builder, STR_POWER_PERCENT, (uint32_t)shown_percent(battery), 0);
    } else {
        string_builder_append(&builder, i18n_text(STR_POWER_UNKNOWN));
    }
}

bool app_usb_battery_changed(HalBattery before, HalBattery after)
{
    return before.known != after.known || before.charging != after.charging ||
           before.usb_power != after.usb_power || before.charge_disabled != after.charge_disabled ||
           (after.known && (shown_percent(before) != shown_percent(after) ||
                            (!after.usb_power && before.millivolts != after.millivolts)));
}

static StringId usb_title(AppUsbView view)
{
    if (view == APP_USB_CONNECTING) {
        return STR_POWER_CONNECTING;
    }
    if (view == APP_USB_STORAGE) {
        return STR_PLUG_COMPUTER;
    }
    return view == APP_USB_DAC ? STR_USB_DAC_TITLE : STR_POWER_TITLE;
}

const char *charge_screen_usb_title(AppUsbView view)
{
    return i18n_text(usb_title(view));
}

CORE_SEAM void usb_screen_draw_fallback(Canvas *canvas, const Theme *theme, HalBattery battery,
                                        AppUsbStatus status)
{
    Rect screen = {0, 0, theme->metrics.screen_width, theme->metrics.screen_height};
    canvas_fill_rect(canvas, screen, theme->colors.background);
    header_draw(canvas, theme, charge_screen_usb_title(status.view));
    int pad = theme->metrics.padding;
    int line = font_line_height(theme->fonts.body);
    Rect area = {pad, theme->metrics.header_height + pad, screen.width - 2 * pad, line};
    char text[TEXT_CAPACITY];
    charge_screen_text(battery, text, sizeof text);
    TextStyle style = {theme->fonts.body, theme->colors.text};
    label_draw(canvas, style, (Label){area, text});
    area.y += line + pad;
    area.height = screen.height - area.y - pad;
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){area, status.detail});
}

void charge_screen_draw_at(Canvas *canvas, const Theme *theme, HalBattery battery, uint32_t now_ms)
{
    charge_art_draw(canvas, theme, battery, now_ms);
}

void charge_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery)
{
    charge_screen_draw_at(canvas, theme, battery, hal_clock_milliseconds());
}
