#include "charge_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "home_art.h"
#include "ui/label.h"

enum {
    FULL_PERCENT = 100,
    READOUT_SCALE = 2,
    TEXT_CAPACITY = 64
};

typedef struct PowerInformation {
    HalBattery battery;
    const char *text;
} PowerInformation;

static int shown_percent(HalBattery battery)
{
    return battery.percent < FULL_PERCENT ? battery.percent : FULL_PERCENT;
}

static int larger(int first, int second)
{
    return first > second ? first : second;
}

static StringId power_state(HalBattery battery)
{
    if (!battery.usb_power) {
        return STR_POWER_BATTERY;
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
           before.usb_power != after.usb_power ||
           (after.known && (shown_percent(before) != shown_percent(after) ||
                            (!after.usb_power && before.millivolts != after.millivolts)));
}

static Rect screen_detail(const Theme *theme, const char *detail)
{
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * padding;
    int height =
        label_wrap_count(theme->fonts.small, detail, width) * font_line_height(theme->fonts.small);
    return (Rect){padding, theme->metrics.screen_height - padding - height, width, height};
}

ChargeScreenLayout charge_screen_layout(const Theme *theme, const char *detail)
{
    int pad = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * pad;
    int line = font_line_height(theme->fonts.title);
    int small = font_line_height(theme->fonts.small);
    int gauge = larger(1, 3 * theme->metrics.scrollbar_inset);
    int first = theme->metrics.header_height + pad;
    ChargeScreenLayout layout = {.header = {pad, 0, width, theme->metrics.header_height},
                                 .detail = screen_detail(theme, detail)};
    int bottom = layout.detail.y - pad;
    if (theme_is_portrait(theme)) {
        int group = READOUT_SCALE * line + 3 * pad + line + pad / 2 + small + gauge;
        int top = first + (bottom - first - group) / 2;
        layout.percent = (Rect){pad, top, width, READOUT_SCALE * line};
        layout.status = (Rect){pad, top + READOUT_SCALE * line + pad, width, line};
        layout.voltage = (Rect){pad, layout.status.y + line + pad / 2, width, small};
        layout.battery = (Rect){pad, layout.voltage.y + small + 2 * pad, width, gauge};
        return layout;
    }
    int side = (width - 2 * pad) / 2;
    int content = larger(READOUT_SCALE * line, line + small + pad / 2);
    int group = content + pad + gauge;
    int top = first + (bottom - first - group) / 2;
    int right = pad + side + 2 * pad;
    int status_top = top + (content - line - small - pad / 2) / 2;
    layout.percent =
        (Rect){pad, top + (content - READOUT_SCALE * line) / 2, side, READOUT_SCALE * line};
    layout.status = (Rect){right, status_top, side, line};
    layout.voltage = (Rect){right, status_top + line + pad / 2, side, small};
    layout.battery = (Rect){pad, top + content + pad, width, gauge};
    return layout;
}

static void draw_battery(Canvas *canvas, const Theme *theme, Rect area, HalBattery battery)
{
    canvas_fill_rect(canvas, area, theme->colors.divider);
    if (!battery.known) {
        return;
    }
    Color ink = battery.charging || shown_percent(battery) == FULL_PERCENT ? theme->colors.success
                                                                           : theme->colors.accent;
    Rect fill = area;
    fill.width = area.width * shown_percent(battery) / FULL_PERCENT;
    canvas_fill_rect(canvas, fill, ink);
    if (fill.width > 0) {
        canvas_fill_rect(canvas, (Rect){fill.x, fill.y, fill.width, 1}, theme->colors.accent);
    }
}

static void draw_line(Canvas *canvas, TextStyle style, Rect area, const char *text)
{
    label_draw(canvas, style, (Label){.area = area, .text = text});
}

static StringId usb_title(AppUsbView view)
{
    if (view == APP_USB_CONNECTING) {
        return STR_POWER_CONNECTING;
    }
    if (view == APP_USB_STORAGE) {
        return STR_POWER_STORAGE;
    }
    return view == APP_USB_DAC ? STR_USB_DAC_TITLE : STR_POWER_TITLE;
}

static void draw_header(Canvas *canvas, const Theme *theme, Rect area, AppUsbView view)
{
    Rect name = area;
    if (theme_is_portrait(theme)) {
        name.height = font_line_height(theme->fonts.header);
    }
    draw_line(canvas, (TextStyle){theme->fonts.header, theme->colors.text}, name,
              FIRMWARE_SHORT_NAME);
    const char *title = i18n_text(usb_title(view));
    int width = text_width(theme->fonts.small, title);
    Rect right = {area.x + area.width - width, area.y, width, area.height};
    if (theme_is_portrait(theme)) {
        right = (Rect){area.x, area.y + name.height, area.width, area.height - name.height};
    }
    draw_line(canvas, (TextStyle){theme->fonts.small, theme->colors.accent}, right, title);
    int cap = theme->metrics.padding;
    canvas_fill_rect(canvas, (Rect){area.x, area.y + area.height, area.width, 1},
                     theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){area.x, area.y + area.height, cap, 1}, theme->colors.accent);
    canvas_fill_rect(canvas, (Rect){area.x + area.width - cap, area.y + area.height, cap, 1},
                     theme->colors.accent_alt);
}

