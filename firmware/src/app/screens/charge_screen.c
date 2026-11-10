#include "charge_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "ui/label.h"

enum {
    FULL_PERCENT = 100,
    BATTERY_SEGMENTS = 10,
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

static ChargeScreenLayout landscape_layout(const Theme *theme, const char *detail)
{
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * padding;
    int top = theme->metrics.header_height + padding;
    int left = width / 3;
    int line = font_line_height(theme->fonts.title);
    int right = padding + left + 2 * padding;
    return (ChargeScreenLayout){
        .header = {padding, 0, width, theme->metrics.header_height},
        .percent = {padding, top, left, line},
        .battery = {padding, top + line + padding, left - padding, theme->metrics.row_height},
        .status = {right, top, width - left - 2 * padding, line},
        .voltage = {right, top + line + padding, width - left - 2 * padding,
                    theme->metrics.row_height},
        .detail = screen_detail(theme, detail)};
}

ChargeScreenLayout charge_screen_layout(const Theme *theme, const char *detail)
{
    ChargeScreenLayout layout = landscape_layout(theme, detail);
    if (!theme_is_portrait(theme)) {
        return layout;
    }
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * padding;
    int top = theme->metrics.header_height + 2 * padding;
    int line = font_line_height(theme->fonts.title);
    layout.percent = (Rect){padding, top, width, line};
    layout.battery = (Rect){2 * padding, top + line + padding, width - 3 * padding,
                            2 * theme->metrics.row_height};
    layout.status =
        (Rect){padding, layout.battery.y + layout.battery.height + 2 * padding, width, line};
    layout.voltage =
        (Rect){padding, layout.status.y + line + padding, width, theme->metrics.row_height};
    return layout;
}

static void draw_outline(Canvas *canvas, Rect body, int edge, Color color)
{
    canvas_fill_rect(canvas, (Rect){body.x, body.y, body.width, edge}, color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y + body.height - edge, body.width, edge}, color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y, edge, body.height}, color);
    canvas_fill_rect(canvas, (Rect){body.x + body.width - edge, body.y, edge, body.height}, color);
}

static void draw_battery(Canvas *canvas, const Theme *theme, Rect body, HalBattery battery)
{
    int edge = theme->metrics.scrollbar_inset;
    int inset = 2 * edge;
    Rect inner = {body.x + inset, body.y + inset, body.width - 2 * inset, body.height - 2 * inset};
    canvas_fill_rect(canvas, (Rect){body.x + edge, body.y + edge, body.width, body.height},
                     theme->colors.divider);
    canvas_fill_gradient(canvas, body, theme->colors.backdrop_top, theme->colors.backdrop_bottom);
    draw_outline(canvas, body, edge, theme->colors.accent);
    Rect nub = {body.x + body.width, body.y + body.height / 3, 2 * edge, body.height / 3};
    canvas_fill_rect(canvas, nub, theme->colors.accent);
    int fill = battery.known ? inner.width * shown_percent(battery) / FULL_PERCENT : 0;
    Color ink =
        battery.usb_power && battery.charging ? theme->colors.success : theme->colors.accent;
    canvas_fill_rect(canvas, (Rect){inner.x, inner.y, fill, inner.height}, ink);
    for (int segment = 1; segment < BATTERY_SEGMENTS; segment++) {
        canvas_fill_rect(
            canvas,
            (Rect){inner.x + inner.width * segment / BATTERY_SEGMENTS, inner.y, edge, inner.height},
            theme->colors.background);
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
    return view == APP_USB_DAC ? STR_USB_DAC_TITLE : STR_POWER_USB;
}

static void draw_header(Canvas *canvas, const Theme *theme, Rect area, AppUsbView view)
{
    TextStyle brand = {.font = theme->fonts.header, .color = theme->colors.text};
    Rect name = area;
    if (theme_is_portrait(theme)) {
        name.height = font_line_height(theme->fonts.header);
    }
    draw_line(canvas, brand, name, FIRMWARE_SHORT_NAME);
    const char *title = i18n_text(usb_title(view));
    int width = text_width(theme->fonts.small, title);
    Rect right = {area.x + area.width - width, area.y, width, area.height};
    if (theme_is_portrait(theme)) {
        right = (Rect){area.x, area.y + name.height, area.width, area.height - name.height};
    }
    draw_line(canvas, (TextStyle){theme->fonts.small, theme->colors.accent}, right, title);
    canvas_fill_rect(canvas, (Rect){area.x, area.y + area.height, area.width, 1},
                     theme->colors.divider);
}

static void draw_percent(Canvas *canvas, const Theme *theme, Rect area, HalBattery battery)
{
    char text[TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    if (battery.known) {
        i18n_format_counts(&builder, STR_POWER_PERCENT, (uint32_t)shown_percent(battery), 0);
    } else {
        string_builder_append(&builder, i18n_text(STR_POWER_UNKNOWN));
    }
    const Font *font = battery.known ? theme->fonts.title : theme->fonts.small;
    Rect centered = label_centered_area(font, area, text);
    draw_line(canvas, (TextStyle){font, theme->colors.text}, centered, text);
}

static void draw_voltage(Canvas *canvas, const Theme *theme, Rect area, HalBattery battery)
{
    if (!battery.known || battery.usb_power || battery.millivolts == 0) {
        return;
    }
    char text[TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_POWER_VOLTAGE, battery.millivolts, 0);
    if (theme_is_portrait(theme)) {
        area = label_centered_area(theme->fonts.small, area, text);
    }
    draw_line(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary}, area, text);
}

static void draw_power(Canvas *canvas, const Theme *theme, HalBattery battery, AppUsbStatus status)
{
    ChargeScreenLayout layout = charge_screen_layout(theme, status.detail);
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    draw_header(canvas, theme, layout.header, status.view);
    draw_battery(canvas, theme, layout.battery, battery);
    draw_percent(canvas, theme, layout.percent, battery);
    const char *text = i18n_text(power_state(battery));
    Rect state = theme_is_portrait(theme)
                     ? label_centered_area(theme->fonts.title, layout.status, text)
                     : layout.status;
    draw_line(canvas, (TextStyle){theme->fonts.title, theme->colors.text}, state, text);
    if (status.information != NULL) {
        Rect information =
            theme_is_portrait(theme)
                ? label_centered_area(theme->fonts.small, layout.voltage, status.information)
                : layout.voltage;
        draw_line(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                  information, status.information);
    } else {
        draw_voltage(canvas, theme, layout.voltage, battery);
    }
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
