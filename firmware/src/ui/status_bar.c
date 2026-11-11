#include "ui/status_bar.h"

#include "core/string_builder.h"
#include "gfx/text.h"
#include "ui/icons.h"

enum {
    BORDER = 1,
    FILL_INSET = 2,
    NUB_DIVISOR = 5,
    BLUETOOTH_WIDTH_TENTHS = 7,
    BADGE_GAP = 1,
    TENTHS = 10,
    FULL_PERCENT = 100,
    DIGIT_SAMPLE = '0',
    TINT_STEPS = 3
};

static int band_top(const Theme *theme, int height)
{
    int band = theme->metrics.status_height > 0 ? theme->metrics.status_height
                                                : theme->metrics.header_height;
    return (band - height) / 2;
}

static int icon_top(const Theme *theme)
{
    return band_top(theme, theme->metrics.status_icon_size);
}

static int nub_width(int size)
{
    return size / NUB_DIVISOR > 0 ? size / NUB_DIVISOR : 1;
}

Rect status_bar_playback_area(const Theme *theme)
{
    int size = theme->metrics.status_icon_size;
    return (Rect){theme->metrics.padding, icon_top(theme), size, size};
}

Rect status_bar_bluetooth_area(const Theme *theme)
{
    Rect playback = status_bar_playback_area(theme);
    int width = playback.width * BLUETOOTH_WIDTH_TENTHS / TENTHS;
    return (Rect){playback.x + playback.width + theme->metrics.padding / 2, playback.y,
                  width > 0 ? width : 1, playback.height};
}

static int battery_tail(const Theme *theme, int height, bool usb_power)
{
    return usb_power ? icon_width(ICON_BOLT, theme->metrics.status_icon_size) : nub_width(height);
}

Rect status_bar_battery_body(const Theme *theme, bool usb_power)
{
    int height = theme->metrics.status_icon_size + 2 * BORDER;
    int right = theme->metrics.screen_width - theme->metrics.padding;
    int x = right - battery_tail(theme, height, usb_power) - 2 * height;
    return (Rect){x, band_top(theme, height), 2 * height, height};
}

Rect status_bar_battery_area(const Theme *theme, bool usb_power)
{
    Rect body = status_bar_battery_body(theme, usb_power);
    return (Rect){body.x, body.y, theme->metrics.screen_width - theme->metrics.padding - body.x,
                  body.height};
}

Rect status_bar_lock_area(const Theme *theme)
{
    Rect bluetooth = status_bar_bluetooth_area(theme);
    return (Rect){bluetooth.x + bluetooth.width + theme->metrics.padding / 2, bluetooth.y,
                  bluetooth.width, bluetooth.height};
}

static IconId shuffle_badge(StatusShuffle shuffle)
{
    return shuffle == STATUS_SHUFFLE_ALBUMS ? ICON_ALBUM_SMALL : ICON_SONG_SMALL;
}

Rect status_bar_shuffle_area(const Theme *theme, StatusShuffle shuffle)
{
    Rect lock = status_bar_lock_area(theme);
    int width = icon_width(shuffle_badge(shuffle), lock.height) + BADGE_GAP +
                icon_width(ICON_SHUFFLE_SMALL, lock.height);
    return (Rect){lock.x + lock.width + theme->metrics.padding / 2, lock.y, width, lock.height};
}

int status_bar_battery_fill_width(uint8_t percent, int inner_width)
{
    int clamped = percent < FULL_PERCENT ? percent : FULL_PERCENT;
    int width = clamped * inner_width / FULL_PERCENT;
    return clamped > 0 && width == 0 ? 1 : width;
}

void status_bar_percent_text(uint8_t percent, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, percent < FULL_PERCENT ? percent : FULL_PERCENT);
}

static Rect inner_of(Rect body)
{
    return (Rect){body.x + FILL_INSET, body.y + FILL_INSET, body.width - 2 * FILL_INSET,
                  body.height - 2 * FILL_INSET};
}

static int digit_height(const Font *font)
{
    return font_find_glyph(font, DIGIT_SAMPLE)->top;
}

bool status_bar_battery_digits_fit(const Theme *theme)
{
    return digit_height(theme->fonts.small) <=
           theme->metrics.status_icon_size + 2 * BORDER - 2 * FILL_INSET;
}

static Color tint(const Theme *theme, Color color)
{
    return color_between(color, theme->colors.header_text, 1, TINT_STEPS);
}

StatusBatteryLook status_bar_battery_look(const Theme *theme, StatusBarState state)
{
    Color text = theme->colors.header_text;
    Color danger = tint(theme, theme->colors.danger);
    Color success = tint(theme, theme->colors.success);
    bool low = !state.usb_power && state.battery_percent <= STATUS_BAR_LOW_BATTERY_PERCENT;
    bool critical = low && state.battery_percent <= STATUS_BAR_CRITICAL_BATTERY_PERCENT;
    return (StatusBatteryLook){.outline = critical ? danger : text,
                               .fill = state.usb_power ? success
                                       : low           ? danger
                                                       : text,
                               .on_fill = theme->colors.header_background,
                               .on_empty = low ? danger : text,
                               .bolt = state.charging ? success : text};
}