static void draw_scaled_level(Canvas *canvas, const Theme *theme, Rect area, const char *text)
{
    const Font *font = theme->fonts.title;
    int width = text_width(font, text);
    int scale = width * READOUT_SCALE <= area.width ? READOUT_SCALE : 1;
    int x = area.x + (area.width - width * scale) / 2;
    Canvas clipped = canvas_clipped(canvas, area);
    const char *cursor = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        const Glyph *glyph = font_find_glyph(font, codepoint);
        if (glyph != NULL) {
            int advance = glyph->advance * scale;
            text_draw_glyph_scaled(&clipped, (TextStyle){font, theme->colors.text},
                                   (Rect){x, area.y, advance, area.height}, codepoint, scale);
            x += advance;
        }
    }
}

static void draw_percent(Canvas *canvas, const Theme *theme, Rect area, HalBattery battery)
{
    char text[TEXT_CAPACITY];
    charge_screen_level_text(battery, text, sizeof text);
    if (battery.known) {
        draw_scaled_level(canvas, theme, area, text);
    } else {
        Rect centered = label_centered_area(theme->fonts.small, area, text);
        draw_line(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary}, centered,
                  text);
    }
}

static void draw_information(Canvas *canvas, const Theme *theme, Rect area,
                             PowerInformation information)
{
    HalBattery battery = information.battery;
    char voltage[TEXT_CAPACITY];
    const char *text = information.text;
    if (text == NULL && battery.usb_power) {
        if (power_state(battery) != STR_POWER_USB) {
            text = i18n_text(STR_POWER_USB);
        }
    } else if (text == NULL && battery.known && battery.millivolts != 0) {
        StringBuilder builder = string_builder_make(voltage, sizeof voltage);
        i18n_format_counts(&builder, STR_POWER_VOLTAGE, battery.millivolts, 0);
        text = voltage;
    }
    if (text != NULL) {
        if (theme_is_portrait(theme)) {
            area = label_centered_area(theme->fonts.small, area, text);
        }
        draw_line(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary}, area,
                  text);
    }
}

static void draw_power(Canvas *canvas, const Theme *theme, HalBattery battery, AppUsbStatus status)
{
    ChargeScreenLayout layout = charge_screen_layout(theme, status.detail);
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    int top = layout.battery.y + layout.battery.height + theme->metrics.padding;
    Rect horizon = {layout.battery.x, top, layout.battery.width,
                    layout.detail.y - theme->metrics.padding - top};
    if (!rect_is_empty(horizon)) {
        Canvas clipped = canvas_clipped(canvas, horizon);
        if (horizon.height >= 2 * font_line_height(theme->fonts.small)) {
            home_art_draw_backdrop(&clipped, horizon, theme);
        } else {
            canvas_fill_gradient(&clipped, horizon, theme->colors.backdrop_top,
                                 theme->colors.backdrop_bottom);
        }
    }
    draw_header(canvas, theme, layout.header, status.view);
    draw_battery(canvas, theme, layout.battery, battery);
    draw_percent(canvas, theme, layout.percent, battery);
    const char *text = i18n_text(power_state(battery));
    Rect state = theme_is_portrait(theme)
                     ? label_centered_area(theme->fonts.title, layout.status, text)
                     : layout.status;
    draw_line(canvas, (TextStyle){theme->fonts.title, theme->colors.text}, state, text);
    draw_information(canvas, theme, layout.voltage,
                     (PowerInformation){battery, status.information});
    canvas_fill_rect(
        canvas,
        (Rect){layout.detail.x, layout.detail.y - theme->metrics.padding, layout.detail.width, 1},
        theme->colors.divider);
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){layout.detail, status.detail});
}

void charge_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery)
{
    draw_power(canvas, theme, battery,
               (AppUsbStatus){.view = APP_USB_POWER, .detail = i18n_text(STR_HOLD_POWER)});
}

void usb_screen_draw(Canvas *canvas, const Theme *theme, HalBattery battery, AppUsbStatus status)
{
    draw_power(canvas, theme, battery, status);
}