typedef struct Gauge {
    Rect body;
    Rect fill;
    StatusBatteryLook look;
} Gauge;

static void draw_gauge(Canvas *canvas, const Theme *theme, const Gauge *gauge, uint8_t percent)
{
    const Font *font = theme->fonts.small;
    char text[STATUS_BAR_PERCENT_CAPACITY];
    status_bar_percent_text(percent, text, sizeof text);
    if (!status_bar_battery_digits_fit(theme)) {
        text[0] = '\0';
    }
    Rect inner = inner_of(gauge->body);
    Point pen = {inner.x + (inner.width - text_width(font, text) + 1) / 2,
                 inner.y + (inner.height + digit_height(font)) / 2};
    text_draw(canvas, (TextStyle){font, gauge->look.on_empty}, pen, text);
    canvas_fill_rect(canvas, gauge->fill, gauge->look.fill);
    Canvas on_fill = canvas_clipped(canvas, gauge->fill);
    text_draw(&on_fill, (TextStyle){font, gauge->look.on_fill}, pen, text);
}

static void draw_outline(Canvas *canvas, Rect body, Color color)
{
    canvas_fill_rect(canvas, (Rect){body.x, body.y, body.width, BORDER}, color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y + body.height - BORDER, body.width, BORDER},
                     color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y, BORDER, body.height}, color);
    canvas_fill_rect(canvas, (Rect){body.x + body.width - BORDER, body.y, BORDER, body.height},
                     color);
}

static void draw_icon(Canvas *canvas, IconId icon, Rect area, Color color)
{
    icon_draw(canvas, icon, area, (IconInk){color, color});
}

static void draw_tail(Canvas *canvas, const Theme *theme, const Gauge *gauge, bool usb_power)
{
    Rect body = gauge->body;
    int size = theme->metrics.status_icon_size;
    int right = body.x + body.width;
    if (usb_power) {
        Rect bolt = {right, body.y + BORDER, icon_width(ICON_BOLT, size), size};
        draw_icon(canvas, ICON_BOLT, bolt, gauge->look.bolt);
        return;
    }
    int third = body.height / 3;
    Rect tip = {right, body.y + third, nub_width(body.height), body.height - 2 * third};
    canvas_fill_rect(canvas, tip, gauge->look.outline);
}

static void draw_battery(Canvas *canvas, const Theme *theme, StatusBarState state)
{
    Rect body = status_bar_battery_body(theme, state.usb_power);
    Gauge gauge = {body, inner_of(body), status_bar_battery_look(theme, state)};
    gauge.fill.width = status_bar_battery_fill_width(state.battery_percent, gauge.fill.width);
    draw_outline(canvas, body, gauge.look.outline);
    draw_gauge(canvas, theme, &gauge, state.battery_percent);
    draw_tail(canvas, theme, &gauge, state.usb_power);
}

static void draw_shuffle(Canvas *canvas, const Theme *theme, StatusShuffle shuffle)
{
    Rect area = status_bar_shuffle_area(theme, shuffle);
    Color color = theme->colors.header_text;
    int arrows = icon_width(ICON_SHUFFLE_SMALL, area.height);
    draw_icon(canvas, shuffle_badge(shuffle),
              (Rect){area.x, area.y, area.width - BADGE_GAP - arrows, area.height}, color);
    draw_icon(canvas, ICON_SHUFFLE_SMALL,
              (Rect){area.x + area.width - arrows, area.y, arrows, area.height}, color);
}

void status_bar_draw(Canvas *canvas, const Theme *theme, StatusBarState state)
{
    Rect icon = status_bar_playback_area(theme);
    Color color = theme->colors.header_text;
    if (state.playback == STATUS_PLAYBACK_PLAYING) {
        draw_icon(canvas, ICON_PLAY, icon, color);
    } else if (state.playback == STATUS_PLAYBACK_PAUSED) {
        draw_icon(canvas, ICON_PAUSE, icon, color);
    }
    if (state.bluetooth != STATUS_BLUETOOTH_OFF) {
        Color tint =
            state.bluetooth == STATUS_BLUETOOTH_CONNECTED ? color : theme->colors.text_tertiary;
        draw_icon(canvas, ICON_BLUETOOTH_SMALL, status_bar_bluetooth_area(theme), tint);
    }
    if (state.locked) {
        draw_icon(canvas, ICON_LOCK, status_bar_lock_area(theme), color);
    }
    if (state.shuffle != STATUS_SHUFFLE_OFF) {
        draw_shuffle(canvas, theme, state.shuffle);
    }
    if (state.battery_known) {
        draw_battery(canvas, theme, state);
    }
}
